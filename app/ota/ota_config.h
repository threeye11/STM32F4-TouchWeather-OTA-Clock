#ifndef __OTA_CONFIG_H
#define __OTA_CONFIG_H


#define ONENET_HOST     "iot-api.heclouds.com"
#define ONENET_PORT     443                     /* HTTPS: ESP32 via AT+CIPSTART="SSL" */

#define ONENET_PRO_ID   "YOUR_PRODUCT_ID"       /* TODO: OneNET 产品 ID */
#define ONENET_DEV_NAME "YOUR_DEVICE_NAME"      /* TODO: 设备名称（字符串） */
#define ONENET_USER_ID  "YOUR_USER_ID"          /* TODO: 用户 ID（控制台账号信息） */

/* 融合版鉴权串：res=products/{产品ID} + 产品 access_key 签名 */
#define ONENET_AUTH     "YOUR_ONENET_AUTH_TOKEN"   /* TODO: 由 onenet_token.py 生成 */

/* 发版版本号：每次发版改这里。check 时设备上报的就是它，
 * 必须与 OneNET 升级包的目标版本号【不同】（同版本平台不下发任务），
 * 升级成功后设备运行的就是包里那个版本（其 FW_VERSION = 包版本）。
 * 界面（OTA 页）会显示它——升级成功与否肉眼可见 */
#define FW_VERSION      "1.1.0"

/* 兜底版本号：flags 里没有记录时用于 check */
#define OTA_DEFAULT_VER "V1.0.0"

/* 分片大小：对齐 W25Q 页/扇区，兼顾串口缓冲 */
#define OTA_CHUNK_SIZE  1536u   /* 片长<2048-头长(~1677)：esp-at 流在绝对偏移 2048 丢 1 字节，小片使丢失点落入 pad 区 */

/* ===== MQTT 接入 ===== */
#define ONENET_MQTT_HOST  "mqtts.heclouds.com"
#define ONENET_MQTT_PORT  1883
#define ONENET_MQTT_TOKEN "YOUR_ONENET_MQTT_TOKEN"  

/* OTA 推送/物模型主题（%s = 产品ID / 设备名） */
#define MQTT_TOPIC_OTA_PUSH   "$sys/%s/%s/ota/upgrade"
#define MQTT_TOPIC_PROP_SET   "$sys/%s/%s/thing/property/set"
#define MQTT_TOPIC_POST_REPLY "$sys/%s/%s/thing/property/post/reply"

#endif /* __OTA_CONFIG_H */
