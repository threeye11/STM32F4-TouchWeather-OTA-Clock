#ifndef __OTA_FLAGS_H
#define __OTA_FLAGS_H

#include "w25q64.h"
#include <stdint.h>



/* 升级状态机（upgrade_flag 的合法值） */
#define UPGRADE_MAGIC_NONE       0x00000000u  /* 空闲：无升级任务 */
#define UPGRADE_MAGIC_READY      0xAA55AA55u  /* Zone1 已存新固件，Boot 应搬运 */
#define UPGRADE_MAGIC_COMMITTED  0x55AA55AAu  /* 已搬入 APP 区，等 APP 确认 */

/* 保护标志（protect_flag 的合法值） */
#define PROTECT_MAGIC_NONE       0x00000000u  /* 新固件未确认（启动失败/掉电） */
#define PROTECT_MAGIC_OK         0xBB66BB66u  /* APP 已确认新固件运行正常 */

/* 字段偏移（与开发文档 §2.3 一致；保留字段为将来扩展占位） */
#define OFS_UPGRADE_FLAG   0x0000
#define OFS_PROTECT_FLAG   0x0004
#define OFS_CURRENT_VER    0x0010
#define OFS_NEW_VER        0x0030
#define OFS_BACKUP_VER     0x0050
#define OFS_FW_SIZE        0x0070
#define OFS_BOOT_FAIL_CNT  0x0080
#define OFS_BACKUP_FW_SIZE 0x0090
#define OFS_DOWNLOADED     0x0100
#define OFS_FILE_MD5       0x0240

typedef struct {
	uint32_t upgrade_flag;              /* 0x0000 升级状态机 */
	uint32_t protect_flag;              /* 0x0004 保护标志 */
	uint8_t  _rsv0[8];                  /* 0x0008 保留 */
	char     current_ver[24];           /* 0x0010 当前运行版本 "V1.0.0" */
	uint8_t  _rsv1[8];                  /* 0x0028 保留 */
	char     new_ver[24];               /* 0x0030 OTA 目标版本 */
	uint8_t  _rsv2[8];                  /* 0x0048 保留 */
	char     backup_ver[24];            /* 0x0050 备份区固件版本 */
	uint8_t  _rsv3[8];                  /* 0x0068 保留 */
	uint32_t fw_size;                   /* 0x0070 新固件字节数 */
	uint8_t  _rsv4[12];                 /* 0x0074 保留 */
	uint32_t boot_fail_cnt;             /* 0x0080 启动失败计数（>=3 回滚） */
	uint8_t  _rsv5[12];                 /* 0x0084 保留 */
	uint32_t backup_fw_size;            /* 0x0090 备份固件字节数 */
	uint8_t  _rsv6[0x0100 - 0x0094];    /* 0x0094 保留 */
	uint32_t downloaded_size;           /* 0x0100 已下载字节（断点续传） */
	uint8_t  _rsv7[0x0240 - 0x0104];    /* 0x0104 保留 */
	char     file_md5[33];              /* 0x0240 平台固件 MD5（32 字符 + '\0'） */
} ota_flags_t;

void OTA_Flags_Load(ota_flags_t *f);        /* Zone3 -> RAM（全 0xFF 自动格式化） */
void OTA_Flags_Save(const ota_flags_t *f);  /* RAM -> Zone3（R-M-E-W 整体写回） */
void OTA_Flags_Format(void);                /* 擦除清零（标志区内容非法时用） */

#endif /* __OTA_FLAGS_H */
