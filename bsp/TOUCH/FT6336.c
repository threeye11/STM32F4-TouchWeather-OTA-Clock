/****************************************************************************************************
//=========================================电源接线================================================//
//     LCD模块                 STM32F407VET6
//      VCC          接        DC5V/3.3V      //电源
//      GND          接          GND          //电源地
//=======================================液晶屏数据线接线==========================================//
//本模块默认数据总线类型为SPI总线
//     LCD模块                STM32F407
//    SDI(MOSI)      接          PA7          //液晶屏SPI总线数据写信号
//    SDO(MISO)      接          PA6          //液晶屏SPI总线数据读信号，如果不需要读，可以不接线
//=======================================液晶屏控制线接线==========================================//
//     LCD模块                          STM32F407
//       LED         接          PE8         //液晶屏背光控制信号，如果不需要控制，接5V或3.3V
//       SCK         接          PA5          //液晶屏SPI总线时钟信号
//  LCD_RS/LCD_DC    接          PE10         //液晶屏数据/命令控制信号
//     LCD_RST       接          PE12         //液晶屏复位控制信号
//     LCD_CS        接          PA4         //液晶屏片选控制信号
//=========================================触摸屏触接线=========================================//
//	   LCD模块                STM32F407
//     CTP_INT       接          PE0          //电容触摸屏中断信号
//     CTP_SDA       接          PB9         //电容触摸屏IIC数据信号
//     CTP_RST       接          PE1          //电容触摸屏复位信号
//     CTP_SCL       接          PB8          //电容触摸屏IIC时钟信号
**************************************************************************************************/


#include "ctpiic.h"
#include "ft6336.h"
#include "lcd.h"
#include "bsp_uart.h"   
#include <stdio.h>

/* 全局坐标 */
volatile u16 tp_x = 0, tp_y = 0;
volatile u8  tp_sta = 0;    // bit0: 按下标志

/* 传输层对接软件 I2C */
void FT6336_WR_Reg(u8 reg, u8 *buf, u8 len) { I2C1_WriteReg(reg, buf, len); }
u8 FT6336_RD_Reg(u8 reg, u8 *buf, u8 len) { return I2C1_ReadReg(reg, buf, len); }

uint8_t FT6336_Init(void)
{
		uint8_t temp = 0;
		uint8_t rd = 0;
		I2C1_init();

		FT_RST_CLR();             // 复位触摸 IC
    delay_ms(10);
    FT_RST_SET();             // 释放复位
    delay_ms(500);            // 手册：复位后需时间开始上报数据

    // 读 ID 校验（三连查，都通过才说明接线正确）
    rd = FT6336_RD_Reg(FT_ID_G_FOCALTECH_ID, &temp, 1);
    safe_printf("[TP] RD 0xA8 ret=%u val=0x%02X want=0x11\r\n", rd, temp);
    if (temp != 0x11) return 1;          // VENDOR ID
    rd = FT6336_RD_Reg(FT_ID_G_CIPHER_MID, &temp, 1);
    safe_printf("[TP] RD 0x9F ret=%u val=0x%02X want=0x26\r\n", rd, temp);
    if (temp != 0x26) return 1;
    rd = FT6336_RD_Reg(FT_ID_G_CIPHER_HIGH, &temp, 1);
    safe_printf("[TP] RD 0xA3 ret=%u val=0x%02X want=0x64\r\n", rd, temp);
    if (temp != 0x64) return 1;

    // 可选调参（默认值即可，这里注释备用）：
    // temp = 40; FT6336_WR_Reg(FT_ID_G_THGROUP, &temp, 1);   // 灵敏度，越小越灵敏
    // temp = 12; FT6336_WR_Reg(FT_ID_G_PERIODACTIVE, &temp, 1);

    return 0;
}

/* 扫描一次触摸：0=无触摸，1=有触摸 */
u8 FT6336_Scan(void)
{
    u8 buf[4], num;

    FT6336_RD_Reg(FT_REG_NUM_FINGER, &num, 1);   // 1. 读触点数量
    if (num == 0 || num > CTP_MAX_TOUCH)
    {
        tp_sta = 0;
        return 0;
    }

    FT6336_RD_Reg(FT_TP1_REG, buf, 4);           // 2. 读第 1 点 4 字节

    // 3. 组合坐标（先取原始物理坐标）
    u16 raw_x = ((buf[0] & 0x0F) << 8) | buf[1];
    u16 raw_y = ((buf[2] & 0x0F) << 8) | buf[3];

    // 4. 无效点过滤：TA 事件编码 00=按下，10=保持接触，01=抬起，11=无事件
//    手指按住不动时持续上报"接触(10)"，旧代码只放行"按下(00)"导致按住即判松手
    if ((buf[0] & 0xC0) == 0x40 || (buf[0] & 0xC0) == 0xC0 ||
        (raw_x == 0 && raw_y == 0))
    {
        tp_sta = 0;
        return 0;
    }

    // 5. 按 LCD 方向映射（见第 4 步）
    switch (lcd_dir)          // 0/1/2/3 = 0°/90°/180°/270°，与 LCD_init 里一致
    {
        case 0:  tp_x = raw_x;                tp_y = raw_y;                        break;
        case 1:  tp_y = lcd_height - raw_x;   tp_x = raw_y;                        break;
        case 2:  tp_x = lcd_width  - raw_x;   tp_y = lcd_height - raw_y;           break;
        case 3:  tp_y = raw_x;                tp_x = lcd_width  - raw_y;           break;
        default: tp_x = raw_x;                tp_y = raw_y;                        break;
    }
    tp_sta = 1;
    return 1;
}
