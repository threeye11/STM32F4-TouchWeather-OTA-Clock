#ifndef _ESP32_H_
#define _ESP32_H_

#include "stm32f4xx.h"
#include <stdbool.h>
#include <stdint.h>

/* FreeRTOS 信号量句柄（ISR -> 任务通知 ACK） */
#include "FreeRTOS.h"
#include "semphr.h"
extern SemaphoreHandle_t xSemACK;
extern SemaphoreHandle_t xSemIPD;      /* +IPD 数据帧信号量（ISR -> OTA 下载解析） */
extern SemaphoreHandle_t xSemMQTT;     /* +MQTTSUBRECV 下发帧信号量 */

/* AT应答状态 */
typedef enum {
	AT_ACK_NONE = 0,
	AT_ACK_OK,
	AT_ACK_ERROR,
	AT_ACK_BUSY,
	AT_ACK_READY,
	AT_ACK_PROMPT,		/* AT+CIPSEND 后的 ">" 提示符 */
} ESP32_ACK;

/* SNTP 网络时间 */
typedef struct {
	uint16_t year;
	uint8_t month;
	uint8_t day;
	uint8_t hour;
	uint8_t minute;
	uint8_t second;
	uint8_t weekday;		/* 周一 = 1，周二 = 2 */
} ESP32_DataTime;

/* ACK匹配表 */
typedef struct {
	ESP32_ACK ack;
	const char *str;
} AckMatch;

static const AckMatch ack_table[] = {
	{AT_ACK_PROMPT, ">"},
	{AT_ACK_OK,    "OK\r\n"},
	{AT_ACK_ERROR, "ERROR\r\n"},
	{AT_ACK_BUSY,  "busy p...\r\n"},
	{AT_ACK_READY, "ready\r\n"},
};

void USART2_DMA_Init(void);
void DMA1_Stream6_IRQHandler(void);
void ESP32_DMA_Send(const char *data, uint32_t len);
void USART2_Init(uint32_t __Baud);
void USART2_IRQHandler(void);
bool ESP32_WriteCmd(const char *cmd, uint32_t timeout_ms);
bool ESP32_WaitReady(uint32_t timeout_ms);
const char *ESP32_GetResponse(void);
uint16_t ESP32_GetRxLen(void);                      /* 最近一帧接收长度 */
const char *ESP32_GetFrame(void);                   /* +IPD 帧队列队头（OTA 下载用） */
uint16_t    ESP32_GetFrameLen(void);
void        ESP32_PopFrame(void);                   /* 消费队头帧 */
uint32_t    ESP32_FramesPending(void);              /* 未消费帧数（收满即停须以排空为前提） */
void        ESP32_FrameFlush(void);                 /* 清空帧队列（事务开始前） */
void        ESP32_DmuxMode(const char *token, char term); /* 解复用令牌："+IPD,"/":"+HTTPCLIENT:"/"," */
void        ESP32_SendLine(const char *s);          /* 裸发一行命令（不等应答） */
bool        ESP32_CmdWithPayload(const char *cmd, const char *data, uint16_t len);
const char *ESP32_HttpGetCIP(const char *url);   /* CIP 通道 HTTPS GET（不受 HTTPCLIENT 干扰） */
bool        ESP32_OTA_PreFlight(const char *ssid, const char *pwd); /* OTA 前：复位模块+重连 WiFi */
void        ESP32_RxDiag(void);                     /* RX 黑匣子转储（联调用） */
bool ESP32_TcpConnect(const char *host, uint16_t port, const char *proto); /* proto: "TCP"/"SSL" */
bool ESP32_TcpSend(const char *data, uint16_t len); /* CIPSEND + 数据 + SEND OK */
bool ESP32_TcpClose(void);
bool ESP32_MqttSetup(const char *client, const char *user, const char *pass);
bool ESP32_MqttConnect(const char *host, uint16_t port);
bool ESP32_MqttSub(const char *topic);
bool ESP32_MqttPub(const char *topic, const char *data);
const char *ESP32_MqttMsg(void);                    /* 最近一帧下发消息 */
uint8_t     ESP32_MqttLinkUp(void);                 /* MQTT 链路状态（URC 维护） */
bool ESP32_Init(void);
bool ESP32_ConnetWifi(const char *ssid, const char *pwd);
bool ESP32_WifiInit(void);
bool ESP32_SntpInit(void);
bool ESP32_SntpGetTime(ESP32_DataTime *date);
const char *ESP32_HttpGet(const char *url);

#endif
