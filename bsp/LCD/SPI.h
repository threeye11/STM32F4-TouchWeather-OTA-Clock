#ifndef _SPI_H_
#define _SPI_H_

#include "stm32f4xx.h"
#include <board.h>
#include <stdio.h>
#include <stdbool.h>
#include "bsp_uart.h" 
//除了SPI时钟信号以及SPI读、写信号引脚不可更改，其他引脚都可以更改
 
void SPI_DMA_Init(void);
void DMA2_Stream3_IRQHandler(void);
uint8_t SPI_DMA_SendBytes(const uint8_t *data, uint32_t len);
void SPI_DMA_SendBytes_Wait(const uint8_t *data, uint32_t len);
uint8_t SPI_DMA_IsIdle(void);
void SPI1_init(void);
void SPI_SendByte(uint8_t Byte);
uint8_t SPI_ReadByte(void);



#endif

