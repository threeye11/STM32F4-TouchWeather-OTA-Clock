#ifndef __CTPIIC_H
#define __CTPIIC_H

#include "stm32f4xx.h"
#include <stdint.h>


void I2C1_init(void);
uint8_t I2C1_WriteReg(uint8_t reg, uint8_t *buf, uint8_t len);
uint8_t I2C1_ReadReg(uint8_t reg, uint8_t *buf, uint8_t len);

#endif
