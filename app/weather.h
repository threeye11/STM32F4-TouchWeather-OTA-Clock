#ifndef __WEATHER_H__
#define __WEATHER_H__

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
	char city[32];
	char location[64];
	char weather[16];
	int  weather_code;      
	int  temp_int;          //温度整数部分
	uint8_t temp_frac;      //温度 1 位小数
}weather_info;

bool Parse_SeniverseResponse(const char *response , weather_info *info);

#endif 


