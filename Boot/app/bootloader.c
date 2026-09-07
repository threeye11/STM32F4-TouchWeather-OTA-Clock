#include "bootloader.h"
#include "ota_flags.h"
#include "w25q64.h"
#include "flash_if.h"
#include <stdio.h>
#include <string.h>

typedef void (*pFunction)(void);

#define ZONE1_BASE  W25Q_ZONE1_BASE
#define ZONE2_BASE  W25Q_ZONE2_BASE

/* ---------------- 头部校验 ---------------- */

/* 内部 Flash 镜像：字0=栈顶(RAM)，字1=复位向量(0x08xxxxxx, Thumb) */
uint8_t Boot_CheckAppValid(uint32_t addr)
{
	uint32_t sp = *(volatile uint32_t *)addr;
	uint32_t pc = *(volatile uint32_t *)(addr + 4);
	return ((sp & 0xFF000000u) == 0x20000000u || (sp & 0xFF000000u) == 0x10000000u) &&
	       ((pc & 0xFFF00000u) == 0x08000000u);
}

/* W25Q 固件头：读 8 字节判断（SP 指向 RAM + PC 指向内部 Flash APP 区） */
uint8_t Boot_CheckW25ImgValid(uint32_t base)
{
	uint8_t  h[8];
	uint32_t sp, pc;

	W25Q_Read(base, h, 8);
	sp = (uint32_t)h[0] | (uint32_t)h[1] << 8 | (uint32_t)h[2] << 16 | (uint32_t)h[3] << 24;
	pc = (uint32_t)h[4] | (uint32_t)h[5] << 8 | (uint32_t)h[6] << 16 | (uint32_t)h[7] << 24;
	return ((sp & 0xFF000000u) == 0x20000000u || (sp & 0xFF000000u) == 0x10000000u) &&
	       ((pc & 0xFFF00000u) == 0x08000000u);
}

/* ---------------- 跳转 ---------------- */

static void Jump_To_App(uint32_t addr)
{
	pFunction app_reset;
	uint32_t i;
	uint32_t sp = *(volatile uint32_t *)addr;

	app_reset = (pFunction)(*(volatile uint32_t *)(addr + 4));

	__disable_irq();
	SysTick->CTRL = 0;
	SysTick->LOAD = 0;
	SysTick->VAL  = 0;
	for (i = 0; i < 8; i++)
	{
		NVIC->ICER[i] = 0xFFFFFFFFu;					// 失能所有外设中断
		NVIC->ICPR[i] = 0xFFFFFFFFu;					// 清除所有中断的挂起标志		
	}
	SCB->VTOR = addr;												// 设置中断向量表偏移地址
	__set_MSP(sp);													//将提取的 sp 值写入 CPU 的 SP（主堆栈指针）
	__enable_irq();
	app_reset();														//函数指针调用 —— 强制 PC 跳转到 APP 的复位入口
}

/* ---------------- 搬运 / 回滚 ---------------- */

static uint8_t copy_w25_to_flash(uint32_t w_base, uint32_t size)
{
	uint8_t  buf[256];
	uint32_t off;

	for (off = 0; off < size; off += sizeof(buf))
	{
		uint16_t chunk = (uint16_t)((size - off >= sizeof(buf)) ? sizeof(buf) : (size - off));
		W25Q_Read(w_base + off, buf, chunk);
		if (Flash_WriteBuf(FLASH_APP_ADDR + off, buf, chunk)) return 1;
		/* 逐块回读比对：源数据损坏或编程异常当场拦截（头 256B 校验覆盖不到中部） */
		if (memcmp(buf, (void *)(FLASH_APP_ADDR + off), chunk) != 0) return 2;
		if ((off & 0x0FFFFu) == 0) printf(".");      /* 每 64KB 一个进度点 */
	}
	return 0;
}

/* 升级失败兜底：清标志 -> APP 仍有效则直接跳回；
 * APP 已被擦掉则尝试 Zone2 恢复；两者皆无则停机等 SWD 重烧（绝不变砖也绝不盲跳） */
static void Fail_Safe(const char *why)
{
	ota_flags_t f;

	OTA_Flags_Load(&f);
	f.upgrade_flag  = UPGRADE_MAGIC_NONE;
	f.boot_fail_cnt = 0;
	OTA_Flags_Save(&f);
	printf("[BOOT] %s\r\n", why);

	if (Boot_CheckAppValid(FLASH_APP_ADDR))
	{
		Jump_To_App(FLASH_APP_ADDR);
	}

	if (f.backup_fw_size > 0 && f.backup_fw_size <= FLASH_APP_MAXSZ &&
	    Boot_CheckW25ImgValid(ZONE2_BASE))
	{
		printf("[BOOT] recover from Zone2 (%s)...\r\n", f.backup_ver);
		__disable_irq();
		Flash_EraseAppArea(f.backup_fw_size);
		copy_w25_to_flash(ZONE2_BASE, f.backup_fw_size);
		__enable_irq();
		printf("[BOOT] recovered, reset\r\n");
		NVIC_SystemReset();
	}

	printf("[BOOT] no valid APP anywhere, halt! (reflash APP via SWD)\r\n");
	while (1) ;
}

/* READY：搬运 Zone1 -> APP 区，写 COMMITTED 后复位进新 APP */
static void DoUpgrade(void)
{
	ota_flags_t f;
	static uint8_t hdr[256];   /* static：Boot 主栈仅 1KB，大缓冲不进栈 */

	OTA_Flags_Load(&f);
	printf("[BOOT] READY: ver=%s size=%u\r\n", f.new_ver, (unsigned int)f.fw_size);

	if (f.fw_size == 0 || f.fw_size > FLASH_APP_MAXSZ || !Boot_CheckW25ImgValid(ZONE1_BASE))
	{
		printf("[BOOT] Zone1 invalid, clear flag & keep old app\r\n");
		f.upgrade_flag = UPGRADE_MAGIC_NONE;
		f.boot_fail_cnt = 0;
		OTA_Flags_Save(&f);
		Jump_To_App(FLASH_APP_ADDR);
		return;
	}

	__disable_irq();                      /* 擦/写期间 CPU 会因同 bank 访问停摆，关中断最干净 */
	printf("[BOOT] erase APP area...\r\n");
	if (Flash_EraseAppArea(f.fw_size))
	{
		__enable_irq();
		Fail_Safe("erase FAIL");
		return;
	}
	printf("[BOOT] copying Zone1 -> flash:\r\n");
	if (copy_w25_to_flash(ZONE1_BASE, f.fw_size))
	{
		__enable_irq();
		Fail_Safe("copy FAIL");
		return;
	}
	printf(" done\r\n");

	/* 回读抽查：比对首 256B（Zone1 vs 内部 Flash） */
	W25Q_Read(ZONE1_BASE, hdr, 256);
	if (memcmp(hdr, (void *)FLASH_APP_ADDR, 256) != 0)
	{
		__enable_irq();
		Fail_Safe("verify FAIL: flash != Zone1");
		return;
	}

	strncpy(f.current_ver, f.new_ver, sizeof(f.current_ver) - 1);
	f.upgrade_flag   = UPGRADE_MAGIC_COMMITTED;   /* 等 APP 确认 */
	f.protect_flag   = PROTECT_MAGIC_NONE;
	f.boot_fail_cnt  = 0;
	OTA_Flags_Save(&f);
	printf("[BOOT] committed, reset to new APP\r\n");
	//执行硬件复位
	NVIC_SystemReset();
	while (1) ;
}

/* COMMITTED 且 APP 未确认：失败计数，超 3 次从 Zone2 回滚 */
static void DoCheckConfirm(void)
{
	ota_flags_t f;

	OTA_Flags_Load(&f);
	if (f.protect_flag == PROTECT_MAGIC_OK)          /* APP 已确认 */
	{
		printf("[BOOT] app confirmed, clear flags\r\n");
		f.upgrade_flag  = UPGRADE_MAGIC_NONE;
		f.protect_flag  = PROTECT_MAGIC_NONE;
		f.boot_fail_cnt = 0;
		OTA_Flags_Save(&f);
		Jump_To_App(FLASH_APP_ADDR);
		return;
	}

	f.boot_fail_cnt++;
	OTA_Flags_Save(&f);
	printf("[BOOT] app NOT confirmed, fail=%u\r\n", (unsigned int)f.boot_fail_cnt);

	if (f.boot_fail_cnt >= 3 && f.backup_fw_size > 0 &&
	    f.backup_fw_size <= FLASH_APP_MAXSZ && Boot_CheckW25ImgValid(ZONE2_BASE))
	{
		printf("[BOOT] rollback from Zone2 (%s)...\r\n", f.backup_ver);
		__disable_irq();
		if (!Flash_EraseAppArea(f.backup_fw_size))
		{
			copy_w25_to_flash(ZONE2_BASE, f.backup_fw_size);
			strncpy(f.current_ver, f.backup_ver, sizeof(f.current_ver) - 1);
			f.upgrade_flag  = UPGRADE_MAGIC_NONE;
			f.protect_flag  = PROTECT_MAGIC_NONE;
			f.boot_fail_cnt = 0;
			OTA_Flags_Save(&f);
			printf(" rollback done, reset\r\n");
			NVIC_SystemReset();
		}
		__enable_irq();
	}
	Jump_To_App(FLASH_APP_ADDR);        /* 未达阈值：继续让 APP 尝试 */
}

/* ---------------- 主状态机 ---------------- */

void BootLoader_Run(void)
{
	ota_flags_t f;

	W25Q_Init();
	OTA_Flags_Load(&f);
	printf("[BOOT] upgrade=0x%08X protect=0x%08X cur=%s fail=%u\r\n",
	       (unsigned int)f.upgrade_flag, (unsigned int)f.protect_flag,
	       f.current_ver, (unsigned int)f.boot_fail_cnt);

	switch (f.upgrade_flag)
	{
	case UPGRADE_MAGIC_READY:
		DoUpgrade();
		break;

	case UPGRADE_MAGIC_COMMITTED:
		DoCheckConfirm();
		break;

	default:
		break;
	}

	if (Boot_CheckAppValid(FLASH_APP_ADDR))
	{
		printf("[BOOT] jumping to APP @0x%08X\r\n", FLASH_APP_ADDR);
		Jump_To_App(FLASH_APP_ADDR);
	}

	printf("[BOOT] APP image invalid @0x%08X, halt!\r\n", FLASH_APP_ADDR);
	while (1) ;
}
