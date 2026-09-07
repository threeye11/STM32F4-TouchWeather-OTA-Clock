# STM32F4‑TouchWeather‑OTA‑Clock


基于 STM32F407VET6 + FreeRTOS + LVGL 的桌面智能天气时钟。

## 功能

- DHT11 室内温湿度采集与显示
- ESP32-C3(AT 固件)联网:SNTP 校时 + 心知天气(Seniverse)实时天气 + Wi-Fi 状态显示
- LVGL 8.3 图形界面:开机动画、主界面时钟、天气页、网络状态页、OTA 升级页,FT6336 电容触摸
- OneNET 融合版 OTA 远程升级(BootLoader + W25Q64 分片缓存 + MD5 校验)
- RTC 实时时钟(联网后 SNTP 自动校时)、锂电池电量检测(ADC)

## 目录结构

```
STM32F407_ProjectTemplate/
├── Boot/           # BootLoader 工程(Keil MDK5)
├── app/            # 应用代码(main、FreeRTOS 任务、LVGL 页面、OTA、天气解析)
│   └── ota/        # OTA 配置与实现(ota_config.h 需配置账号凭据)
├── board/          # 板级初始化
├── bsp/            # 外设驱动(LCD/ESP32/DHT11/RTC/FLASH/TOUCH/ADC/UART)
├── module/         # 中断服务与 FreeRTOS 配置
├── libraries/      # STM32F4 标准外设库
├── Third_Lib/      # LVGL 8.3.10、FreeRTOS
├── project/        # 应用工程(Keil MDK5)
└── docs/           # 开发文档与工具
```

## 使用前必填(账号凭据,已替换为占位符)

本仓库不包含任何个人密钥。克隆后请在以下位置填入你自己的参数:

1. **Wi-Fi 与天气 API key** — `app/page/lv_app.h`
   - `WIFI_SSID` / `WIFI_PASSWORD`:你的无线路由器凭据
   - `WEATHER_URL`:心知天气 `https://api.seniverse.com/v3/weather/now.json?key=你的key&...`
     (免费注册:https://www.seniverse.com)
2. **OneNET OTA 账号凭据** — `app/ota/ota_config.h`
   - `ONENET_PRO_ID` / `ONENET_DEV_NAME` / `ONENET_USER_ID`:你的 OneNET 产品/设备/账号 ID
   - `ONENET_AUTH`、`ONENET_MQTT_TOKEN`:用仓库内通用工具生成:
     ```
     python docs/tools/onenet_token.py ota  <产品ID> <产品级access_key>
     python docs/tools/onenet_token.py mqtt <产品ID> <产品级access_key>
     ```
   - 发版前记得递增 `FW_VERSION`

## 编译与烧录

- 开发环境:Keil MDK5(需 STM32F4xx 标准外设库、FreeRTOS、LVGL 源码已随仓库提供)
- 两个独立工程:
  - `Boot/Project/Boot.uvprojx` — BootLoader(默认下载到 0x08000000,APP 起始 0x08008000)
  - `project/MDK(V5)/Project.uvprojx` — 应用程序
- 首次使用请在 Keil 中确认魔术棒里的 Include Paths 与芯片型号(STM32F407VETx)

## 参考文档

`docs/` 下为开发过程记录:OTA 升级流程与故障排查手册、FPU 上下文切换 Bug 复盘、SPI+DMA LCD 调试记录、LVGL UI 开发文档等。
