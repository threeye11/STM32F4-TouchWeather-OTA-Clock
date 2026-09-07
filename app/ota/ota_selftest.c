#include "ota_selftest.h"
#include "w25q64.h"
#include "ota_flags.h"
#include "bsp_uart.h"
#include <string.h>
#include <stdio.h>

#define OTA_SELFTEST_EN 1

/* 上电自检流程：
 * JEDEC ID            期望 0xEF4017（W25Q64），不符说明接线/焊接有问题
 * NOR 特性演示        未擦直写 vs 擦-写-读回环（学习 NOR "写前必擦"）
 * 标志区 R-M-E-W      多字段保存重读，验证字段隔离与掉电保持 */
void OTA_SelfTest(void)
{
#if OTA_SELFTEST_EN
	uint32_t    id, i, fail = 0;
	uint8_t     wr[32], rd[32];
	ota_flags_t f0, f1, f2;

	printf("\r\n===== OTA Stage1 SelfTest =====\r\n");

	/* ① JEDEC ID */
	id = W25Q_ReadID();
	printf("[W25Q] JEDEC ID = 0x%06X %s (expect 0xEF4017)\r\n",
	       (unsigned int)id, (id == 0xEF4017u) ? "OK" : "FAIL -- check wiring!");
	if (id != 0xEF4017u)
		return;

	/* ② 未擦直写：NOR 只能 1->0，读回 = 旧内容 & 新内容，多半不是预期值 */
	for (i = 0; i < 32; i++) wr[i] = (uint8_t)('A' + i);
	W25Q_PageProgram(W25Q_TEST_BASE, wr, 32);
	memset(rd, 0, sizeof(rd));
	W25Q_Read(W25Q_TEST_BASE, rd, 32);
	printf("[W25Q] raw-write w/o erase: rd[0]=0x%02X (NOR demo)\r\n", rd[0]);

	/* 先擦后写：擦完应全 0xFF，写完回环比对应完全一致 */
	W25Q_SectorErase(W25Q_TEST_BASE);
	memset(rd, 0xFF, sizeof(rd));
	W25Q_Read(W25Q_TEST_BASE, rd, 32);
	printf("[W25Q] after erase: rd[0]=0x%02X (expect 0xFF)\r\n", rd[0]);

	W25Q_PageProgram(W25Q_TEST_BASE, wr, 32);
	memset(rd, 0, sizeof(rd));
	W25Q_Read(W25Q_TEST_BASE, rd, 32);
	for (i = 0; i < 32; i++)
		if (rd[i] != wr[i]) { fail = 1; break; }
	printf("[W25Q] erase-write-readback %s\r\n", fail ? "FAIL" : "PASS");

	/* ③ 标志区：多字段保存 -> 重读 -> 字段隔离检查 */
	OTA_Flags_Load(&f0);            /* 记住原值，测试结束后恢复（不干扰 OTA 状态机） */
	memset(&f1, 0, sizeof(f1));
	strcpy(f1.current_ver, "V1.0.0");
	strcpy(f1.new_ver,     "V1.0.1");
	f1.upgrade_flag    = UPGRADE_MAGIC_READY;
	f1.fw_size         = 436902u;
	f1.downloaded_size = 204800u;
	OTA_Flags_Save(&f1);

	OTA_Flags_Load(&f2);
	printf("[FLAG] ver=%s -> %s size=%u dl=%u flag=0x%08X\r\n",
	       f2.current_ver, f2.new_ver,
	       (unsigned int)f2.fw_size, (unsigned int)f2.downloaded_size,
	       (unsigned int)f2.upgrade_flag);
	if (f2.upgrade_flag == UPGRADE_MAGIC_READY &&
	    f2.fw_size == 436902u &&
	    f2.downloaded_size == 204800u &&
	    strcmp(f2.current_ver, "V1.0.0") == 0 &&
	    strcmp(f2.new_ver, "V1.0.1") == 0)
		printf("[FLAG] save/load + R-M-E-W: PASS\r\n");
	else
		printf("[FLAG] save/load + R-M-E-W: FAIL\r\n");

	OTA_Flags_Save(&f0);            /* 恢复原标志 */
	printf("===== OTA Stage1 SelfTest Done =====\r\n\r\n");
#endif
}
