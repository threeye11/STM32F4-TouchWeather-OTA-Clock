/* ===========================================================================
 * OTA 标志区模块 —— W25Q64 Zone3（0x200000，一个 4KB 扇区）
 * 集中存放升级流程中所有需要掉电保持的标志/版本/进度
 *
 * 存储策略 R-M-E-W（读-改-擦-写）：
 *   NOR Flash 最小擦除单位 4KB，不支持字节覆盖写。本模块把全部字段集中在
 *   一个 RAM 结构体里：修改字段后调 OTA_Flags_Save() 整体写回——
 *   1.读整个 4KB 扇区进缓冲 2.在缓冲中改目标字段 3.擦扇区 4.缓冲整体写回
 *   保证同扇区内其他字段完整保留。
 * =========================================================================== */

#include "ota_flags.h"

#define FLAGS_ADDR    (W25Q_ZONE3_BASE)     /* 标志区基址：Zone3 首个 4KB 扇区 */
#define FLAGS_LEN     (W25Q_SECTOR_SIZE)

static uint8_t sector_buf[FLAGS_LEN];       /* R-M-E-W 的 4KB 扇区缓冲（静态，不占栈） */

/* Zone3 -> RAM。首次上电（全 0xFF = 出厂空片）自动格式化成全 0 */
void OTA_Flags_Load(ota_flags_t *f)
{
	W25Q_Read(FLAGS_ADDR, (uint8_t *)f, sizeof(ota_flags_t));

	if (f->upgrade_flag == 0xFFFFFFFFu && f->protect_flag == 0xFFFFFFFFu)
	{
		OTA_Flags_Format();
		W25Q_Read(FLAGS_ADDR, (uint8_t *)f, sizeof(ota_flags_t));
	}
}

/* RAM -> Zone3：R-M-E-W 四步，保证同扇区内其他字节不丢 */
void OTA_Flags_Save(const ota_flags_t *f)
{
	uint32_t off, left;

	W25Q_Read(FLAGS_ADDR, sector_buf, FLAGS_LEN);                      /* 读 */
	for (off = 0; off < sizeof(ota_flags_t); off++)
		sector_buf[off] = ((const uint8_t *)f)[off];                   	/* 改 */
	W25Q_SectorErase(FLAGS_ADDR);                                      /* 擦 */
	left = sizeof(ota_flags_t); off = 0;                               /* 写 */
	while (left)
	{
		uint16_t chunk = (left > W25Q_PAGE_SIZE) ? W25Q_PAGE_SIZE : (uint16_t)left;
		W25Q_PageProgram(FLAGS_ADDR + off, &sector_buf[off], chunk);
		off += chunk;
		left -= chunk;
	}
}

void OTA_Flags_Format(void)
{
	uint32_t off, left;

	for (off = 0; off < FLAGS_LEN; off++) sector_buf[off] = 0;
	W25Q_SectorErase(FLAGS_ADDR);
	left = FLAGS_LEN; off = 0;
	while (left)
	{
		uint16_t chunk = (left > W25Q_PAGE_SIZE) ? W25Q_PAGE_SIZE : (uint16_t)left;
		W25Q_PageProgram(FLAGS_ADDR + off, &sector_buf[off], chunk);
		off += chunk;
		left -= chunk;
	}
}
