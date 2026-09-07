#ifndef __RTC_H
#define __RTC_H

#include "stm32f4xx.h"

void MyRTC_Init(void);
void MyRTC_ReadTime(RTC_TimeTypeDef *t , RTC_DateTypeDef *d);
void MyRTC_SetTime(const RTC_TimeTypeDef *t , const RTC_DateTypeDef *d);

#endif
