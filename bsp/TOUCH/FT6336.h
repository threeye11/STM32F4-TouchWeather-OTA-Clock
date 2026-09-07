#ifndef __FT6336_H
#define __FT6336_H

#include "stm32f4xx.h"
#include <board.h>

#define CTP_MAX_TOUCH   2

/* 全局触摸状态：tp_x/tp_y 为按 lcd_dir 映射后的触点坐标。
 * 注意：本模块 FT6336G 固件按面板分辨率上报（约 0~240 x 0~320），
 * 并不是寄存器位宽暗示的 0~1023！tp_sta bit0=按下 */
extern volatile u16 tp_x, tp_y;
extern volatile u8  tp_sta;

/* 触摸控制引脚（PE1=RST 推挽输出，PE0=INT 输入+上拉） */
#define FT_RST_CLR()    GPIO_ResetBits(GPIOE, GPIO_Pin_1)   // PE1
#define FT_RST_SET()    GPIO_SetBits(GPIOE, GPIO_Pin_1)
#define FT_INT_READ()   GPIO_ReadInputDataBit(GPIOE, GPIO_Pin_0)  // PE0（可选）

/* I2C 读写命令 */
#define FT_CMD_WR       0x70        // 7bit 地址 0x38 << 1
#define FT_CMD_RD       0x71

/* 关键寄存器 */
#define FT_REG_NUM_FINGER   0x02    // 触点数量
#define FT_TP1_REG          0x03    // 第 1 点坐标起始地址
#define FT_TP2_REG          0x09    // 第 2 点坐标起始地址
#define FT_ID_G_CIPHER_MID  0x9F    // 芯片代号中字节，默认 0x26
#define FT_ID_G_CIPHER_HIGH 0xA3    // 芯片代号高字节，默认 0x64
#define FT_ID_G_FOCALTECH_ID 0xA8   // VENDOR ID，默认 0x11
#define FT_ID_G_THGROUP     0x80    // 触摸阈值
#define FT_ID_G_PERIODACTIVE 0x88   // 扫描周期

void FT6336_WR_Reg(u8 reg, u8 *buf, u8 len);
u8   FT6336_RD_Reg(u8 reg, u8 *buf, u8 len);
u8   FT6336_Init(void);
u8   FT6336_Scan(void);

#endif
