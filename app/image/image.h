#ifndef __IMAGE_H__
#define __IMAGE_H__

#include <stdint.h>
#include "lvgl.h"

/* 旧 LCD 位图格式（LCD_ShowImage 用） */
typedef struct
{
	uint16_t width;
	uint16_t height;
	const unsigned char *data;
}ImageShow;

/* LVGL 图片格式：lv_img_set_src(img, &xxx) 直接使用 */
extern const unsigned char wallpaper_map[];
extern const lv_img_dsc_t  wallpaper;
extern const unsigned char icon_logo_map[];
extern const lv_img_dsc_t  icon_logo;
extern const unsigned char icon_battery_map[];
extern const lv_img_dsc_t  icon_battery;
extern const unsigned char icon_Clock_map[];
extern const lv_img_dsc_t  icon_Clock;
extern const unsigned char icon_Network_map[];
extern const lv_img_dsc_t  icon_Network;
extern const unsigned char icon_Ota_map[];
extern const lv_img_dsc_t  icon_Ota;
extern const unsigned char icon_wifi_on_map[];
extern const lv_img_dsc_t  icon_wifi_on;
extern const unsigned char icon_wifi_off_map[];
extern const lv_img_dsc_t  icon_wifi_off;
extern const unsigned char icon_Weather_map[];
extern const lv_img_dsc_t  icon_Weather;
extern const unsigned char icon_cloud_map[];
extern const lv_img_dsc_t  icon_cloud;
extern const unsigned char icon_HeavyRain_map[];
extern const lv_img_dsc_t  icon_HeavyRain;
extern const unsigned char icon_sun_map[];
extern const lv_img_dsc_t  icon_sun;

#endif /* __IMAGE_H__ */
