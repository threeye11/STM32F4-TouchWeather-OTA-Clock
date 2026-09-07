#include <board.h>
#include "FreeRTOS.h"
#include "task.h"
#include "lvgl.h"

void board_init(void)
{
    /* NVIC Configuration */
#define NVIC_VTOR_MASK              0x3FFFFF80
#ifdef  VECT_TAB_RAM
    SCB->VTOR  = (0x10000000 & NVIC_VTOR_MASK);
#else  /* VECT_TAB_FLASH  */
    SCB->VTOR  = (0x08000000 & NVIC_VTOR_MASK);
#endif

    /* 启用 DWT 周期计数器（CYCCNT）用于微秒延时 */
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    DWT->CYCCNT = 0;
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;
}

/*
 * DWT 微秒延时：基于 DWT->CYCCNT 周期计数器
 * 不依赖 SysTick，与 FreeRTOS 无冲突
 */
void delay_us(uint32_t _us)
{
    uint32_t start = DWT->CYCCNT;
    uint32_t ticks = _us * (SystemCoreClock / 1000000);
    while ((DWT->CYCCNT - start) < ticks);
}

/*
 * 毫秒延时：调度器运行中用 vTaskDelay，否则用 DWT 忙等
 */
void delay_ms(uint32_t _ms)
{
    if (xTaskGetSchedulerState() == taskSCHEDULER_RUNNING)
        vTaskDelay(_ms);
    else
        delay_us(_ms * 1000);
}

void delay_1ms(uint32_t ms) { delay_us(ms * 1000); }
void delay_1us(uint32_t us) { delay_us(us); }

/* LVGL 时基：SysTick 每 1ms 调用 lv_tick_inc(1)（任务里不要再调，避免双倍计时） */
void vApplicationTickHook(void)
{
    lv_tick_inc(1);
}
