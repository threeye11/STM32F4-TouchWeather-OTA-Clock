#ifndef __FLASH_IF_H
#define __FLASH_IF_H

#include "stm32f4xx.h"
#include <stdint.h>

uint8_t Flash_EraseAppArea(uint32_t size);                    /* 从 0x08008000 起擦 size 字节，0=成功 */
uint8_t Flash_WriteBuf(uint32_t addr, const uint8_t *buf, uint32_t len); /* 页写（内部 Flash，字粒度） */

#endif /* __FLASH_IF_H */
