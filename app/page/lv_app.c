#include "lv_app.h"
#include "mytasks.h"
#include "esp32.h"
#include "weather.h"      
#include "dht11.h"
#include "rtc.h"             
#include "bsp_uart.h"
#include "image.h"
#include "battery.h"
#include "ota_http.h"
#include "ota_config.h"

/* 自定义中文字体 */
LV_FONT_DECLARE(my_chinese_font_16);
LV_FONT_DECLARE(my_chinese_font_32);
LV_FONT_DECLARE(my_chinese_font_48);
/* ===== 应用级共享数据 ===== */
ESP32_DataTime ui_clock;
weather_info   ui_weather;
dht11_data_t   ui_dht;
bool           ui_dht_valid = false;
ui_error_t     ui_last_error = UI_ERR_NONE;

typedef struct {
	void (*create)(void); //创建页面对象
	void (*enter)(void); 	//每次进入时刷新数据
	void (*tick)(void); 	//页面停留期间的周期
}lv_page_ops_t;

static lv_obj_t *scr[LV_PAGE_NUM];
static lv_page_t cur =LV_PAGE_BOOT;

/* ==================== 函数表 ==================== */
static void page_boot_create(void);
static void page_boot_enter(void);
static void page_boot_tick(void);

static void page_main_create(void);
static void page_main_enter(void);
static void page_main_tick(void);
static void page_weather_create(void);
static void page_weather_enter(void);
static void page_weather_tick(void);
static void page_network_create(void);
static void page_network_enter(void);
static void page_network_tick(void);
static void page_ota_create(void);
static void page_ota_enter(void);
static void page_ota_tick(void);

static const lv_page_ops_t pages[LV_PAGE_NUM] = {
	{page_boot_create,    page_boot_enter,    page_boot_tick},
	{page_main_create,    page_main_enter,    page_main_tick},
	{page_weather_create, page_weather_enter, page_weather_tick},
	{page_network_create, page_network_enter, page_network_tick},
	{page_ota_create,     page_ota_enter,     page_ota_tick},
};

void LvApp_Switch(lv_page_t p)
{
	cur = p;
	if (scr[p] == NULL)pages[p].create();	//屏幕对象尚未创建则进行创建
	pages[p].enter();
	lv_scr_load_anim(scr[p] ,LV_SCR_LOAD_ANIM_MOVE_LEFT ,250 ,0 ,false);
}

void LvApp_Init(void)      { LvApp_Switch(LV_PAGE_BOOT); }
lv_page_t LvApp_GetPage(void){return cur;}

void LvApp_Tick(void)
{
	if (pages[cur].tick)pages[cur].tick();
}

/* ==================== BOOT开机 + WiFi静默连接 ==================== */
static lv_obj_t *boot_status;

static void page_boot_create(void)
{
	scr[LV_PAGE_BOOT] = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr[LV_PAGE_BOOT] ,lv_color_black() ,0);
	
	lv_obj_t *logo = lv_img_create(scr[LV_PAGE_BOOT]);
	lv_img_set_src(logo ,&icon_logo);
	lv_obj_align(logo ,LV_ALIGN_CENTER ,0 ,-60);
	
	lv_obj_t *title = lv_label_create(scr[LV_PAGE_BOOT]);
	lv_label_set_text(title ,"Smart Weather Clock");
	lv_obj_set_style_text_color(title , lv_color_hex(0xFFFFFF),0);
	lv_obj_set_style_text_font(title, &my_chinese_font_16, 0);
	lv_obj_align(title ,LV_ALIGN_CENTER , 0 ,10);
	
	lv_obj_t *text = lv_label_create(scr[LV_PAGE_BOOT]);
	lv_label_set_text(text ,"Welcome!");
	lv_obj_set_style_text_color(text , lv_color_hex(0xFFFFFF),0);
	lv_obj_set_style_text_font(text, &my_chinese_font_32, 0);
	lv_obj_align(text ,LV_ALIGN_CENTER , 0 ,70);
	
	boot_status = lv_label_create(scr[LV_PAGE_BOOT]);
	lv_label_set_text(boot_status ,"Connecting WiFi...");
	lv_obj_set_style_text_color(boot_status , lv_color_hex(0xFFFFFF),0);
	lv_obj_set_style_text_font(boot_status, &my_chinese_font_16, 0);
	lv_obj_align(boot_status, LV_ALIGN_CENTER, 0, 100);
	
}

static void page_boot_enter(void)
{
	lv_label_set_text(boot_status, "Connecting WiFi...");
}

static void page_boot_tick(void)
{
	static uint16_t cnt = 0;      /* LvApp_Tick 每 5ms 一次，400 次 = 2s */
	/* 默认尝试连接：不判成败，固定 2 秒后进桌面 */
	/* 注意：不要在 tick 里创建控件 */
	if (++cnt >= 400) { cnt = 0; LvApp_Switch(LV_PAGE_MAIN); }
}

/* 发起一次 WiFi 重连：清错误、清事件位、发重连命令 */
void LvApp_NetReconnect(void)
{
	ui_last_error = UI_ERR_NONE;
	xEventGroupClearBits(xEvtSystem , EVT_WIFI_CONNECTED | EVT_SNTP_OK | EVT_WEATHER_OK);
	esp32_cmd_t cmd = CMD_WIFI_RECONNECT;
	xQueueSend(xQueueCmd ,&cmd ,0);
}


/* 错误码 -> 中文提示（联网页 n_err 显示；中文以 UTF-8 转义书写，源文件为 GBK） */
static const char *error_text(ui_error_t e)
{
	switch (e)
	{
	case UI_ERR_ESP32_INIT:   return "ESP32 \xE5\x88\x9D\xE5\xA7\x8B\xE5\x8C\x96\xE5\xA4\xB1\xE8\xB4\xA5"; /* 初始化失败 */
	case UI_ERR_WIFI_CONNECT: return "WiFi \xE8\xBF\x9E\xE6\x8E\xA5\xE5\xA4\xB1\xE8\xB4\xA5"; /* 连接失败 */
	case UI_ERR_SNTP:         return "\xE7\xBD\x91\xE7\xBB\x9C\xE6\xA0\xA1\xE6\x97\xB6\xE5\xA4\xB1\xE8\xB4\xA5"; /* 网络校时失败 */
	case UI_ERR_WEATHER:      return "\xE5\xA4\xA9\xE6\xB0\x94\xE8\x8E\xB7\xE5\x8F\x96\xE5\xA4\xB1\xE8\xB4\xA5"; /* 天气获取失败 */
	default:                  return "\xE9\x94\x99\xE8\xAF\xAF"; /* 错误（字体缺"未知"二字，如需请补进 symbols） */
	}
}

/* ==================== MAIN:桌面 ==================== */
static lv_obj_t *img_wifi_Normal;        /* 状态栏 WiFi 图标 */
static lv_obj_t *img_bat;         				/* 状态栏电量条框架 */
static lv_obj_t *val_bat;         				/* 状态栏电量条 */
static lv_obj_t *btn_weather, *btn_network, *btn_ota;

/* APP 图标回调：lv_event_get_user_data 拿到目标页号 */
static void app_icon_cb(lv_event_t *e)
{
	//(uintptr_t)将 void * 转换为整数类型
	lv_page_t p = (lv_page_t)(uintptr_t)lv_event_get_user_data(e);
	LvApp_Switch(p);
}

/* 返回桌面回调（三个功能页共用 */
static void back_cb(lv_event_t *e)
{
	(void)e;
	LvApp_Switch(LV_PAGE_MAIN);
}

/* 建一个 APP 图标：透明按钮 + 图标 + 名称 */
static void main_icon_create(lv_obj_t *parent , const lv_img_dsc_t *icon ,
															const char *name , lv_page_t page)
{
	lv_obj_t *btn = lv_btn_create(parent);
	lv_obj_set_size(btn ,72 ,88);
	lv_obj_set_style_bg_opa(btn ,LV_OPA_TRANSP ,0);			//设置为透明
	
	lv_obj_t *img = lv_img_create(btn);
	lv_img_set_src(img , icon);
	lv_obj_align(img , LV_ALIGN_TOP_MID ,0 ,4);					//调整图标位置
	
	lv_obj_t *lab = lv_label_create(btn);
	lv_label_set_text(lab ,name);
	lv_obj_set_style_text_color(lab , lv_color_hex(0xFFFFFF),0);
	lv_obj_set_style_text_font(lab, &my_chinese_font_16, 0);
	lv_obj_align(lab, LV_ALIGN_BOTTOM_MID, 0, -2);
	
	lv_obj_add_event_cb(btn ,app_icon_cb ,LV_EVENT_CLICKED ,(void *)(uintptr_t)page);
}

static void page_main_create(void)
{
	scr[LV_PAGE_MAIN] = lv_obj_create(NULL);
	
	/* 底层：壁纸铺满全屏 */
	lv_obj_t *wall = lv_img_create(scr[LV_PAGE_MAIN]);
	lv_img_set_src(wall ,&wallpaper);
	lv_obj_set_pos(wall, 0, 0);
	
	/* 顶层：状态栏 WiFi 图标 + 电量条 */
	img_wifi_Normal = lv_img_create(scr[LV_PAGE_MAIN]);
	lv_img_set_src(img_wifi_Normal, &icon_wifi_on);
	lv_obj_align(img_wifi_Normal, LV_ALIGN_TOP_LEFT, 0, -2);
	
	img_bat = lv_img_create(scr[LV_PAGE_MAIN]);
	lv_img_set_src(img_bat, &icon_battery);						
	lv_obj_align(img_bat, LV_ALIGN_TOP_RIGHT, 0, -2);
	val_bat = lv_bar_create(img_bat);
	lv_obj_set_size(val_bat, 26, 11);
	lv_obj_set_pos(val_bat, 2, 11);                          /* 图标 32x32，内框区域约 (2,11)-(27,21) */
	lv_obj_set_style_bg_color(val_bat, lv_color_hex(0x161616), LV_PART_MAIN);      /* 轨道：深灰=空 */
	lv_obj_set_style_bg_color(val_bat, lv_color_hex(0x00E676), LV_PART_INDICATOR); /* 填充：亮绿=有电 */
	lv_bar_set_range(val_bat, 0, 100);
	lv_bar_set_value(val_bat, 100, LV_ANIM_OFF);             /* 初始满电，首个 200ms 后更新为实际值 */
	
	main_icon_create(scr[LV_PAGE_MAIN] ,&icon_Weather ,"Weather" ,LV_PAGE_WEATHER);
	main_icon_create(scr[LV_PAGE_MAIN] ,&icon_Network ,"Network" ,LV_PAGE_NETWORK);
	main_icon_create(scr[LV_PAGE_MAIN], &icon_Ota,     "OTA",     LV_PAGE_OTA);
	btn_weather = lv_obj_get_child(scr[LV_PAGE_MAIN], 3);
	btn_network = lv_obj_get_child(scr[LV_PAGE_MAIN], 4);
	btn_ota     = lv_obj_get_child(scr[LV_PAGE_MAIN], 5);
	lv_obj_align(btn_weather, LV_ALIGN_BOTTOM_MID, -80, -10);
	lv_obj_align(btn_network, LV_ALIGN_BOTTOM_MID,   0, -10);
	lv_obj_align(btn_ota,     LV_ALIGN_BOTTOM_MID,  80, -10);
}
static void page_main_enter(void) {/* 进入桌面无需刷新（tick 持续更新状态栏） */  }

static void page_main_tick(void)
{
	static uint8_t last_wifi  = 0xFF ,last_bat = 0xFF;
	static uint8_t div = 0;
	uint8_t wifi =(xEventGroupGetBits(xEvtSystem) & EVT_WIFI_CONNECTED) ? 1 : 0;

	if (wifi != last_wifi)
	{
		last_wifi = wifi ;
		lv_img_set_src(img_wifi_Normal ,wifi ? &icon_wifi_on : &icon_wifi_off);
	}
	/* 电量读取含 16 次 ADC 转换（约 0.4ms 阻塞），每 40 tick（200ms）读一次 */
	if (++div >= 40)
	{
		div = 0;
		uint8_t bat = Battery_GetPercent();
		if (bat != last_bat && bat <= 100)
		{
			last_bat = bat;
			lv_bar_set_value(val_bat ,bat ,LV_ANIM_OFF);
			safe_printf("[BAT] %d%% (%d mV)\r\n", bat, Battery_GetVoltage_mV());
		}
	}
}


/* ==================== 天气页 ==================== */
static lv_obj_t *w_city, *w_temp, *w_text, *w_dht_Temp, *w_dht_Humi,*w_gonet;
static lv_obj_t *w_icon;
static lv_obj_t *w_time, *w_date;
static lv_obj_t *container_time ,*container_indoor ,*container_outdoor;

/* RTC 星期号转中文（ 1=周一 .. 7=周日）
 * 中文用 UTF-8 十六进制转义书写（源文件为 GBK 编码，不能直接写 UTF-8 字面量） */
static const char *wd_name(uint8_t wd)
{
	/* 周一 周二 周三 周四 周五 周六 周日 */
	static const char *n[] = {"",
		"\xE5\x91\xA8\xE4\xB8\x80", "\xE5\x91\xA8\xE4\xBA\x8C",
		"\xE5\x91\xA8\xE4\xB8\x89", "\xE5\x91\xA8\xE5\x9B\x9B",
		"\xE5\x91\xA8\xE4\xBA\x94", "\xE5\x91\xA8\xE5\x85\xAD",
		"\xE5\x91\xA8\xE6\x97\xA5"};
	return (wd >= 1 && wd <= 7) ? n[wd] : "";
}

/* 天气码 -> 图标（心知天气 now.code 分段，粗略映射仅为展示效果）
 *   0-3   晴（sun）
 *   4-28  多云/阴/风/浮尘/扬沙/沙尘暴/霾/雾（cloud 占位）
 *   29-43 阵雨/雷阵雨/雨/雪/冰雹（HeavyRain 占位；后续可细分雨/雪图标）
 */
static const lv_img_dsc_t *weather_icon(int code)
{
	if (code <= 3) return &icon_sun;
	if (code <= 28) return &icon_cloud;
	return &icon_HeavyRain;
}

/* 按当前天气数据刷新图标：数据未就绪则隐藏 */
static void weather_refresh_icon(void)
{
	if (ui_weather.weather[0])
	{
		lv_img_set_src(w_icon, weather_icon(ui_weather.weather_code));
		lv_obj_clear_flag(w_icon, LV_OBJ_FLAG_HIDDEN);
	}
	else
	{
		lv_obj_add_flag(w_icon, LV_OBJ_FLAG_HIDDEN);
	}
}

/* 天气获取失败 -> 跳转联网页 */
static void goto_net_cb(lv_event_t *e)
{
	(void)e;
	LvApp_Switch(LV_PAGE_NETWORK);
}

static void page_weather_create(void)
{
	scr[LV_PAGE_WEATHER] = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr[LV_PAGE_WEATHER], lv_color_hex(0xFFFFFF), 0);		
	
	lv_obj_t *back = lv_btn_create(scr[LV_PAGE_WEATHER]);
	lv_obj_set_size(back, 48, 32);
	lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
	lv_obj_set_style_bg_color(back , lv_color_hex(0x161616) ,0);
	lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl = lv_label_create(back);
	lv_label_set_text(bl, "<");
	lv_obj_center(bl);
	
	//RTC时间日期
	container_time = lv_obj_create(scr[LV_PAGE_WEATHER]);
	lv_obj_set_size(container_time ,236 ,128);
	lv_obj_set_pos(container_time ,4, 38);
	lv_obj_set_style_bg_color(container_time, lv_color_hex(0xC4FF71), LV_PART_MAIN);
	lv_obj_set_style_radius(container_time, 8, LV_PART_MAIN);
	/* 时间/日期标签 */
	w_time = lv_label_create(container_time);
	lv_obj_set_style_text_color(w_time, lv_color_hex(0x161616), 0);
	lv_obj_align(w_time, LV_ALIGN_CENTER, 0, -28);
	lv_obj_set_style_text_font(w_time, &my_chinese_font_48, 0);	/* 时钟大字 */
	lv_label_set_text(w_time, "--:--:--");

	w_date = lv_label_create(container_time);
	lv_obj_set_style_text_color(w_date, lv_color_hex(0x161616), 0);
	lv_obj_align(w_date, LV_ALIGN_CENTER, 0, 32);
	lv_label_set_text(w_date, "----/--/--");
	lv_obj_set_style_text_font(w_date, &my_chinese_font_16, 0);
	
	//室外部分
	container_outdoor = lv_obj_create(scr[LV_PAGE_WEATHER]);
	lv_obj_set_size(container_outdoor ,114 ,148);
	lv_obj_set_pos(container_outdoor ,122, 166);
	lv_obj_set_style_bg_color(container_outdoor, lv_color_hex(0xF44336), LV_PART_MAIN);
	lv_obj_set_style_radius(container_outdoor, 8, LV_PART_MAIN);
	
	w_city = lv_label_create(container_outdoor);
	lv_obj_set_style_text_color(w_city , lv_color_hex(0x000000),0);
	lv_obj_align(w_city, LV_ALIGN_TOP_MID, -10, 0);
	lv_obj_set_style_text_font(w_city, &my_chinese_font_32, 0);

	w_temp = lv_label_create(container_outdoor);
	lv_obj_set_style_text_color(w_temp , lv_color_hex(0x000000),0);
	lv_obj_align(w_temp, LV_ALIGN_CENTER, -10, 0);
	lv_obj_set_style_text_font(w_temp, &my_chinese_font_32, 0);

	w_text = lv_label_create(container_outdoor);
	lv_obj_set_style_text_color(w_text , lv_color_hex(0x000000),0);
	lv_obj_align(w_text, LV_ALIGN_BOTTOM_LEFT, -10, 0);
	lv_obj_set_style_text_font(w_text, &my_chinese_font_32, 0);

	/* 天气图标：放室外卡下半部空区（数据无效时隐藏） */
	w_icon = lv_img_create(container_outdoor);
	lv_obj_align(w_icon, LV_ALIGN_CENTER, 0, 35);
	
	//室内部分
	container_indoor = lv_obj_create(scr[LV_PAGE_WEATHER]);
	lv_obj_set_size(container_indoor ,114 ,148);
	lv_obj_set_pos(container_indoor ,4, 166);
	lv_obj_set_style_bg_color(container_indoor, lv_color_hex(0x3dcfdc), LV_PART_MAIN);
	lv_obj_set_style_radius(container_indoor, 8, LV_PART_MAIN);
	lv_obj_t *indoor_label = lv_label_create(container_indoor);
	lv_obj_set_style_text_color(indoor_label, lv_color_hex(0x000000), 0);
	lv_obj_set_style_text_font(indoor_label, &my_chinese_font_16, 0);
	lv_label_set_text(indoor_label, "\xE5\xAE\xA4\xE5\x86\x85\xE7\x8E\xAF\xE5\xA2\x83\x3A"); /* 室内环境: */
	lv_obj_align(indoor_label, LV_ALIGN_TOP_LEFT, -10, 0);
	w_dht_Temp = lv_label_create(container_indoor);
	lv_obj_set_style_text_color(w_dht_Temp , lv_color_hex(0x000000),0);
	lv_obj_set_style_text_font(w_dht_Temp, &my_chinese_font_32, 0);
	lv_obj_align(w_dht_Temp, LV_ALIGN_TOP_LEFT, -10, 20);
	w_dht_Humi = lv_label_create(container_indoor);
	lv_obj_set_style_text_color(w_dht_Humi , lv_color_hex(0x000000),0);
	lv_obj_set_style_text_font(w_dht_Humi, &my_chinese_font_32, 0);
	lv_obj_align(w_dht_Humi, LV_ALIGN_TOP_LEFT, -10, 60);
	
	/* 获取失败时的"前往联网"按钮 */
	w_gonet = lv_btn_create(scr[LV_PAGE_WEATHER]);
	lv_obj_set_size(w_gonet, 120, 40);
	lv_obj_align(w_gonet, LV_ALIGN_CENTER, 0, 100);
	lv_obj_add_event_cb(w_gonet, goto_net_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *gl = lv_label_create(w_gonet);
	lv_label_set_text(gl, "Go Network");
	lv_obj_center(gl);
	lv_obj_add_flag(w_gonet , LV_OBJ_FLAG_HIDDEN);	//隐藏图标
}
/* 室内温湿度标签：数据由 TaskUI 每 2s 收队更新 ui_dht，
 * 进入页面与停留期间（tick 变化检测）共用本函数刷新 */
static void weather_refresh_dht(void)
{
	if (ui_dht_valid)
	{
		lv_label_set_text_fmt(w_dht_Temp, "%d.%dC", ui_dht.temp_int, ui_dht.temp_dec);
		lv_label_set_text_fmt(w_dht_Humi, "%d.%d%%", ui_dht.humi_int, ui_dht.humi_dec);
	}
	else
	{
		lv_label_set_text(w_dht_Temp, "Temp: --");
		lv_label_set_text(w_dht_Humi, "Humi: --");
	}
}

static void page_weather_enter(void) 
{ 
	lv_label_set_text(w_city ,ui_weather.city[0] ? ui_weather.city : "--");
	if (ui_weather.weather[0])
	{
		lv_label_set_text_fmt(w_temp, "%d.%dC", ui_weather.temp_int, ui_weather.temp_frac);
		lv_label_set_text(w_text, ui_weather.weather);
		lv_obj_add_flag(w_gonet, LV_OBJ_FLAG_HIDDEN);      /* 数据就绪：隐藏跳转按钮 */
	} 
	else {
		lv_label_set_text(w_temp, "--");
		lv_label_set_text(w_text, "weather N/A");
		lv_obj_clear_flag(w_gonet,LV_OBJ_FLAG_HIDDEN); /* 获取失败：显示跳转按钮 */
	}
	
	weather_refresh_dht();
	weather_refresh_icon();

	/* RTC 时间日期（tick 每秒续刷） */
	{
		RTC_TimeTypeDef rt;
		RTC_DateTypeDef rd;
		MyRTC_ReadTime(&rt, &rd);
		lv_label_set_text_fmt(w_time, "%02d:%02d:%02d",
		                      rt.RTC_Hours, rt.RTC_Minutes, rt.RTC_Seconds);
		lv_label_set_text_fmt(w_date, "%04d/%02d/%02d %s",
		                      (int)2000 + rd.RTC_Year, rd.RTC_Month, rd.RTC_Date,
		                      wd_name(rd.RTC_WeekDay));
	}
}

/* RTC 时钟：时间每秒刷一次，日期在分钟变化时刷一次（页内 5ms 周期调用） */
static void page_weather_tick(void)
{
	RTC_TimeTypeDef rt;
	RTC_DateTypeDef rd;
	static uint8_t last_sec = 0xFF;
	static uint8_t last_min = 0xFF;

	MyRTC_ReadTime(&rt, &rd);
	if (rt.RTC_Seconds != last_sec)
	{
		last_sec = rt.RTC_Seconds;
		lv_label_set_text_fmt(w_time, "%02d:%02d:%02d",
		                      rt.RTC_Hours, rt.RTC_Minutes, rt.RTC_Seconds);
	}
	if (rt.RTC_Minutes != last_min)
	{
		last_min = rt.RTC_Minutes;
		lv_label_set_text_fmt(w_date, "%04d/%02d/%02d %s",
		                      (int)2000 + rd.RTC_Year, rd.RTC_Month, rd.RTC_Date,
		                      wd_name(rd.RTC_WeekDay));
	}

	/* 室内 DHT：新帧每 2s 到达，500ms 检测一次字段变化再刷新标签 */
	{
		static uint8_t dht_div = 0;
		static uint8_t d_valid = 0;
		static int d_ti = 0, d_td = 0, d_hi = 0, d_hd = 0;
		static int w_code = -1;

		if (++dht_div >= 100)
		{
			dht_div = 0;
			if (ui_dht_valid != d_valid || ui_dht.temp_int != d_ti ||
			    ui_dht.temp_dec != d_td || ui_dht.humi_int != d_hi ||
			    ui_dht.humi_dec != d_hd)
			{
				d_valid = ui_dht_valid;
				d_ti = ui_dht.temp_int; d_td = ui_dht.temp_dec;
				d_hi = ui_dht.humi_int; d_hd = ui_dht.humi_dec;
				weather_refresh_dht();
			}
			if (ui_weather.weather_code != w_code)
			{
				w_code = ui_weather.weather_code;
				weather_refresh_icon();
			}
		}
	}
}

/* ==================== 联网页==================== */
static lv_obj_t *n_ssid, *n_state, *n_err;

static void net_reconnect_cb(lv_event_t *e)
{
	(void)e;
	LvApp_NetReconnect();
	lv_label_set_text(n_state, "Connecting...");
	lv_label_set_text(n_err, "");
}

static void page_network_create(void)
{
	scr[LV_PAGE_NETWORK] = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr[LV_PAGE_NETWORK], lv_color_hex(0xFFFFFF), 0);
	
	lv_obj_t *back = lv_btn_create(scr[LV_PAGE_NETWORK]);
	lv_obj_set_size(back, 48, 32);
	lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
	lv_obj_set_style_bg_color(back , lv_color_hex(0x161616) ,0);
	lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl = lv_label_create(back);
	lv_label_set_text(bl, "<");
	lv_obj_center(bl);
	
	n_ssid = lv_label_create(scr[LV_PAGE_NETWORK]);
	lv_obj_set_style_text_font(n_ssid, &my_chinese_font_16, 0);	
	lv_obj_align(n_ssid, LV_ALIGN_TOP_MID, 0, 40);

	n_state = lv_label_create(scr[LV_PAGE_NETWORK]);
	lv_obj_set_style_text_font(n_state, &my_chinese_font_32, 0);		
	lv_obj_align(n_state, LV_ALIGN_CENTER, 0, -20);
	
	n_err = lv_label_create(scr[LV_PAGE_NETWORK]);
	lv_obj_align(n_err, LV_ALIGN_CENTER, 0, 10);
	lv_obj_set_style_text_font(n_err, &my_chinese_font_16, 0);
	lv_obj_set_style_text_color(n_err ,lv_color_hex(0xFF0000) ,0);
	
	lv_obj_t *btn =lv_btn_create(scr[LV_PAGE_NETWORK]);
	lv_obj_set_size(btn, 140, 44);
	lv_obj_align(btn, LV_ALIGN_CENTER, 0, 60);
	lv_obj_add_event_cb(btn, net_reconnect_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl2 = lv_label_create(btn);
	lv_label_set_text(bl2, "Reconnect");
	lv_obj_center(bl2);
}
static void page_network_enter(void)
{
	EventBits_t bits = xEventGroupGetBits(xEvtSystem);

	lv_label_set_text(n_ssid, WIFI_SSID);
	/* 状态按事件组 + 错误码刷新（page_network_tick 每 500ms 续刷） */
	if (bits & EVT_WIFI_CONNECTED)
	{
		lv_label_set_text(n_state, "Connected");
		lv_label_set_text(n_err, "");
	}
	else if (ui_last_error != UI_ERR_NONE)
	{
		lv_label_set_text(n_state, "Disconnected");
		lv_label_set_text(n_err, error_text(ui_last_error));
	}
	else
	{
		lv_label_set_text(n_state, "Connecting...");
		lv_label_set_text(n_err, "");
	}

	/* 失败空闲态才自动重连；连接进行中不重复触发（防止连环重试） */
	if (!(bits & EVT_WIFI_CONNECTED) && ui_last_error != UI_ERR_NONE)
		LvApp_NetReconnect();
}


/* 状态实时刷新：连上/失败/进行中由事件组与错误码驱动（5ms 周期，节流到 500ms） */
static void page_network_tick(void)
{
	static uint8_t div = 0;
	static const char *last_st = NULL;
	static const char *last_err = NULL;
	const char *st;
	const char *err;
	EventBits_t bits;

	if (++div < 100) return;
	div = 0;

	bits = xEventGroupGetBits(xEvtSystem);
	if (bits & EVT_WIFI_CONNECTED)
	{
		st  = "Connected";
		err = "";
	}
	else if (ui_last_error != UI_ERR_NONE)
	{
		st  = "Disconnected";
		err = error_text(ui_last_error);
	}
	else
	{
		st  = "Connecting...";
		err = "";
	}

	if (st != last_st)   { last_st = st;   lv_label_set_text(n_state, st); }
	if (err != last_err) { last_err = err; lv_label_set_text(n_err, err); }
}

/* ==================== 升级页 ==================== */
static lv_obj_t *o_info, *o_stat, *o_bar;
static void ota_btn_cb(lv_event_t *e);       /* 前向声明：create 里注册回调 */

static void page_ota_create(void)
{
	scr[LV_PAGE_OTA] = lv_obj_create(NULL);
	lv_obj_set_style_bg_color(scr[LV_PAGE_OTA], lv_color_hex(0x00b96b), 0);
	
	lv_obj_t *back = lv_btn_create(scr[LV_PAGE_OTA]);
	lv_obj_set_size(back, 48, 32);
	lv_obj_align(back, LV_ALIGN_TOP_LEFT, 6, 6);
	lv_obj_set_style_bg_color(back , lv_color_hex(0x161616) ,0);
	lv_obj_add_event_cb(back, back_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl = lv_label_create(back);
	lv_label_set_text(bl, "<");
	lv_obj_center(bl);
	
	o_info = lv_label_create(scr[LV_PAGE_OTA]);
	lv_obj_set_style_text_font(o_info, &my_chinese_font_16, 0);
	lv_obj_set_style_text_color(o_info, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(o_info, LV_ALIGN_CENTER, 0, -40);
	lv_label_set_text(o_info, "Firmware " FW_VERSION);   /* 编译期版本：升级成功的可见凭据 */

	o_stat = lv_label_create(scr[LV_PAGE_OTA]);
	lv_obj_set_style_text_color(o_stat, lv_color_hex(0xFFFFFF), 0);
	lv_obj_align(o_stat, LV_ALIGN_CENTER, 0, 20);
	lv_label_set_text(o_stat, "Idle");

	/* 下载进度条：值由 g_ota_ui.percent 驱动（OTA 任务逐片更新），仅下载/校验阶段可见 */
	o_bar = lv_bar_create(scr[LV_PAGE_OTA]);
	lv_obj_set_size(o_bar, 180, 12);
	lv_obj_align(o_bar, LV_ALIGN_CENTER, 0, 42);
	lv_bar_set_range(o_bar, 0, 100);
	lv_bar_set_value(o_bar, 0, LV_ANIM_OFF);
	lv_obj_set_style_radius(o_bar, 6, 0);
	lv_obj_set_style_radius(o_bar, 6, LV_PART_INDICATOR);
	lv_obj_set_style_bg_color(o_bar, lv_color_hex(0x161616), 0);
	lv_obj_set_style_bg_color(o_bar, lv_color_hex(0xFFFFFF), LV_PART_INDICATOR);
	lv_obj_add_flag(o_bar, LV_OBJ_FLAG_HIDDEN);

	/* 检查并升级按钮：置命令由 vTaskESP32 执行（UI 不阻塞） */
	lv_obj_t *btn = lv_btn_create(scr[LV_PAGE_OTA]);
	lv_obj_set_size(btn, 160, 48);
	lv_obj_align(btn, LV_ALIGN_CENTER, 0, 80);
	lv_obj_add_event_cb(btn, ota_btn_cb, LV_EVENT_CLICKED, NULL);
	lv_obj_t *bl2 = lv_label_create(btn);
	lv_label_set_text(bl2, "Check & Upgrade");
	lv_obj_center(bl);
}

/* 按钮回调：状态允许才发命令 */
static void ota_btn_cb(lv_event_t *e)
{
	(void)e;
	if (g_ota_ui.state == OTA_UI_IDLE || g_ota_ui.state == OTA_UI_FAIL ||
	    g_ota_ui.state == OTA_UI_NO_TASK)
	{
		esp32_cmd_t cmd = CMD_OTA_DOWNLOAD;
		xQueueSend(xQueueCmd, &cmd, 0);
	}
}

static void page_ota_enter(void)
{
	lv_label_set_text_fmt(o_info, "Firmware %s", FW_VERSION);
	lv_label_set_text(o_stat, "Idle");
	lv_obj_add_flag(o_bar, LV_OBJ_FLAG_HIDDEN);
	lv_bar_set_value(o_bar, 0, LV_ANIM_OFF);
}

/* OTA 状态实时刷新（500ms 节流，读共享 g_ota_ui） */
static void page_ota_tick(void)
{
	static uint8_t div = 0;
	const char *txt;

	if (++div < 100) return;
	div = 0;

	switch (g_ota_ui.state)
	{
	case OTA_UI_IDLE:        txt = "Idle"; break;
	case OTA_UI_CHECKING:    txt = "Checking..."; break;
	case OTA_UI_NO_TASK:     txt = "No update"; break;
	case OTA_UI_DOWNLOADING: txt = "Downloading"; break;
	case OTA_UI_VERIFYING:   txt = "Verifying..."; break;
	case OTA_UI_DONE_REBOOT: txt = "Rebooting..."; break;
	default:                 txt = "Failed, retry"; break;
	}

	/* 进度条映射：下载中 = percent；校验中 = 满条；其余 = 隐藏 */
	if (g_ota_ui.state == OTA_UI_DOWNLOADING)
	{
		lv_obj_clear_flag(o_bar, LV_OBJ_FLAG_HIDDEN);
		lv_bar_set_value(o_bar, (int)g_ota_ui.percent, LV_ANIM_OFF);
		lv_label_set_text_fmt(o_stat, "%s %d%%", txt, (int)g_ota_ui.percent);
	}
	else if (g_ota_ui.state == OTA_UI_VERIFYING)
	{
		lv_obj_clear_flag(o_bar, LV_OBJ_FLAG_HIDDEN);
		lv_bar_set_value(o_bar, 100, LV_ANIM_OFF);
		lv_label_set_text(o_stat, txt);
	}
	else
	{
		lv_obj_add_flag(o_bar, LV_OBJ_FLAG_HIDDEN);
		lv_bar_set_value(o_bar, 0, LV_ANIM_OFF);
		lv_label_set_text(o_stat, txt);
	}
}
