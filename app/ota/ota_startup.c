/* APP 启动确认逻辑（在 main() 里、调度器启动前调用）：
 *   - 检测到 COMMITTED：把 Zone1 备份到 Zone2 + 写 protect=OK + 清标志
 *   - 上电时按住 PE7（KEY）：模拟一次完整 OTA（当前固件 -> Zone1 -> READY -> 复位）
 *     用于无网络环境验证 Boot 搬运/确认/回滚链路 */

#include "ota_startup.h"
#include "ota_flags.h"
#include "w25q64.h"
#include <board.h>
 
/* APP 链接基址（与 Boot 工程 FLASH_APP_ADDR 保持一致，勿改） */
#define FLASH_APP_ADDR   0x08008000u
#define FLASH_APP_MAXSZ  0x00078000u
#include "bsp_uart.h"
#include <stdio.h>
#include <string.h>

#define SIM_NEW_VER  "V9.9.9"

/* 内部 Flash 读一页（Flash 可按地址直接读，拷贝用） */
static void flash_read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	memcpy(buf, (void *)addr, len);
}

/* 擦除外部 Flash 的 [base, base+size)：64K 对齐处用块擦，其余用 4K 扇区补齐。
 * 注意不能只擦"1 个尾部扇区"——旧数据残留会让 NOR 写入变成新旧按位与（实测踩坑） */
static void w25_erase_range(uint32_t base, uint32_t size)
{
	W25Q_EraseRange(base, size);   /* 驱动统一实现：64K 块 + 4K 补齐 */
}

/* Zone1 -> Zone2 备份（确认阶段调用一次） */
static void backup_zone1_to_zone2(ota_flags_t *f)
{
	uint8_t  buf[256];
	uint32_t off, size = f->fw_size;
	uint32_t addr_w = W25Q_ZONE2_BASE;

	printf("[OTA] backup Zone1 -> Zone2 (%u bytes)\r\n", (unsigned int)size);

	/* 64KB 块擦整备份区，再 4KB 扇区擦尾部 */
	w25_erase_range(addr_w, size);   /* 先把备份区擦干净（含尾部） */
	for (off = 0; off < size; off += sizeof(buf))
	{
		uint16_t chunk = (uint16_t)((size - off >= sizeof(buf)) ? sizeof(buf) : (size - off));
		flash_read(FLASH_APP_ADDR + off, buf, chunk);          /* 从内部 Flash 当前固件读 */
		W25Q_PageProgram(addr_w + off, buf, chunk);
	}
	strncpy(f->backup_ver, f->new_ver, sizeof(f->backup_ver) - 1);
	f->backup_fw_size = size;
}

/* 页扫描求当前固件实际长度：找第一个整页全 0xFF 的页边界 */
static uint32_t detect_fw_size(void)
{
	uint8_t  buf[256];
	uint32_t off;
	uint8_t  allff;

	for (off = 0; off < FLASH_APP_MAXSZ; off += sizeof(buf))
	{
		flash_read(FLASH_APP_ADDR + off, buf, sizeof(buf));
		allff = 1;
		for (int i = 0; i < 256; i++)
			if (buf[i] != 0xFFu) { allff = 0; break; }
		if (allff) break;
	}
	return off;
}

/* 模拟升级：当前运行固件（内部 Flash）拷贝到 Zone1，置 READY 后复位 */
static void simulate_upgrade(void)
{
	ota_flags_t f;
	uint8_t     buf[256];
	uint32_t    off, size;

	size = detect_fw_size();
	if (size < 4096u) { printf("[SIM] fw size detect fail\r\n"); return; }
	printf("[SIM] fw size=%u, copy to Zone1...\r\n", (unsigned int)size);

	w25_erase_range(W25Q_ZONE1_BASE, size);   /* Zone1 全量擦净（含尾部） */
	for (off = 0; off < size; off += sizeof(buf))
	{
		uint16_t chunk = (uint16_t)((size - off >= sizeof(buf)) ? sizeof(buf) : (size - off));
		flash_read(FLASH_APP_ADDR + off, buf, chunk);
		W25Q_PageProgram(W25Q_ZONE1_BASE + off, buf, chunk);
	}

	memset(&f, 0, sizeof(f));
	strcpy(f.current_ver, "V1.0.0");
	strcpy(f.new_ver, SIM_NEW_VER);
	f.fw_size        = size;
	f.upgrade_flag   = UPGRADE_MAGIC_READY;
	f.protect_flag   = PROTECT_MAGIC_NONE;
	OTA_Flags_Save(&f);

	printf("[SIM] READY set (V1.0.0 -> %s), reset to Boot...\r\n", SIM_NEW_VER);
	delay_ms(200);
	NVIC_SystemReset();
	while (1) ;
}

void OTA_AppStartup(void)
{
	ota_flags_t f;
	GPIO_InitTypeDef g;
	uint8_t pressed;

	OTA_Flags_Load(&f);

	/* ---- 分支1：Boot 已搬运完成，等 APP 确认 ---- */
	if (f.upgrade_flag == UPGRADE_MAGIC_COMMITTED)
	{
		printf("[OTA] detect COMMITTED (new=%s)\r\n", f.new_ver);
		backup_zone1_to_zone2(&f);
		f.protect_flag  = PROTECT_MAGIC_OK;
		f.upgrade_flag  = UPGRADE_MAGIC_NONE;
		f.boot_fail_cnt = 0;
		OTA_Flags_Save(&f);
		printf("[OTA] committed & backed up -> boot normally\r\n");
	}

	/* ---- 分支2：按住 PE7 上电 -> 模拟 OTA 升级（无网络测试用） ---- */
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	g.GPIO_Pin  = GPIO_Pin_7;
	g.GPIO_Mode = GPIO_Mode_IN;
	g.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_Init(GPIOE, &g);
	delay_ms(100);                                   /* 消抖窗口 */
	pressed = (GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_7) == Bit_RESET);
	if (pressed)
	{
		printf("\r\n*** PE7 held: simulate OTA upgrade ***\r\n");
		simulate_upgrade();
	}
}
