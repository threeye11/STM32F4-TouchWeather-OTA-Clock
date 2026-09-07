#include "tp_cal.h"
#include "lcd.h"
#include "ft6336.h"
#include <board.h>       /* delay_ms */
#include "bsp_uart.h"   
#include <stdio.h>       /* printf */

/* 校准系数默认值 = 恒等映射。
 * 本模块上报的坐标已是面板分辨率（0~240 x 0~320），不是寄存器位宽暗示的 0~1023；
 * TP_Cal_Run 的 4 点拟合会覆盖这里的值做精调。 */
static float cal_x_scale = 1.0f;
static float cal_y_scale = 1.0f;
static float cal_x_off   = 0.0f;
static float cal_y_off   = 0.0f;

/* 4 个校准点：屏幕四角，离边 20px */
#define CAL_MARGIN  20
static const u16 cal_pts[4][2] =
{
	{CAL_MARGIN,               CAL_MARGIN},
	{LCD_W - CAL_MARGIN - 1,   CAL_MARGIN},
	{CAL_MARGIN,               LCD_H - CAL_MARGIN - 1},
	{LCD_W - CAL_MARGIN - 1,   LCD_H - CAL_MARGIN - 1},
};

/* 画十字准星（横竖两条短线拼成） */
static void draw_cross(u16 x, u16 y, u16 color)
{
	LCD_DrawHLine(x - 8, y, 17, color);
	LCD_DrawVLine(x, y - 8, 17, color);
}

/* 等待一次"按下→松开"，松开时记录原始坐标 */
static void wait_tap(u16 *rx, u16 *ry)
{
	u8 pressed = 0;
	while (1)
	{
		if (FT6336_Scan())
		{
			*rx = tp_x;
			*ry = tp_y;
			pressed = 1;
		}
		else if (pressed)
		{
			return;
		}
		delay_ms(10);
	}
}

/* 阻塞式 4 点校准：依次点击四角准星，拟合 X/Y 偏移+缩放 */
void TP_Cal_Run(void)
{
	u16 rx[4], ry[4];
	u8  i;
	u8  ok = 1;

	LCD_Clear(WHITE);

	for (i = 0; i < 4; i++)
	{
		draw_cross(cal_pts[i][0], cal_pts[i][1], BLUE);
		wait_tap(&rx[i], &ry[i]);
		draw_cross(cal_pts[i][0], cal_pts[i][1], WHITE);   /* 擦掉准星 */
		safe_printf("[CAL] p%d screen=(%d,%d) raw=(%d,%d)\r\n",
		            i, cal_pts[i][0], cal_pts[i][1], rx[i], ry[i]);	}

	/* 线性拟合：X 用上下两行斜率平均，Y 用左右两列平均 */
	float span_x = (float)(cal_pts[1][0] - cal_pts[0][0]);
	float span_y = (float)(cal_pts[2][1] - cal_pts[0][1]);
	float scale_x = (float)((rx[1] - rx[0]) + (rx[3] - rx[2])) / (2.0f * span_x);
	float scale_y = (float)((ry[2] - ry[0]) + (ry[3] - ry[1])) / (2.0f * span_y);

	/* 偏差过大（斜率为负或过小）则保留旧系数 */
	if (scale_x <= 0.5f || scale_y <= 0.5f)
	{
		ok = 0;
	}
	else
	{
		cal_x_scale = scale_x;
		cal_y_scale = scale_y;
		cal_x_off   = (float)(rx[0] + rx[2]) / 2.0f - cal_x_scale * (float)cal_pts[0][0];
		cal_y_off   = (float)(ry[0] + ry[1]) / 2.0f - cal_y_scale * (float)cal_pts[0][1];
	}

	safe_printf("[CAL] %s scale=(%d,%d)*0.001 off=(%d,%d)\r\n",
	            ok ? "OK" : "FAIL keep old",
	            (int)(cal_x_scale * 1000.0f), (int)(cal_y_scale * 1000.0f),
	            (int)cal_x_off, (int)cal_y_off);

	LCD_Clear(WHITE);
}

/* 按校准系数转屏幕坐标（自动限幅） */
void TP_Cal_Convert(u16 raw_x, u16 raw_y, u16 *sx, u16 *sy)
{
	int32_t x = (int32_t)(((float)raw_x - cal_x_off) / cal_x_scale + 0.5f);
	int32_t y = (int32_t)(((float)raw_y - cal_y_off) / cal_y_scale + 0.5f);

	if (x < 0)         x = 0;
	if (x > LCD_W - 1) x = LCD_W - 1;
	if (y < 0)         y = 0;
	if (y > LCD_H - 1) y = LCD_H - 1;

	*sx = (u16)x;
	*sy = (u16)y;
}
