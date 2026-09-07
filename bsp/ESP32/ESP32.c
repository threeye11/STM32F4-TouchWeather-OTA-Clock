/****************************************************************************************************
//=========================================接线================================================//
//     ESP32C3                      STM32F407VET6
//      IO6(RX)          接        PD5(USART2_TX)
//      IO7(TX)          接        PD6(USART2_RX)
//			GND二者共地
//
//  FreeRTOS 适配:
//  - TX: DMA1_Stream6 发送（非阻塞）
//  - RX: DMA1_Stream5 + IDLE 中断（不定长接收）
//  - ACK 等待: 二值信号量 xSemACK（替代 delay_ms(1) 轮询）
//  - 注意: 中断优先级必须 >= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY(5) 才能调用 FreeRTOS API
**************************************************************************************************/


#include "esp32.h"
#include "board.h"
#include <stdbool.h>
#include <string.h>
#include <stdio.h>

/* FreeRTOS 头文件 */
#include "FreeRTOS.h"
#include "semphr.h"
#include "task.h"
#include "bsp_uart.h"

/* 外部句柄 main.c 中创建） */
extern SemaphoreHandle_t xSemACK;

#define ESP32_RX_BUF_SIZE   8192	/* 8KB：单片 206+4096B 响应可能一条连续突发，必须整体容纳，否则超缓冲整块丢弃 */

static char rx_buffer[ESP32_RX_BUF_SIZE + 1];	/* 最近一次空闲突发的线性副本（ACK 匹配/诊断用） */
static char dma_ring[ESP32_RX_BUF_SIZE];	/* 循环 DMA 接收环：永不停机，ISR 线性化后处理 */
static volatile uint16_t ring_rd = 0;        /* 环读指针（仅 ISR 维护） */

/* +IPD 帧队列：ISR 拷贝入队，OTA 解析按序消费。
 * 旧设计任务直接读 rx_buffer，但 DMA 每次空闲中断就重启回开头，
 * 4KB 级下载响应的后继突发会覆盖前一批（实测 got=0 整包收不到） */
#define ESP32_FRAME_SLOTS   4	/* 4×2048：大突发内多帧关闭不溢出（计数制防覆盖） */
#define ESP32_SLOT_CAP      2048
static char     frame_fifo[ESP32_FRAME_SLOTS][ESP32_SLOT_CAP];
static volatile uint16_t frame_len[ESP32_FRAME_SLOTS];
static volatile uint32_t g_dm_closed;        /* ISR：已关闭（待消费）帧数 */
static uint32_t dm_popped;                   /* 任务：已消费帧数 */

/* +IPD 解复用状态：从 UART 字节流剥出纯 TCP 数据。
 * esp-at 输出 "+IPD,<len>:<data>"，data 可跨多次空闲突发、一次突发可含多帧。
 * 只有 data 进 FIFO——OK/回显/URC 等控制文本留在 rx_buffer 供 AT 应答匹配 */
static uint8_t  dm_phase;        /* 0=找帧头令牌 1=读长度 2=收数据 */
static uint8_t  dm_match;        /* 令牌已匹配字节数 */
static uint32_t dm_len;          /* 本帧数据总长 */
static uint32_t dm_remain;       /* 本帧剩余数据字节 */
static uint8_t  dm_open;         /* FIFO 尾槽有未封口数据 */
static uint32_t dm_lost;         /* 队满丢弃统计 */
static char     dm_token[16] = "+IPD,";      /* 当前帧头令牌 */
static volatile uint8_t dm_token_len = 5;
static volatile char    dm_term = ':';       /* 长度字段结束符 */

/* RX 黑匣子（联调用）：突发长度环形记录 + 解复用计数 */
volatile uint16_t g_rx_burst[16];
volatile uint8_t  g_rx_burst_n;
volatile uint32_t g_dm_frames, g_dm_extracted, g_dm_lost;
volatile uint32_t g_dm_decl[16];        /* 每帧声明长度 */
volatile uint8_t  g_dm_decl_n;
volatile uint8_t  g_dm_first[8];        /* 最新帧数据首 4 字节 */
volatile uint8_t  g_dm_last[8];         /* 最新帧数据尾 4 字节 */
static volatile uint16_t rx_index = 0;
static volatile ESP32_ACK current_ack = AT_ACK_NONE;

/* MQTT 下发帧捕获（ISR 复制，防 rx_buffer 被下一帧覆盖） */
#define ESP32_MQTT_FRAME_SIZE  1024
static char     mqtt_frame[ESP32_MQTT_FRAME_SIZE];
static volatile uint8_t mqtt_link_up = 0;
/* CIPSEND 握手粘性标志：ISR 检测到就置 1，任务轮询清零。
 * 不用信号量：esp-at 的 OK / > / Recv n bytes / SEND OK 可能任意合并分帧，
 * 每帧发一次令牌会拿错时序（实测拿到 "Recv n bytes" 帧的过期令牌） */
static volatile uint8_t tcp_prompt   = 0;   /* 帧 contains '>' */
static volatile uint8_t tcp_send_ok  = 0;   /* 帧 contains "SEND OK" */

/* DMA 发送完成标志 */
static volatile bool dma_tx_done = true;

/* USART2 初始化（GPIO + UART + NVIC）*/
void USART2_Init(uint32_t __Baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	USART_InitTypeDef USART_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOD, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART2, ENABLE);

	/* PD5=TX, PD6=RX, 复用推挽 */
	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_5 | GPIO_Pin_6;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_Init(GPIOD, &GPIO_InitStructure);

	GPIO_PinAFConfig(GPIOD, GPIO_PinSource5, GPIO_AF_USART2);
	GPIO_PinAFConfig(GPIOD, GPIO_PinSource6, GPIO_AF_USART2);

	USART_DeInit(USART2);
	USART_InitStructure.USART_BaudRate            = __Baud;
	USART_InitStructure.USART_Mode                = USART_Mode_Rx | USART_Mode_Tx;
	USART_InitStructure.USART_WordLength          = USART_WordLength_8b;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Parity              = USART_Parity_No;
	USART_InitStructure.USART_StopBits            = USART_StopBits_1;
	USART_Init(USART2, &USART_InitStructure);

	/*
	 * 注意: 不使能 RXNE 中断！
	 * RX 使用 DMA1_Stream5 接收，IDLE 中断触发一帧完成处理
	 * 如果同时使能 RXNE，会和 DMA 冲突
	 */
	USART_ClearFlag(USART2, USART_FLAG_RXNE);
	USART_ClearFlag(USART2, USART_FLAG_TC);

	USART_Cmd(USART2, ENABLE);

	/* USART2 中断（IDLE 线路检测）—— 优先级必须 >= 5，否则不能调用 FreeRTOS API */
	NVIC_InitStructure.NVIC_IRQChannel                   = USART2_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

/* DMA 初始化 TX + RX */
void USART2_DMA_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);

	DMA_StructInit(&DMA_InitStructure);	/* 先填默认值必须初始化，栈上垃圾值会让 RX 流出错收不到数据 */

	/* ---- TX: DMA1_Stream6_Channel4 = USART2_TX ---- */
	DMA_DeInit(DMA1_Stream6);
	while (DMA_GetCmdStatus(DMA1_Stream6) != DISABLE);

	DMA_InitStructure.DMA_Channel            = DMA_Channel_4;
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr    = 0;
	DMA_InitStructure.DMA_DIR                = DMA_DIR_MemoryToPeripheral;
	DMA_InitStructure.DMA_BufferSize         = 0;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc          = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize     = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority           = DMA_Priority_Medium;
	DMA_InitStructure.DMA_FIFOMode           = DMA_FIFOMode_Disable;
	DMA_Init(DMA1_Stream6, &DMA_InitStructure);

	DMA_ITConfig(DMA1_Stream6, DMA_IT_TC, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel                   = DMA1_Stream6_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

	USART_DMACmd(USART2, USART_DMAReq_Tx, ENABLE);

	/* ---- RX: DMA1_Stream5_Channel4 = USART2_RX ---- */
	DMA_DeInit(DMA1_Stream5);
	while (DMA_GetCmdStatus(DMA1_Stream5) != DISABLE);

	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART2->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr    = (uint32_t)dma_ring;	/* 循环环缓冲：ISR 线性化到 rx_buffer 后处理 */
	DMA_InitStructure.DMA_DIR                = DMA_DIR_PeripheralToMemory;
	DMA_InitStructure.DMA_BufferSize         = ESP32_RX_BUF_SIZE;
	/* 循环模式永不停机：旧 Normal 模式每次空闲中断复位 DMA，复位窗口内到达的
	 * 字节直接丢失，且重启后 DMA 从头覆盖 rx_buffer 而解复用还在读——
	 * +IPD 帧头被吞即解复用失步，后续整帧被当噪声丢弃（下载固定只收 2920B 的根因） */
	DMA_InitStructure.DMA_Mode               = DMA_Mode_Circular;
	DMA_InitStructure.DMA_Priority           = DMA_Priority_High;
	DMA_Init(DMA1_Stream5, &DMA_InitStructure);

	DMA_Cmd(DMA1_Stream5, ENABLE);
	ring_rd = 0;                             /* 与环写指针(=8192-NDTR=0)对齐 */

	USART_DMACmd(USART2, USART_DMAReq_Rx, ENABLE);
	USART_ITConfig(USART2, USART_IT_IDLE, ENABLE);
}

/* DMA 中断服务函数 */
void DMA1_Stream6_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA1_Stream6, DMA_IT_TCIF6))
	{
		DMA_ClearITPendingBit(DMA1_Stream6, DMA_IT_TCIF6);
		dma_tx_done = true;
	}
}

/* DMA 发送（非阻塞，启动后立即返回）*/
void ESP32_DMA_Send(const char *data, uint32_t len)
{
	while (!dma_tx_done);
	safe_printf("[TXDMA] %u B: %.24s\r\n", (unsigned)len, data);
	dma_tx_done = false;
	DMA1_Stream6->M0AR = (uint32_t)data;
	DMA1_Stream6->NDTR = len;
	DMA_Cmd(DMA1_Stream6, ENABLE);
}

/* ---- +IPD 解复用 ---- */
static void dm_close(void)
{
	if (!dm_open) return;
	/* 未消费帧占满全部槽：保持开启继续攒，等消费者腾位（计数制，无覆盖） */
	if (g_dm_closed - dm_popped >= ESP32_FRAME_SLOTS)
		return;
	dm_open = 0;
	g_dm_closed++;
	g_dm_frames++;
	if (xSemIPD != NULL)
	{
		BaseType_t w = pdFALSE;
		xSemaphoreGiveFromISR(xSemIPD, &w);
		portYIELD_FROM_ISR(w);
	}
}

static void dm_put(uint8_t b)
{
	uint8_t slot = (uint8_t)(g_dm_closed % ESP32_FRAME_SLOTS);
	if (!dm_open)
	{
		if (g_dm_closed - dm_popped >= ESP32_FRAME_SLOTS)
		{
			dm_lost++;
			g_dm_lost++;                 /* 未消费帧占满槽：丢弃（消费停摆才可能） */
			return;
		}
		dm_open = 1;
		frame_len[slot] = 0;
	}
	if (frame_len[slot] >= ESP32_SLOT_CAP)
	{
		dm_close();                        /* 满槽封口（未消费占满则保持开启） */
		slot = (uint8_t)(g_dm_closed % ESP32_FRAME_SLOTS);
		if (dm_open)                       /* 封口失败：未消费占满，丢此字节 */
		{
			dm_lost++;
			g_dm_lost++;
			return;
		}
		frame_len[slot] = 0;
	}
	frame_fifo[slot][frame_len[slot]++] = b;
	g_dm_extracted++;
	if (frame_len[slot] <= 4) g_dm_first[frame_len[slot] - 1] = b;
}

static void demux_feed(const char *d, uint16_t n)
{
	while (n--)
	{
		uint8_t b = (uint8_t)*d++;
		if (dm_phase == 2)                       /* 收数据：按长度计数，内容不扫描 */
		{
			dm_put(b);
			if (dm_remain <= 4) g_dm_last[dm_remain - 1] = b;
			if (--dm_remain == 0) { dm_close(); dm_phase = 0; dm_match = 0; }
			continue;
		}
		if (dm_phase == 1)                       /* 读十进制长度直到 ':' */
		{
			if (b >= '0' && b <= '9')
			{
				dm_len = dm_len * 10 + (uint32_t)(b - '0');
			}
			else if (b == dm_term)
			{
				dm_remain = dm_len;
				dm_phase = 2;
				g_dm_decl[g_dm_decl_n++ & 15] = (uint32_t)dm_len;
				dm_len = 0;
				if (dm_remain == 0) { dm_close(); dm_phase = 0; }
			}
			else                                     /* 异常：回找头 */
			{
				dm_phase = 0; dm_match = 0; dm_len = 0;
			}
			continue;
		}
		if (b == dm_token[dm_match])             /* 找帧头令牌 */
		{
			dm_match++;
			if (dm_match == dm_token_len) { dm_phase = 1; dm_match = 0; dm_len = 0; }
		}
		else
		{
			dm_match = (b == '+') ? 1 : 0;
		}
	}
}

/*===========================================================================
 *  USART2 中断服务函数（IDLE 线路检测 + 循环 DMA 接收）
 *
 *  工作流程:
 *  1. DMA1_Stream5 循环模式自动将 USART2 收到的字节写入 dma_ring（永不停机）
 *  2. 当总线空闲（~1 字符时间无新数据），IDLE 标志置位，本 ISR 触发
 *  3. 读出环写指针，把新到字节"先线性化拷贝到 rx_buffer、后处理"：
 *     拷贝期间 DMA 继续往环里写，互不干扰；不存在丢字节窗口
 *  4. ACK 尾部匹配 -> 释放 xSemACK；+IPD 解复用 -> 帧队列 -> xSemIPD
 *===========================================================================*/
void USART2_IRQHandler(void)
{
	if (USART_GetITStatus(USART2, USART_IT_IDLE) != RESET)
	{
		uint16_t wr, rd, n, first;

		/* 清除 IDLE 标志必须先读状态再读数据（顺带清 ORE） */
		(void)USART2->SR;
		(void)USART2->DR;

		/* 环写指针 = 8192 - NDTR；wr==rd 视为无新数据（写读重合的满环
		 * 需要 712ms 连续无空闲的数据流才可能，+IPD 帧间必有间隙，不设防） */
		wr = (uint16_t)(ESP32_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Stream5));
		rd = ring_rd;
		if (wr == rd) return;
		n = (uint16_t)((wr > rd) ? (wr - rd) : (ESP32_RX_BUF_SIZE - rd + wr));
		first = (uint16_t)(ESP32_RX_BUF_SIZE - rd);
		if (first > n) first = n;
		memcpy(rx_buffer, &dma_ring[rd], first);
		if (n > first) memcpy(&rx_buffer[first], dma_ring, (size_t)(n - first));
		ring_rd = wr;

		rx_index = n;
		rx_buffer[n] = '\0';				//在结尾手动补结束符
		g_rx_burst[g_rx_burst_n++ & 15] = n;

		/* 遍历 ack_table 做尾部匹配 */
		{
			uint8_t i;
			for (i = 0; i < (sizeof(ack_table) / sizeof(ack_table[0])); i++)
			{
				uint16_t ack_len = (uint16_t)strlen(ack_table[i].str);
				if (rx_index >= ack_len &&
				    strcmp(&rx_buffer[rx_index - ack_len], ack_table[i].str) == 0)
				{
					current_ack = ack_table[i].ack;
					break;
				}
			}
		}

		/* ACK 匹配成功，释放信号量唤醒 ESP32 任务 */
		if (current_ack != AT_ACK_NONE && xSemACK != NULL)
		{
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xSemaphoreGiveFromISR(xSemACK, &xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}

		/* +IPD 解复用：剥出纯 TCP 数据按序入 FIFO（帧可跨突发） */
		demux_feed((const char *)rx_buffer, rx_index);
		dm_close();                          /* 突发结束：未满槽封口交付 */

		/* MQTT 下发帧：复制到专用缓冲并唤醒（rx_buffer 会被下一帧覆盖） */
		if (strstr(rx_buffer, "+MQTTSUBRECV") != NULL && xSemMQTT != NULL)
		{
			strncpy(mqtt_frame, rx_buffer, ESP32_MQTT_FRAME_SIZE - 1);
			mqtt_frame[ESP32_MQTT_FRAME_SIZE - 1] = '\0';
			{
				BaseType_t xHigherPriorityTaskWoken3 = pdFALSE;
				xSemaphoreGiveFromISR(xSemMQTT, &xHigherPriorityTaskWoken3);
				portYIELD_FROM_ISR(xHigherPriorityTaskWoken3);
			}
		}
		if (strstr(rx_buffer, "+MQTTDISCONNECTED") != NULL)
			mqtt_link_up = 0;
		/* CIPSEND 握手粘性标志 */
		if (strstr(rx_buffer, ">") != NULL)
			tcp_prompt = 1;
		if (strstr(rx_buffer, "SEND OK") != NULL)
			tcp_send_ok = 1;
	}
}

/*===========================================================================
 *  命令发送 + 等待 ACK
 *
 *  调度器运行中: 用信号量阻塞等待（不浪费 CPU）
 *  调度器未启动: 忙等轮询（init 阶段使用）
 *===========================================================================*/
bool ESP32_WriteCmd(const char *cmd, uint32_t timeout_ms)
{
	/* 清空接收状态，丢弃旧数据 */
	rx_index = 0;
	current_ack = AT_ACK_NONE;
	memset(rx_buffer, 0, sizeof(rx_buffer));
	xSemaphoreTake(xSemACK, 0);	/* 丢弃残留令牌：上一次响应可能已填过信号量，否则本次会立即拿到旧令牌而误判超时 */

	safe_printf("[AT] > %.48s\r\n", cmd);	/* TX 侦听：核对每条真正发出的命令（联调期临时） */
	/* 逐字节发送命令 */
	while (*cmd)
	{
		USART_SendData(USART2, (uint8_t)*cmd++);
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
	}
	USART_SendData(USART2, '\r');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);
	USART_SendData(USART2, '\n');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET);

	/* 等待 ACK */
	if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
	{
		/* 调度器运行中：用信号量阻塞等待，不浪费 CPU */
		if (xSemaphoreTake(xSemACK, pdMS_TO_TICKS(timeout_ms)) == pdTRUE)
		{
			return (current_ack == AT_ACK_OK);
		}
		return false;
	}
	else
	{
		/* 调度器未启动（init 阶段）：忙等轮询 */
		while (timeout_ms--)
		{
			if (current_ack == AT_ACK_OK)  return true;
			if (current_ack == AT_ACK_ERROR || current_ack == AT_ACK_BUSY) return false;
			delay_ms(1);
		}
		return false;
	}
}

/* 等待 ESP32 模块上电就绪（收到 "ready"）*/
bool ESP32_WaitReady(uint32_t timeout_ms)
{
	rx_index = 0;
	current_ack = AT_ACK_NONE;
	memset(rx_buffer, 0, sizeof(rx_buffer));

	while (timeout_ms--)
	{
		if (current_ack == AT_ACK_READY) return true;
		delay_ms(1);
	}
	return false;
}

/* 获取最近一次 AT 响应的原始字符串 */
const char *ESP32_GetResponse(void)
{
	return rx_buffer;
}

/* ---- OTA 下载专用：按序 +IPD 帧队列 ---- */
const char *ESP32_GetFrame(void)    { return frame_fifo[dm_popped % ESP32_FRAME_SLOTS]; }
uint16_t    ESP32_GetFrameLen(void) { return frame_len[dm_popped % ESP32_FRAME_SLOTS]; }
void        ESP32_PopFrame(void)    { dm_popped++; }
uint32_t    ESP32_FramesPending(void) { return g_dm_closed - dm_popped; }
void        ESP32_FrameFlush(void)
{
	dm_popped = g_dm_closed;
	dm_open = 0; dm_phase = 0; dm_match = 0; dm_len = 0; dm_remain = 0;
	g_dm_decl_n = 0;
}

/* RX 诊断转储：最近 16 次突发长度 + 解复用计数 */
void ESP32_RxDiag(void)
{
	uint8_t k, n = g_rx_burst_n;
	safe_printf("[RXDIAG] frames=%u ext=%u lost=%u, bursts:",
	            (unsigned)g_dm_frames, (unsigned)g_dm_extracted, (unsigned)g_dm_lost);
	for (k = 0; k < 16 && k < n; k++)
		safe_printf(" %u", (unsigned)g_rx_burst[k]);
	safe_printf("\r\n");
	safe_printf("[RXDIAG] decl:");
	for (k = 0; k < 16 && k < g_dm_decl_n; k++)
		safe_printf(" %u", (unsigned)g_dm_decl[k]);
	safe_printf("\r\n[RXDIAG] last frame first4: %02X %02X %02X %02X / last4: %02X %02X %02X %02X\r\n",
	            g_dm_first[0], g_dm_first[1], g_dm_first[2], g_dm_first[3],
	            g_dm_last[0], g_dm_last[1], g_dm_last[2], g_dm_last[3]);
}

/* 配置解复用帧头令牌："+IPD," 配 ':'；"+HTTPCLIENT:" 配 ',' */
void ESP32_DmuxMode(const char *token, char term)
{
	dm_token_len = 0;
	while (token[dm_token_len] && dm_token_len < sizeof(dm_token) - 1)
	{
		dm_token[dm_token_len] = token[dm_token_len];
		dm_token_len++;
	}
	dm_token[dm_token_len] = '\0';
	dm_term = term;
	dm_phase = 0; dm_match = 0; dm_len = 0; dm_remain = 0;
	dm_open = 0;
}

/* 裸发一行命令（不等应答）：数据型命令的应答时序由调用者按流消费 */
void ESP32_SendLine(const char *s)
{
	while (!dma_tx_done);
	while (*s)
	{
		USART_SendData(USART2, (uint8_t)*s++);
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
	}
	USART_SendData(USART2, '\r');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
	USART_SendData(USART2, '\n');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
}

/* 带数据的 AT 命令（AT+HTTPCHEAD 等，与 CIPSEND 同款 ">"+数据 握手） */
bool ESP32_CmdWithPayload(const char *cmd, const char *data, uint16_t len)
{
	uint32_t t;
	const char *p;

	rx_index = 0;
	current_ack = AT_ACK_NONE;
	memset(rx_buffer, 0, sizeof(rx_buffer));
	xSemaphoreTake(xSemACK, 0);
	tcp_prompt  = 0;
	tcp_send_ok = 0;

	p = cmd;
	while (*p)
	{
		USART_SendData(USART2, (uint8_t)*p++);
		while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
	}
	USART_SendData(USART2, '\r');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
	USART_SendData(USART2, '\n');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;

	for (t = 0; t < 300 && !tcp_prompt; t++)
		vTaskDelay(pdMS_TO_TICKS(10));
	if (!tcp_prompt)
	{
		safe_printf("[ESP32] no prompt, resp:[%s]\r\n", ESP32_GetResponse());
		return false;
	}

	ESP32_DMA_Send(data, len);
	vTaskDelay(pdMS_TO_TICKS(300));     /* 数据落定（完成标志因命令而异） */
	return true;
}

/* HTTPS GET（CIP 通道）：返回 body 指针（静态缓冲），失败返回 NULL。
 * 不用 AT+HTTPCLIENT：其遗留状态疑似干扰后续 CIP 接收（详见开发文档排障记录） */
const char *ESP32_HttpGetCIP(const char *url)
{
	static char body[1024];
	char     host[64], path[256], req[512];
	char     tail[4], hbuf[300];
	uint8_t  tail_n = 0, in_head = 1, done = 0;
	uint16_t blen = 0, hn = 0;
	int32_t  cl = -1, got = 0;
	uint32_t quiet = 0;
	const char *p;
	int      i, fl;

	safe_printf("[NET] CIP-GET %s\r\n", url);
	p = strstr(url, "://");
	if (!p) return NULL;
	p += 3;
	i = 0;
	while (*p && *p != '/' && i < (int)sizeof(host) - 1) host[i++] = *p++;
	host[i] = '\0';
	if (*p != '/') return NULL;
	strncpy(path, p, sizeof(path) - 1);
	path[sizeof(path) - 1] = '\0';

	if (!ESP32_TcpConnect(host, 443, "SSL")) return NULL;

	ESP32_DmuxMode("+IPD,", ':');
	snprintf(req, sizeof(req),
	         "GET %s HTTP/1.1\r\nHost: %s\r\n\r\n", path, host);
	if (!ESP32_TcpSend(req, (uint16_t)strlen(req)))
	{
		ESP32_TcpClose();
		return NULL;
	}

	for (;;)
	{
		if (xSemaphoreTake(xSemIPD, pdMS_TO_TICKS(300)) == pdTRUE)
		{
			const char *fr = ESP32_GetFrame();
			quiet = 0;
			fl = ESP32_GetFrameLen();
			for (i = 0; i < fl && !done; i++)
			{
				char c = fr[i];
				if (in_head)
				{
					if (hn < (int)sizeof(hbuf) - 1) hbuf[hn++] = c;
					if (tail_n < 4) tail[tail_n++] = c;
					else { tail[0] = tail[1]; tail[1] = tail[2]; tail[2] = tail[3]; tail[3] = c; }
					if (tail_n == 4 && tail[0] == '\r' && tail[1] == '\n' &&
					    tail[2] == '\r' && tail[3] == '\n')
					{
						const char *clp;
						in_head = 0;
						hbuf[hn] = '\0';
						clp = strstr(hbuf, "Content-Length:");
						cl = clp ? atoi(clp + 15) : (1 << 20);   /* no CL: fill buffer */
					}
				}
				else if (got < cl)
				{
					if (blen < (int)sizeof(body) - 1) body[blen++] = c;
					if (++got >= cl) done = 1;              /* consume exactly CL */
				}
			}
			ESP32_PopFrame();
		}
		else
		{
			quiet++;
			if (done && quiet >= 5) break;   /* CL 满后再抽干 1.5s：清空 esp-at SSL 缓冲尾流 */
			if (!done && quiet >= 30) break;
		}
	}
	safe_printf("[NET] resp-head(%u): %.*s\r\n",
	            (unsigned)hn, (int)(hn < 120 ? hn : 120), hbuf);
	ESP32_TcpClose();
	body[blen] = '\0';
	if (!blen) ESP32_RxDiag();
	return blen ? body : NULL;
}

/* OTA 前置：复位 ESP32 模块并重连 WiFi（跳过天气/MQTT）。
 * esp-at 的 SSL 收包缓冲在 AT+CIPCLOSE 后不清零，上一条 SSL 连接的尾流
 * 会混入下一条连接的 +IPD 流（实测天气数据入侵 OTA 响应），
 * 模块复位是唯一保证零残留的手段 */
bool ESP32_OTA_PreFlight(const char *ssid, const char *pwd)
{
	ESP32_WriteCmd("AT+RESTORE", 2000);      /* 模块复位（容错：失败继续试 ready） */
	if (!ESP32_WaitReady(5000)) return false;
	delay_ms(3000);                          /* 复位后射频校准/关联环境稳定等待 */
	ESP32_WriteCmd("ATE0", 1000);
	if (!ESP32_WriteCmd("AT+CWMODE=1", 2000)) return false;
	{
		uint8_t retry;                       /* 复位后入网较慢：5 次 × 5s 间隔 */
		for (retry = 0; retry < 5; retry++)
		{
			if (ESP32_ConnetWifi(ssid, pwd)) break;
			safe_printf("[OTA] preflight wifi retry %d\r\n", retry + 1);
			vTaskDelay(pdMS_TO_TICKS(5000));
		}
		if (retry == 5) return false;
	}
	mqtt_link_up = 0;                        /* 模块复位后 MQTT 链路已不存在 */
	return true;
}

/* 初始化握手（AT 重试探活 -> AT+RESTORE -> wait ready）*/
bool ESP32_Init(void)
{
	uint8_t retry;

	USART2_Init(115200);
	USART2_DMA_Init();
	delay_ms(500);	/* 等模块上电/复位稳定 */

	/* AT 探活：模块冷启动需要 1~2 秒，最多重试 5 次，每次失败打印收到的原始应答便于定位 */
	for (retry = 0; retry < 5; retry++)
	{
		if (ESP32_WriteCmd("AT", 500)) break;
		printf("[ESP32] AT try%u FAIL, resp:[%s]\r\n", retry, ESP32_GetResponse());
	}
	if (retry >= 5)
	{
		printf("[ESP32] no AT response! check wiring TX-RX cross / GND / baud\r\n");
		return false;
	}

	if (!ESP32_WriteCmd("AT+RESTORE", 2000))
	{
		printf("[ESP32] RESTORE FAIL, resp:[%s]\r\n", ESP32_GetResponse());
		return false;
	}
	if (!ESP32_WaitReady(5000))
	{
		printf("[ESP32] no ready, resp:[%s]\r\n", ESP32_GetResponse());
		return false;
	}
	delay_ms(1000);
	/* 关回显：OTA 大流量期间曾观测到历史命令回显被复现并污染接收流，从源头关掉。
	 * 应答匹配只依赖 OK/ERROR 等"响应"，不依赖回显，故无副作用 */
	ESP32_WriteCmd("ATE0", 1000);
	printf("[ESP32] Init OK\r\n");
	return true;
}

/* 业务命令 */
bool ESP32_ConnetWifi(const char *ssid, const char *pwd)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "AT+CWJAP=\"%s\",\"%s\"", ssid, pwd);
	if (!ESP32_WriteCmd(cmd, 15000))
	{
		printf("[DEBUG] CWJAP fail, ack=%d, resp:\r\n%s\r\n", (int)current_ack, rx_buffer);
		return false;
	}
	return true;
}

bool ESP32_WifiInit(void)
{
	return ESP32_WriteCmd("AT+CWMODE=1", 2000);
}

bool ESP32_SntpInit(void)
{
	return ESP32_WriteCmd("AT+CIPSNTPCFG=1,8,\"ntp.aliyun.com\",\"cn.ntp.org.cn\"", 3000);
}

static uint8_t WeekdayToNum(const char *str)
{
	if (strcmp(str, "Mon") == 0) return 1;
	if (strcmp(str, "Tue") == 0) return 2;
	if (strcmp(str, "Wed") == 0) return 3;
	if (strcmp(str, "Thu") == 0) return 4;
	if (strcmp(str, "Fri") == 0) return 5;
	if (strcmp(str, "Sat") == 0) return 6;
	if (strcmp(str, "Sun") == 0) return 7;
	return 0;
}

static uint8_t MonthToNum(const char *str)
{
	const char *months[] = {"Jan","Feb","Mar","Apr","May","Jun",
	                        "Jul","Aug","Sep","Oct","Nov","Dec"};
	for (uint8_t i = 0; i < 12; i++)
	{
		if (strcmp(str, months[i]) == 0)
			return (uint8_t)(i + 1);
	}
	return 1;
}

bool ESP32_SntpGetTime(ESP32_DataTime *date)
{
	if (!ESP32_WriteCmd("AT+CIPSNTPTIME?", 3000)) return false;

	/* +CIPSNTPTIME:Wed Aug 20 10:30:15 2026 */
	const char *response = strstr(rx_buffer, "+CIPSNTPTIME:");
	if (!response) return false;

	char weekday_str[4] = {0};
	char month_str[4]   = {0};
	int day = 0, hour = 0, minute = 0, second = 0, year = 0;

	if (sscanf(response, "+CIPSNTPTIME:%3s %3s %d %d:%d:%d %d",
	           weekday_str, month_str, &day, &hour, &minute, &second, &year) != 7)
	{
		return false;
	}

	date->year    = (uint16_t)year;
	date->month   = MonthToNum(month_str);
	date->day     = (uint8_t)day;
	date->hour    = (uint8_t)hour;
	date->minute  = (uint8_t)minute;
	date->second  = (uint8_t)second;
	date->weekday = WeekdayToNum(weekday_str);

	return true;
}

const char *ESP32_HttpGet(const char *url)
{
	static char cmd[512];
	/* 2=GET, 1=application/json, 最后的 2=HTTPS */
	snprintf(cmd, sizeof(cmd), "AT+HTTPCLIENT=2,1,\"%s\",,,2", url);
	safe_printf("[NET] HttpGet <- %s\r\n", url);	/* 观测：天气抓取的精确时刻 */
	if (ESP32_WriteCmd(cmd, 10000))
	{
		return rx_buffer;
	}
	return NULL;
}


/*===========================================================================
 *  OTA 原始 TCP 支持（AT+CIPSTART / CIPSEND / CIPCLOSE）
 *===========================================================================*/
uint16_t ESP32_GetRxLen(void)
{
	return rx_index;
}

bool ESP32_TcpConnect(const char *host, uint16_t port, const char *proto)
{
	char cmd[96];
	snprintf(cmd, sizeof(cmd), "AT+CIPSTART=\"%s\",\"%s\",%u", proto, host, (unsigned)port);
	if (!ESP32_WriteCmd(cmd, 20000))         /* SSL 握手更慢，放宽超时 */
	{
		safe_printf("[ESP32] %s connect fail, resp:[%s]\r\n", proto, ESP32_GetResponse());
		return false;
	}
	return true;
}

bool ESP32_TcpClose(void)
{
	/* 未连接时回 ERROR 也视为成功（幂等关闭） */
	ESP32_WriteCmd("AT+CIPCLOSE", 3000);
	return true;
}

/* CIPSEND：AT+CIPSEND=n -> 等 '>'（粘性标志轮询）-> 发数据 -> 等 SEND OK */
bool ESP32_TcpSend(const char *data, uint16_t len)
{
	char cmd[32];
	uint32_t t;

	rx_index = 0;
	current_ack = AT_ACK_NONE;
	memset(rx_buffer, 0, sizeof(rx_buffer));
	xSemaphoreTake(xSemACK, 0);

	tcp_prompt  = 0;
	tcp_send_ok = 0;

	snprintf(cmd, sizeof(cmd), "AT+CIPSEND=%u", (unsigned)len);
	{
		const char *p = cmd;                 /* 数组名不可自增，经指针遍历 */
		while (*p)
		{
			USART_SendData(USART2, (uint8_t)*p++);
			while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
		}
	}
	USART_SendData(USART2, '\r');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;
	USART_SendData(USART2, '\n');
	while (USART_GetFlagStatus(USART2, USART_FLAG_TXE) == RESET) ;

	/* 等 '>'：3s，10ms 轮询粘性标志（OK 帧不用管，ISR 只认 '>'） */
	for (t = 0; t < 300 && !tcp_prompt; t++)
		vTaskDelay(pdMS_TO_TICKS(10));
	if (!tcp_prompt)
	{
		safe_printf("[ESP32] CIPSEND no prompt, resp:[%s]\r\n", ESP32_GetResponse());
		return false;
	}

	ESP32_DMA_Send(data, len);

	/* 等 SEND OK：5s，10ms 轮询（"Recv n bytes" 帧不影响粘性标志） */
	for (t = 0; t < 500 && !tcp_send_ok; t++)
		vTaskDelay(pdMS_TO_TICKS(10));
	if (!tcp_send_ok)
	{
		safe_printf("[ESP32] no SEND OK, resp:[%s]\r\n", ESP32_GetResponse());
		return false;
	}
	return true;
}


/*===========================================================================
 *  OneNET MQTT（esp-at 内部 MQTT 客户端，不占用 CIP 链路）
 *  鉴权方案：clientid=设备名  username=产品ID  password=token
 *===========================================================================*/
bool ESP32_MqttSetup(const char *client, const char *user, const char *pass)
{
	char cmd[384];
	snprintf(cmd, sizeof(cmd), "AT+MQTTUSERCFG=0,1,\"%s\",\"%s\",\"%s\",0,0,\"\"",
	         client, user, pass);
	return ESP32_WriteCmd(cmd, 5000);
}

bool ESP32_MqttConnect(const char *host, uint16_t port)
{
	char cmd[128];
	snprintf(cmd, sizeof(cmd), "AT+MQTTCONN=0,\"%s\",%u,1", host, (unsigned)port);
	if (!ESP32_WriteCmd(cmd, 10000)) return false;
	mqtt_link_up = 1;
	return true;
}

bool ESP32_MqttSub(const char *topic)
{
	char cmd[192];
	snprintf(cmd, sizeof(cmd), "AT+MQTTSUB=0,\"%s\",0", topic);
	return ESP32_WriteCmd(cmd, 5000);
}

bool ESP32_MqttPub(const char *topic, const char *data)
{
	char cmd[384];
	snprintf(cmd, sizeof(cmd), "AT+MQTTPUB=0,\"%s\",\"%s\",1,0", topic, data);
	return ESP32_WriteCmd(cmd, 5000);
}

const char *ESP32_MqttMsg(void)
{
	return mqtt_frame;
}

uint8_t ESP32_MqttLinkUp(void)
{
	return mqtt_link_up;
}