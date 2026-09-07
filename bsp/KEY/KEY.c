#include "key.h"
#include "board.h"
#include "FreeRTOS.h"
#include "task.h"

/* UI 任务句柄在 main.c 中创建 */
extern TaskHandle_t xTaskUI_Handle;

/*
 * PE7 按键：按下接 GND（低电平有效），内部上拉输入
 * 外部中断（EXTI7 下降沿）通过 xTaskNotifyFromISR 通知 UI 任务
 * 消抖在任务级完成（vTaskDelay 20ms），ISR 中不做延时
 */

void Key_init(void)
{
	GPIO_InitTypeDef  GPIO_InitStructure;
	EXTI_InitTypeDef  EXTI_InitStructure;
	NVIC_InitTypeDef  NVIC_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SYSCFG, ENABLE);

	GPIO_InitStructure.GPIO_Pin   = GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_Init(GPIOE, &GPIO_InitStructure);

	SYSCFG_EXTILineConfig(EXTI_PortSourceGPIOE, EXTI_PinSource7);

	EXTI_InitStructure.EXTI_Line    = EXTI_Line7;
	EXTI_InitStructure.EXTI_Mode    = EXTI_Mode_Interrupt;
	EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Falling;
	EXTI_InitStructure.EXTI_LineCmd = ENABLE;
	EXTI_Init(&EXTI_InitStructure);

	/*
	 * 中断优先级 = 5（>= configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY）
	 * 这样 ISR 中才能调用 xTaskNotifyFromISR
	 * 如果优先级 < 5（如原来的 0），调用 FreeRTOS API 会导致 HardFault
	 */
	NVIC_InitStructure.NVIC_IRQChannel                   = EXTI9_5_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 5;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority        = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd                = ENABLE;
	NVIC_Init(&NVIC_InitStructure);
}

/*
 * 消费一次按键事件（非阻塞）
 * 返回: 通知值（bit0 = 1 表示有按键按下），0 表示无事件
 *
 * 调用方式:
 *   if (GetKeynum() & 0x01) { ... 处理按键 ... }
 */
uint32_t GetKeynum(void)
{
	uint32_t notify_val = 0;
	/* 非阻塞检查（timeout=0），清除已读位 */
	xTaskNotifyWait(0, 0xFFFFFFFF, &notify_val, 0);
	return notify_val;
}

/*
 * EXTI9_5 中断服务函数（Line7 = PE7 按键）
 *
 * 不做消抖！只负责通知 UI 任务"按键发生了"
 * 消抖在 UI 任务中通过 vTaskDelay(20) 完成
 *
 * xTaskNotifyFromISR 的 eSetBits 模式：
 * 多次快速按压会将通知值按位或，UI 任务只需检查 bit0
 */
void EXTI9_5_IRQHandler(void)
{
	if (EXTI_GetITStatus(EXTI_Line7) != RESET)
	{
		if (xTaskUI_Handle != NULL)
		{
			BaseType_t xHigherPriorityTaskWoken = pdFALSE;
			xTaskNotifyFromISR(xTaskUI_Handle,
			                   0x01,            /* 通知值：bit0 = 按键事件 */
			                   eSetBits,        /* 按位或，不覆盖之前的值 */
			                   &xHigherPriorityTaskWoken);
			portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
		}
		EXTI_ClearITPendingBit(EXTI_Line7);
	}
}
