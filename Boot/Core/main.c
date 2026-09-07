/* ===========================================================================
 * BootLoader 上电入口（v1：串口横幅 + 标志区打印 + 跳转 APP）
 * 时钟：本工程自带 system_stm32f4xx.c（VECT_TAB_OFFSET=0x0），
 *       SystemInit -> SetSysClock 已配 168MHz，SPI2/APB1=42MHz
 * 链接：IROM 0x08000000 / 32KB（Sector0-1），不使用 RTOS
 * =========================================================================== */
#include "stm32f4xx.h"
#include "bootloader.h"
#include "w25q64.h"
#include <stdio.h>

static void boot_uart1_init(void)
{
	GPIO_InitTypeDef g;
	USART_InitTypeDef u;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource9, GPIO_AF_USART1);
	g.GPIO_Pin   = GPIO_Pin_9;
	g.GPIO_Mode  = GPIO_Mode_AF;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_PuPd  = GPIO_PuPd_UP;
	g.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &g);

	u.USART_BaudRate            = 115200;
	u.USART_WordLength          = USART_WordLength_8b;
	u.USART_StopBits            = USART_StopBits_1;
	u.USART_Parity              = USART_Parity_No;
	u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	u.USART_Mode                = USART_Mode_Tx;
	USART_Init(USART1, &u);
	USART_Cmd(USART1, ENABLE);
}

/* MicroLIB printf 重定向：轮询发送 */
int fputc(int ch, FILE *f)
{
	(void)f;
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) ;
	USART_SendData(USART1, (uint8_t)ch);
	return ch;
}

int main(void)
{
	boot_uart1_init();
	printf("\r\n===== BootLoader v1 (F407VET6, APP@0x08008000) =====\r\n");

	BootLoader_Run();      /* 不返回：有效则跳 APP；无效则停在死循环 */

	while (1) ;
}
