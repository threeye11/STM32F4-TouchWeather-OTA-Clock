#ifndef __BATTERY_H
#define __BATTERY_H

#include "stm32f4xx.h"
#include <stdint.h>


void Battery_Init(void);        /* ADC1 通道1（PA1），单次转换 */
uint16_t Battery_GetVoltage_mV(void);
uint8_t Battery_GetPercent(void);


#endif


