#ifndef __MYTASKS_H
#define __MYTASKS_H

#include "FreeRTOS.h"
#include "semphr.h"
#include "queue.h"
#include "event_groups.h"
#include "timers.h"

/* ===== 同步对象句柄 ===== */
extern SemaphoreHandle_t  xSemACK;        /* ESP32 ACK 信号量（ISR -> 任务） */
extern SemaphoreHandle_t  xSemIPD;        /* +IPD 数据帧信号量（ISR -> OTA 解析） */
extern SemaphoreHandle_t  xSemMQTT;       /* +MQTTSUBRECV 下发帧信号量 */
extern SemaphoreHandle_t  xMtxPrintf;     /* printf 互斥锁 */
extern SemaphoreHandle_t  xMtxLCD;        /* LCD 互斥锁 */
extern SemaphoreHandle_t  xMtxNet;        /* ESP32 网络事务锁（天气抓取 vs OTA 互斥） */
extern QueueHandle_t      xQueueDHT;      /* DHT11 数据队列（Sensor -> UI，深度1覆盖式） */
extern QueueHandle_t      xQueueCmd;      /* ESP32 命令队列（定时器/UI -> ESP32） */
extern EventGroupHandle_t xEvtSystem;     /* 系统状态事件组 */
extern TimerHandle_t      xTmrWeather;    /* 15 分钟天气刷新定时器 */
extern TimerHandle_t      xTmrSNTP;       /* 1 小时 SNTP 校时定时器 */

extern TaskHandle_t xTaskUI_Handle;       /* UI 任务句柄（按键 EXTI ISR 通知目标） */
extern TaskHandle_t xTaskESP32_Handle;    /* ESP32 任务句柄 */
extern QueueHandle_t      xQueueOTA;      /* OTA 独立任务触发队列 */
extern volatile uint8_t   ota_session;    /* OTA 会话进行中（禁天气/校时/重连） */

/* ===== 系统事件位（xEvtSystem） ===== */
#define EVT_WIFI_CONNECTED  (1 << 0)      /* 0b001  bit0 = WiFi 已连接 */
#define EVT_SNTP_OK         (1 << 1)      /* 0b010  bit1 = SNTP 校时成功 */
#define EVT_WEATHER_OK      (1 << 2)      /* 0b100  bit2 = 天气数据就绪 */

/* ===== ESP32 命令xQueueCmd 的载荷 ===== */
typedef enum {
	CMD_SNTP_SYNC = 0,                    /* SNTP 校时 */
	CMD_WEATHER_FETCH,                    /* 抓取天气 */
	CMD_WIFI_RECONNECT,					/* 重试连接 */
	CMD_OTA_DOWNLOAD,					/* OTA：查询任务+分片下载+READY */
} esp32_cmd_t;


void AppTasks_Create(void);

#endif /* __MYTASKS_H */
