#ifndef __OTA_HTTP_H
#define __OTA_HTTP_H

#include <stdint.h>

/* OTA 页 UI 状态（vTaskESP32 写 / lvgl tick 读，单字节标量跨任务安全） */
typedef enum {
	OTA_UI_IDLE = 0,      /* 空闲 */
	OTA_UI_CHECKING,      /* 查询升级任务中 */
	OTA_UI_NO_TASK,       /* 平台无升级任务 */
	OTA_UI_DOWNLOADING,   /* 分片下载中（看 percent） */
	OTA_UI_VERIFYING,     /* MD5 校验中 */
	OTA_UI_DONE_REBOOT,   /* READY 已写，即将复位进 BootLoader */
	OTA_UI_FAIL,          /* 失败（可重试） */
} ota_ui_state_t;

typedef struct {
	volatile uint8_t state;
	volatile uint8_t percent;
} ota_ui_t;

extern volatile ota_ui_t g_ota_ui;

void OTA_Run(const char *ssid, const char *pwd);   /* 查询任务 -> 分片下载 -> MD5 -> READY -> 复位 */
void OTA_NotifyFail(void);   /* 无 WiFi 等场景：仅置 UI 失败态 */

#endif /* __OTA_HTTP_H */
