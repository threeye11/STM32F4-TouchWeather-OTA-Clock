#ifndef __W25Q64_H
#define __W25Q64_H

#include "stm32f4xx.h"
#include <stdint.h>

/* ===========================================================================
 * W25Q64 (8MB SPI NOR Flash) 驱动 —— SPI2 硬件外设
 *   PB12 = CS（软件片选）  PB13 = SCK  PB14 = MISO  PB15 = MOSI（复用 AF5）
 * NOR Flash 特性（使用前必读）：
 *   1. 写前必须擦：擦除后全 0xFF，编程只能把 1 写成 0
 *   2. 页编程一次最多 256B，且不能跨页（页边界 = addr % 256）
 *   3. 最小擦除单位 4KB 扇区（SectorErase）；整区清理用 64KB BlockErase 更快
 *   4. 擦/写期间状态寄存器 BUSY(bit0)=1，操作完成后必须等待
 * 分区约定（docs/OTA远程升级开发文档.md §2.2）：
 *   Zone1 下载区 0x000000(1MB) / Zone2 备份区 0x100000(1MB)
 *   Zone3 标志区 0x200000(4KB) / 自检与保留  0x300000 起
 * =========================================================================== */

#define W25Q_ZONE1_BASE   0x000000u
#define W25Q_ZONE2_BASE   0x100000u
#define W25Q_ZONE3_BASE   0x200000u
#define W25Q_TEST_BASE    0x300000u
#define W25Q_SECTOR_SIZE  4096u
#define W25Q_PAGE_SIZE    256u
#define W25Q_TOTAL_SIZE   (8u * 1024u * 1024u)

void     W25Q_Init(void);                  /* SPI2 + GPIO 初始化 */
uint32_t W25Q_ReadID(void);                /* JEDEC ID，W25Q64 = 0xEF4017 */
void     W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len);
void     W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len); /* <=256B 不跨页 */
void     W25Q_SectorErase(uint32_t addr);  /* 4KB 扇区擦除 */
void     W25Q_BlockErase64K(uint32_t addr);/* 64KB 块擦除 */
void     W25Q_EraseRange(uint32_t base, uint32_t size);   /* [base,base+size)：64K 块 + 4K 补齐 */

#endif /* __W25Q64_H */
