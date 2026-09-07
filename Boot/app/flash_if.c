#include "flash_if.h"
#include "bootloader.h"
#include <stdio.h>

/* F407VET6（512KB）扇区表：Sector0-3=16KB，Sector4=64KB，Sector5-7=128KB */
static uint8_t get_sector_num(uint32_t addr)
{
	if (addr < 0x08004000u) return 0;
	if (addr < 0x08008000u) return 1;
	if (addr < 0x0800C000u) return 2;
	if (addr < 0x08010000u) return 3;
	if (addr < 0x08020000u) return 4;
	if (addr < 0x08040000u) return 5;
	if (addr < 0x08060000u) return 6;
	return 7;
}

static uint32_t get_sector_size(uint8_t n)
{
	static const uint32_t sz[8] = {16, 16, 16, 16, 64, 128, 128, 128}; /* KB */
	return sz[n] * 1024u;
}

/* 从 FLASH_APP_ADDR 起擦除 size 字节（Sector2 开始）。0=成功 1=失败 */
uint8_t Flash_EraseAppArea(uint32_t size)
{
	uint32_t addr = FLASH_APP_ADDR;
	uint8_t  sec;

	FLASH_Unlock();
	//清除错误标志
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
	while (size > 0 && addr < 0x08080000u)
	{
		sec = get_sector_num(addr);
		//擦除该扇区
		/* StdPeriph 扇区号是 N<<3 编码（SN[2:0] 在 CR[5:3]）：传原始 0-7 会错擦别的扇区（实测错擦 Sector0 把 Boot 自己擦掉 -> 静默锁死） */
		if (FLASH_EraseSector(((uint32_t)sec << 3), VoltageRange_3) != FLASH_COMPLETE)
		{
			FLASH_Lock();
			return 1;
		}
		printf(".");
		// 地址前进到下一扇区起始
		addr += get_sector_size(sec);
		size  = (size > get_sector_size(sec)) ? (size - get_sector_size(sec)) : 0;
	}
	FLASH_Lock();
	return 0;
}

/* 连续写 len 字节到内部 Flash。0=成功 1=失败 */
uint8_t Flash_WriteBuf(uint32_t addr, const uint8_t *buf, uint32_t len)
{
	uint32_t i;

	FLASH_Unlock();
	FLASH_ClearFlag(FLASH_FLAG_EOP | FLASH_FLAG_OPERR | FLASH_FLAG_WRPERR |
	                FLASH_FLAG_PGAERR | FLASH_FLAG_PGPERR | FLASH_FLAG_PGSERR);
	//F4 标准库FLASH_ProgramWord()只能一次写入完整 32 位字 (4 字节)
	for (i = 0; i < len; i += 4)
	{
		uint32_t w = 0;   /* 必须从 0 组装：从 0xFF 起 OR 恒为 0xFF，等于什么都没写 */
		//处理固件长度不是 4 的整数倍的尾巴
		uint8_t  j, n = (uint8_t)((len - i >= 4) ? 4 : (len - i));
		/* 小端组装；尾部不足 4 字节以 0xFF 填充（NOR 擦除态，写入无副作用） */
		for (j = 0; j < 4; j++)
		{
			uint8_t b = (j < n) ? buf[i + j] : 0xFFu;
			w |= (uint32_t)b << (8 * j);
		}   /* 小端组装 */
		if (FLASH_ProgramWord(addr + i, w) != FLASH_COMPLETE)
		{
			FLASH_Lock();
			return 1;
		}
	}
	FLASH_Lock();
	return 0;
}
