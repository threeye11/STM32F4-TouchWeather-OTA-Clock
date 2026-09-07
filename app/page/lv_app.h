#ifndef __LV_APP_H
#define __LV_APP_H

#include "stm32f4xx.h"
#include "lvgl.h"
#include <stdbool.h>
#include "esp32.h"
#include "weather.h"
#include "dht11.h"

/* ===== 网络配置 ===== */
#define WIFI_SSID      "YOUR_WIFI_SSID"            /* TODO: 你的 Wi-Fi 名称 */
#define WIFI_PASSWORD  "YOUR_WIFI_PASSWORD"        /* TODO: 你的 Wi-Fi 密码 */
#define WEATHER_URL    "https://api.seniverse.com/v3/weather/now.json?key=YOUR_API_KEY&location=Nanning&language=zh-Hans&unit=c"  /* TODO: 心知天气 API key */

/* ===== 错误码 ===== */
typedef enum {
	UI_ERR_NONE = 0,
	UI_ERR_ESP32_INIT,
	UI_ERR_WIFI_CONNECT,
	UI_ERR_SNTP,
	UI_ERR_WEATHER,
}ui_error_t;

/* ===== 应用级共享数据 ===== */
extern ESP32_DataTime ui_clock;
extern weather_info   ui_weather;
extern dht11_data_t   ui_dht;
extern bool           ui_dht_valid;
extern ui_error_t     ui_last_error;

/* ===== 页面框架 ===== */
typedef enum {
	LV_PAGE_BOOT = 0,
	LV_PAGE_MAIN,
	LV_PAGE_WEATHER,
	LV_PAGE_NETWORK,
	LV_PAGE_OTA,
	LV_PAGE_NUM,
}lv_page_t;

void LvApp_Switch(lv_page_t p);
void LvApp_Init(void);
void LvApp_Tick(void);

#endif /* __LV_APP_H */
