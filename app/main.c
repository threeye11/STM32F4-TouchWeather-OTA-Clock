#include "board.h"
#include "bsp_uart.h"
#include "lcd.h"
#include "ft6336.h"
#include "key.h"
#include "tp_cal.h"
#include "dht11.h"
#include "rtc.h"
#include "battery.h"
#include "w25q64.h"
#include "ota_selftest.h"
#include "ota_startup.h"
#include "esp32.h"
#include "mytasks.h"
#include "FreeRTOS.h"
#include "task.h"

int main(void)
{
	/* FreeRTOS 要求 4 位优先级全部作抢占位：不设分组时 NVIC_Init 算出的优先级全错，
	 * USART2 的 ISR（调用 FromISR API）实际会跑到优先级 0，触发 configASSERT 关中断死机 */
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_4);
	board_init();
	/* 向量表重定位必须在 board_init 之后：board.c 里硬编码了 VTOR=0x08000000，
	 * 若在它之前设置会被覆盖，导致中断从 Boot 向量表取向量而死机（实测白屏）- */
	SCB->VTOR = 0x08008000u;
	uart1_init(115200U);
	LCD_init();
	Key_init();
	Dht11_Init();
	MyRTC_Init();
	Battery_Init();
	W25Q_Init();		
	OTA_AppStartup();
	OTA_SelfTest();	

	AppTasks_Create();

	//启动调度器
	vTaskStartScheduler();
	while (1) {}
}
