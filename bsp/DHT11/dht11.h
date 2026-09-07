#ifndef __DHT11_H
#define __DHT11_H

#include <stdbool.h>
#include <stdint.h>

#define DHT11_PORT GPIOE
#define DHT11_PIN    GPIO_Pin_2

#define DHT11_DQ_H()   GPIO_SetBits(DHT11_PORT, DHT11_PIN)
#define DHT11_DQ_L()   GPIO_ResetBits(DHT11_PORT, DHT11_PIN)
#define DHT11_DQ_IN()  (GPIO_ReadInputDataBit(DHT11_PORT, DHT11_PIN) != RESET)

typedef struct {
	uint8_t humi_int;
	uint8_t humi_dec;  //湿度小数
	uint8_t temp_int;
	uint8_t temp_dec;  //温度小数
}dht11_data_t;

void Dht11_Init(void);
bool DHT11_Read(dht11_data_t *out);

#endif
