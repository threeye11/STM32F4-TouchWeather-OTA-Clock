/*===========================================================================
 *  mytasks.c - FreeRTOS 任务层
 *  TaskSensor(4)  DHT11 采集   -> xQueueDHT
 *  TaskESP32(3)   网络/天气/重连 <- xQueueCmd
 *  TaskUI(2)      LVGL 渲染/按键
 *  同步对象/定时器/任务在 AppTasks_Create() 中创建
 *===========================================================================*/
#include "stm32f4xx.h"
#include "mytasks.h"
#include "bsp_uart.h"
#include "esp32.h"
#include "weather.h"
#include "dht11.h"
#include "rtc.h"
#include "tp_cal.h"
#include "lvgl.h"
#include "lv_port_disp_template.h"
#include "lv_port_indev_template.h"
#include "lv_app.h"
#include "ota_http.h"
#include "ota_config.h"

#include "task.h"
#include "semphr.h"
#include "event_groups.h"
#include "queue.h"
#include "timers.h"

/* ===== 同步对象句柄定义 ===== */
SemaphoreHandle_t  xSemACK        = NULL;   /* ESP32 ACK 信号量（ISR -> 任务） */
SemaphoreHandle_t  xMtxPrintf     = NULL;   /* printf 互斥锁 */
SemaphoreHandle_t  xMtxLCD        = NULL;   /* LCD 互斥锁 */
QueueHandle_t      xQueueDHT      = NULL;   /* DHT11 数据队列（Sensor -> UI） */
QueueHandle_t      xQueueCmd      = NULL;   /* ESP32 命令队列（定时器/UI -> ESP32） */
EventGroupHandle_t xEvtSystem     = NULL;   /* 系统状态事件组 */
SemaphoreHandle_t  xSemIPD        = NULL;   /* +IPD 数据帧信号量（ISR -> OTA 解析） */
SemaphoreHandle_t  xSemMQTT       = NULL;   /* +MQTTSUBRECV 下发帧信号量 */
TimerHandle_t      xTmrWeather    = NULL;   /* 15 分钟天气刷新 */
TimerHandle_t      xTmrSNTP       = NULL;   /* 1 小时 SNTP 校时 */
QueueHandle_t      xQueueOTA      = NULL;   /* OTA 独立任务触发队列 */
volatile uint8_t   ota_session    = 0;      /* OTA 会话进行中标志 */
SemaphoreHandle_t  xMtxNet        = NULL;   /* ESP32 网络事务锁（天气抓取 vs OTA 互斥） */

TaskHandle_t xTaskUI_Handle    = NULL;     /* 按键 EXTI ISR 通知目标 */
TaskHandle_t xTaskESP32_Handle = NULL;


/* TaskSensor（优先级4）：每 2 秒采集 DHT11，覆盖式写入队列 */
static void vTaskSensor(void *pvparameters)
{
	dht11_data_t data;
	uint32_t fail = 0;
	(void)pvparameters;

	for (;;)
	{
		if (DHT11_Read(&data))
		{
			fail = 0;
			xQueueOverwrite(xQueueDHT, &data);	/* 深度1覆盖式：UI 永远拿到最新值 */
		}
		else if (++fail % 20 == 0)
		{
			safe_printf("[DHT] read fail x%lu (check wiring)\r\n", (unsigned long)fail);
		}
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
}

/*===========================================================================
 *  连接阶段（首次上电 / 重连共用）：清错误位 -> ESP32 握手 ->
 *  连 WiFi（3 次）。成功置 EVT_WIFI_CONNECTED；失败设置 ui_last_error
 *===========================================================================*/
static bool Esp32_TaskConnect(void)
{
	int attempt;

	ui_last_error = UI_ERR_NONE;	/* 清掉上次遗留的错误，WiFi 界面才肯停留在"正在连接" */
	xEventGroupClearBits(xEvtSystem, EVT_WIFI_CONNECTED | EVT_SNTP_OK | EVT_WEATHER_OK);

	if (!ESP32_Init())
	{
		ui_last_error = UI_ERR_ESP32_INIT;
		safe_printf("[TaskESP32] Init Faild\r\n");
		return false;
	}

	if (!ESP32_WifiInit())
	{
		ui_last_error = UI_ERR_ESP32_INIT;
		safe_printf("[TaskESP32] WIFI Init Faild\r\n");
		return false;
	}

	for (attempt = 1; attempt <= 3; attempt++)
	{
		safe_printf("[TaskESP32] WIFI connecting attempt %d/3\r\n", attempt);
		if (ESP32_ConnetWifi(WIFI_SSID, WIFI_PASSWORD))
		{
			safe_printf("[TaskESP32] WIFI connected\r\n");
			xEventGroupSetBits(xEvtSystem, EVT_WIFI_CONNECTED);
			break;
		}
		vTaskDelay(pdMS_TO_TICKS(2000));
	}
	if (attempt > 3)
	{
		ui_last_error = UI_ERR_WIFI_CONNECT;
		safe_printf("[TaskESP32] WIFI connect Failed\r\n");
		return false;
	}
	return true;
}

/* 天气抓取（受网络事务锁保护）：
 *   ① take(xMtxNet, 0) 非阻塞——OTA 事务持锁期间直接跳过，绝不与 OTA 并发
 *     操作 ESP32 模块（esp-at 单 UART 无并发能力，历史并发事故见开发文档）；
 *   ② ota_session / OTA UI 状态双重门控兜底。调用点：开机首抓(boot)、15min 定时器/UI(cmd) */
static void Weather_TryFetch(const char *tag)
{
	const char *response;

	if (!(xEventGroupGetBits(xEvtSystem) & EVT_WIFI_CONNECTED)) return;
	if (ota_session ||
	    g_ota_ui.state == OTA_UI_CHECKING ||
	    g_ota_ui.state == OTA_UI_DOWNLOADING ||
	    g_ota_ui.state == OTA_UI_VERIFYING)
	{
		safe_printf("[TaskESP32] weather skip (%s, OTA busy)\r\n", tag);
		return;
	}
	if (xSemaphoreTake(xMtxNet, 0) != pdTRUE)
	{
		safe_printf("[TaskESP32] weather skip (%s, net tx busy)\r\n", tag);
		return;
	}
	response = ESP32_HttpGetCIP(WEATHER_URL);
	if (response && Parse_SeniverseResponse(response, &ui_weather))
	{
		xEventGroupSetBits(xEvtSystem, EVT_WEATHER_OK);
		safe_printf("[ESP32] Weather %s OK\r\n", tag);
	}
	else
	{
		xEventGroupClearBits(xEvtSystem, EVT_WEATHER_OK);
		safe_printf("[ESP32] Weather %s Failed\r\n", tag);
	}
	xSemaphoreGive(xMtxNet);
}

/* 联网后一次性动作：SNTP 校时写 RTC + 开机首抓天气。
 * 安全前提：此刻 OTA 尚不可触发（MQTT 未订阅/UI 未就绪），且抓取全程持网络
 * 事务锁——其后任何 OTA 触发都会等本次抓取收尾（锁）或先复位模块（PreFlight） */
static void Esp32_TaskSyncAndWeather(void)
{
	ESP32_DataTime t;
	int i;

	if (!ESP32_SntpInit())
	{
		ui_last_error = UI_ERR_SNTP;
		safe_printf("[TaskESP32] SNTP Init Failed\r\n");
	}
	else
	{
		for (i = 0; i < 5; i++)
		{
			vTaskDelay(pdMS_TO_TICKS(2000));
			if (ESP32_SntpGetTime(&t) && t.year >= 2026)
			{
				RTC_TimeTypeDef rt;
				RTC_DateTypeDef rd;
				rt.RTC_Hours   = t.hour;
				rt.RTC_Minutes = t.minute;
				rt.RTC_Seconds = t.second;
				rd.RTC_Year    = t.year - 2000;
				rd.RTC_Month   = t.month;
				rd.RTC_Date    = t.day;
				rd.RTC_WeekDay = t.weekday;
				MyRTC_SetTime(&rt, &rd);

				ui_clock = t;
				xEventGroupSetBits(xEvtSystem, EVT_SNTP_OK);
				safe_printf("[TaskESP32] SNTP Sync OK\r\n");
				break;
			}
		}
		if (!(xEventGroupGetBits(xEvtSystem) & EVT_SNTP_OK))	
		{
			ui_last_error = UI_ERR_SNTP;
			safe_printf("[TaskESP32] SNTP Sync Failed\r\n");
		}
	}

		/* 开机首抓（恢复）：天气只在无 OTA 事务时执行——Weather_TryFetch 三重门控
 * （xMtxNet 事务锁 + ota_session + OTA UI 状态）；若用户先触发 OTA，vTaskOTA
 * 会先取锁等本次抓取收尾再 PreFlight 复位模块，残留缓冲一并清除 */
	Weather_TryFetch("boot");

}

/*===========================================================================
 *  MQTT 接入 OneNET：连接 + 订阅（OTA 推送 / 属性下发 / 上报回复）
 *  成功后控制台设备显示"在线"；平台可主动推送升级任务
 *===========================================================================*/
static void Mqtt_Connect(void)
{
	char topic[96];

	if (!ESP32_MqttSetup(ONENET_DEV_NAME, ONENET_PRO_ID, ONENET_MQTT_TOKEN))
	{
		safe_printf("[MQTT] usercfg fail\r\n");
		return;
	}
	if (!ESP32_MqttConnect(ONENET_MQTT_HOST, ONENET_MQTT_PORT))
	{
		safe_printf("[MQTT] connect fail\r\n");
		return;
	}

	snprintf(topic, sizeof(topic), MQTT_TOPIC_OTA_PUSH, ONENET_PRO_ID, ONENET_DEV_NAME);
	ESP32_MqttSub(topic);
	snprintf(topic, sizeof(topic), MQTT_TOPIC_PROP_SET, ONENET_PRO_ID, ONENET_DEV_NAME);
	ESP32_MqttSub(topic);
	snprintf(topic, sizeof(topic), MQTT_TOPIC_POST_REPLY, ONENET_PRO_ID, ONENET_DEV_NAME);
	ESP32_MqttSub(topic);

	safe_printf("[MQTT] online & subscribed\r\n");
}

/*===========================================================================
 *  TaskESP32（优先级3）：连接阶段 + 命令循环的可重入结构
 *  首次上电与每次重连命令都从连接阶段重跑；失败不退出，等下次重试
 *===========================================================================*/
static void vTaskESP32(void *pvparameters)
{
	(void)pvparameters;
	esp32_cmd_t cmd;
	bool reconnect;

	for (;;)
	{
		/* ===== 阶段1：连接（上电 / 重试共用），重连全套约需 10~60 秒 ===== */
		if (Esp32_TaskConnect())
		{
			Esp32_TaskSyncAndWeather();	/* SNTP 校时 + 首次天气 */
			Mqtt_Connect();				/* 接入 OneNET：在线状态 + OTA 推送订阅 */
		}

		/* ===== 阶段2：命令循环，直到收到重连命令 ===== */
		xQueueReset(xQueueCmd);	/* 丢弃连接阶段堆积的命令，避免旧重连命令连环触发 */
		reconnect = false;
		while (!reconnect)
		{
			/* MQTT 下发消息轮询：1s 无命令时检查一次（平台推送 OTA 任务直接触发升级） */
			if (xSemaphoreTake(xSemMQTT, 0) == pdTRUE)
			{
				safe_printf("[MQTT] recv: %s\r\n", ESP32_MqttMsg());
				if (strstr(ESP32_MqttMsg(), "ota") != NULL ||
				    strstr(ESP32_MqttMsg(), "\"tid\"") != NULL)
				{
					esp32_cmd_t c = CMD_OTA_DOWNLOAD;
					xQueueSend(xQueueOTA, &c, 0);    /* 转交独立 OTA 任务 */
				}
			}

			if (xQueueReceive(xQueueCmd, &cmd, pdMS_TO_TICKS(1000)) != pdTRUE)
				continue;

			switch (cmd)
			{
			case CMD_SNTP_SYNC:
				if (xEventGroupGetBits(xEvtSystem) & EVT_WIFI_CONNECTED)
				{
					if (ota_session) break;          /* OTA 会话期间禁校时 */
					{
					ESP32_DataTime t;
					if (ESP32_SntpGetTime(&t) && t.year >= 2026)
					{
						RTC_TimeTypeDef rt;
						RTC_DateTypeDef rd;
						rt.RTC_Hours   = t.hour;
						rt.RTC_Minutes = t.minute;
						rt.RTC_Seconds = t.second;
						rd.RTC_Year    = t.year - 2000;
						rd.RTC_Month   = t.month;
						rd.RTC_Date    = t.day;
						rd.RTC_WeekDay = t.weekday;
						MyRTC_SetTime(&rt, &rd);

						ui_clock = t;
						xEventGroupSetBits(xEvtSystem, EVT_SNTP_OK);
						safe_printf("[TaskESP32] SNTP Sync by cmd OK\r\n");
					}
					else
					{
						safe_printf("[TaskESP32] SNTP Sync by cmd Failed\r\n");
					}
					}
				}
				break;

			case CMD_WEATHER_FETCH:
				Weather_TryFetch("cmd");	/* 门控+抓取收敛（见上） */
				break;

			case CMD_WIFI_RECONNECT:
				if (ota_session) break;          /* OTA 会话期间禁重连 */
				safe_printf("[TaskESP32] reconnect requested\r\n");
				reconnect = true;					//跳出命令循环，回到连接阶段重跑全套
				break;

			case CMD_OTA_DOWNLOAD:
			{
				esp32_cmd_t c = CMD_OTA_DOWNLOAD;
				xQueueSend(xQueueOTA, &c, 0);    /* 转交独立 OTA 任务（结构隔离） */
			}
			break;

			default:
				break;
			}
		}
	}
}

/*===========================================================================
 *  TaskOTA（优先级4）：独立 OTA 任务
 *  与网络任务结构性隔离：OTA 运行期间（含阻塞等待的间隙）vTaskESP32
 *  即使被调度也不会执行天气/校时/重连（ota_session 拦截），UART 数据流
 *  不再可能被其他事务的迟到尾段污染
 *===========================================================================*/
static void vTaskOTA(void *pvparameters)
{
	(void)pvparameters;
	esp32_cmd_t cmd;

	for (;;)
	{
		if (xQueueReceive(xQueueOTA, &cmd, portMAX_DELAY) == pdTRUE)
		{
			ota_session = 1;
			/* 取网络事务锁：若天气抓取正在进行则等其收尾（正常 ≤3s，
			 * 超过 30s 放弃本轮），此后整个 OTA 事务独占 ESP32 模块 */
			if (xSemaphoreTake(xMtxNet, pdMS_TO_TICKS(30000)) != pdTRUE)
			{
				safe_printf("[OTA] net lock timeout, abort this round\r\n");
				OTA_NotifyFail();
				ota_session = 0;
				continue;
			}
			if (xEventGroupGetBits(xEvtSystem) & EVT_WIFI_CONNECTED)
			{
				uint8_t pf;                          /* PreFlight 整体重试一轮（复位后入网慢） */
				for (pf = 0; pf < 2; pf++)
				{
					if (pf)
					{
						safe_printf("[OTA] preflight fail, retry recovery\r\n");
						vTaskDelay(pdMS_TO_TICKS(3000));
					}
					if (ESP32_OTA_PreFlight(WIFI_SSID, WIFI_PASSWORD)) break;
				}
				if (pf < 2)
				{
					OTA_Run(WIFI_SSID, WIFI_PASSWORD);	/* 查询+分片下载+MD5+READY */
				}
				else
				{
					safe_printf("[OTA] preflight fail x2\r\n");
					OTA_NotifyFail();
				}
			}
			else
			{
				OTA_NotifyFail();
			}
			xSemaphoreGive(xMtxNet);
			ota_session = 0;
		}
	}
}

/*===========================================================================
 *  TaskUI（优先级2）：LVGL 渲染任务
 *  时基：SysTick 1ms -> lv_tick_inc；LVGL 非线程安全，lv_* 只在本任务调用
 *===========================================================================*/
static void vTaskUI(void *pvparameters)
{
	(void)pvparameters;

	lv_init();
	lv_port_disp_init();		/* LCD + 绘制缓冲（模式1：240x10 行）+ flush 对接 */
	lv_port_indev_init();		/* FT6336 触摸 indev */
	LvApp_Init();
	
	for (;;)
	{
		/* EXTI 通知只作清位；按键触发改用电平轮询，避免复查窗口竞态 */
		uint32_t notify_val;
		xTaskNotifyWait(0, 0xFFFFFFFF, &notify_val, 0);

		/* DHT11 室内数据：Sensor 任务每 2s 覆盖式入队，零等待取最新（天气页读取） */
	{
		dht11_data_t dht;
		if (xQueueReceive(xQueueDHT, &dht, 0) == pdTRUE)
		{
			ui_dht = dht;
			ui_dht_valid = true;
		}
	}
	LvApp_Tick();                            /* 状态栏/BOOT 状态刷新 */
	lv_timer_handler();					/* LVGL 全部渲染/事件在此驱动 */
	vTaskDelay(pdMS_TO_TICKS(5));		/* 5ms 周期，tick 由中断钩子供 */

	}
}


/*===========================================================================
 *  软件定时器回调：只发命令入队，不做耗时操作
 *===========================================================================*/
static void vTmrWeather_Cb(TimerHandle_t xTimer)
{
	esp32_cmd_t cmd = CMD_WEATHER_FETCH;
	(void)xTimer;
	xQueueSend(xQueueCmd, &cmd, 0);
}

static void vTmrSNTP_Cb(TimerHandle_t xTimer)
{
	esp32_cmd_t cmd = CMD_SNTP_SYNC;
	(void)xTimer;
	xQueueSend(xQueueCmd, &cmd, 0);
}

/*===========================================================================
 *  FreeRTOS 钩子：栈溢出 / 堆耗尽报警（configCHECK_FOR_STACK_OVERFLOW = 2）
 *===========================================================================*/
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
	(void)xTask;
	safe_printf("[FATAL] stack overflow in: %s\r\n", pcTaskName);
	for (;;);
}

void vApplicationMallocFailedHook(void)
{
	safe_printf("[FATAL] FreeRTOS heap exhausted!\r\n");
	for (;;);
}

/*===========================================================================
 *  创建全部同步对象 / 定时器 / 任务
 *===========================================================================*/
void AppTasks_Create(void)
{
	/* 同步对象 */
	xSemACK    = xSemaphoreCreateBinary();
	xMtxPrintf = xSemaphoreCreateMutex();
	xMtxLCD    = xSemaphoreCreateMutex();
	xQueueDHT  = xQueueCreate(1, sizeof(dht11_data_t));	/* 深度1：配合 xQueueOverwrite */
	xQueueCmd  = xQueueCreate(5, sizeof(esp32_cmd_t));
	xEvtSystem = xEventGroupCreate();
	xSemIPD    = xSemaphoreCreateCounting(8, 0);	/* +IPD 帧：计数信号量 */
	xSemMQTT   = xSemaphoreCreateCounting(4, 0);	/* MQTT 下发帧 */
	xMtxNet    = xSemaphoreCreateMutex();	/* ESP32 网络事务锁（天气 vs OTA） */

	/* 软件定时器 */
	xTmrWeather = xTimerCreate("Weather", pdMS_TO_TICKS(15UL * 60 * 1000), pdTRUE, NULL, vTmrWeather_Cb);
	xTmrSNTP    = xTimerCreate("SNTP",    pdMS_TO_TICKS(60UL * 60 * 1000), pdTRUE, NULL, vTmrSNTP_Cb);
	xTimerStart(xTmrWeather, 0);
	xTimerStart(xTmrSNTP, 0);

	/* 任务：优先级 Sensor > ESP32 > UI */
	xTaskCreate(vTaskSensor, "Sensor", 256, NULL, 4, NULL);
	xTaskCreate(vTaskESP32, "ESP32", 1024, NULL, 3, &xTaskESP32_Handle);	/* OTA 下载需 ~1.6KB 栈 */
	xQueueOTA = xQueueCreate(1, sizeof(esp32_cmd_t));
	xTaskCreate(vTaskOTA,    "OTA",   1024, NULL, 4, NULL);   /* 独立 OTA 任务：优先级高于网络任务 */
	xTaskCreate(vTaskUI,     "UI",    1024, NULL, 2, &xTaskUI_Handle);	
}
