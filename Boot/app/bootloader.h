#ifndef __BOOTLOADER_H
#define __BOOTLOADER_H

#include "stm32f4xx.h"

/* APP 固件起始地址（Boot 占 Sector0-1 共 32KB，APP 从 Sector2 开始） */
#define FLASH_APP_ADDR   0x08008000u
#define FLASH_APP_MAXSZ  0x00078000u   /* APP 区上限 480KB（Sector2~7） */

void    BootLoader_Run(void);              /* 主状态机（v2：搬运/确认/回滚） */
uint8_t Boot_CheckAppValid(uint32_t addr); /* 校验内部 Flash 镜像头部 */
uint8_t Boot_CheckW25ImgValid(uint32_t base); /* 校验 W25Q 固件头部 */

#endif /* __BOOTLOADER_H */
