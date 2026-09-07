#ifndef __KEY_H
#define __KEY_H

#include "stm32f4xx.h"

/* PE7 按键：按下接 GND（低电平有效），内部上拉输入。
   外部中断（EXTI7 下降沿）通过任务通知唤醒 UI 任务。
   GetKeynum() 在任务中调用，返回通知值（bit0=1 表示有按键）。 */

void Key_init(void);
uint32_t GetKeynum(void);   /* 非阻塞，返回通知值；0 = 无事件，bit0=1 = 按键按下 */

#endif
