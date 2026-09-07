#ifndef __TP_CAL_H
#define __TP_CAL_H

#include "stm32f4xx.h"

/* 4 点触摸校准：屏幕四角依次点击十字准星，
   算出"原始 12 位坐标 → 屏幕坐标"的线性系数（偏移+缩放）。
   系数只存在内存里，掉电丢失（如需保存可后续加 Flash/EEPROM）。 */

void TP_Cal_Run(void);                                       /* 阻塞式校准流程 */
void TP_Cal_Convert(u16 raw_x, u16 raw_y, u16 *sx, u16 *sy); /* 按校准系数转屏幕坐标（自动限幅） */

#endif
