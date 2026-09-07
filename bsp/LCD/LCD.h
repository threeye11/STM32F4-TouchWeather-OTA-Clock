#ifndef _LCD_H_
#define _LCD_H_

#include "stm32f4xx.h"
#include "image.h"

/*  LCD_GPIO宏定义 */
#define LCD_BL_ON()  	 GPIO_SetBits(GPIOE , GPIO_Pin_8);
#define LCD_BL_OFF() 	 GPIO_ResetBits(GPIOE , GPIO_Pin_8);
#define LCD_CS_CLR()   GPIO_ResetBits(GPIOA, GPIO_Pin_4)
#define LCD_CS_SET()   GPIO_SetBits(GPIOA, GPIO_Pin_4)
#define LCD_DC_CLR()   GPIO_ResetBits(GPIOE, GPIO_Pin_10)   // DC=0 命令
#define LCD_DC_SET()   GPIO_SetBits(GPIOE, GPIO_Pin_10)     // DC=1 数据
#define LCD_RST_CLR()  GPIO_ResetBits(GPIOE, GPIO_Pin_12)
#define LCD_RST_SET()  GPIO_SetBits(GPIOE, GPIO_Pin_12)

/* RGB565 颜色宏定义 */ 
#define WHITE   0xFFFF
#define BLACK   0x0000
#define RED     0xF800
#define GREEN   0x07E0
#define BLUE    0x001F
#define YELLOW  0xFFE0
#define CYAN    0x07FF
#define MAGENTA 0xF81F
#define LIGHT_GRAY2 0xD69A
#define LIGHT_CYAN 0xC79F				//淡青色
#define ORANGE 0xFBE0     
#define LIGHT_LAVENDER 0xE6F7		//浅淡紫

/* 	LCD屏幕大小宏定义 */ 
#define LCD_W 240
#define LCD_H 320

void LCD_GPIO_init(void);
void LCD_WriteCmd(uint8_t CMD);
void LCD_WriteData(uint8_t Data);
void LCD_Write_16Byte(uint16_t Data);
void LCD_WriteReg(uint8_t Adr, uint8_t Val);
void LCD_RESET(void);
void LCD_SetArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1 );
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color );
void LCD_Clear(uint16_t color);
void LCD_SetDirection(uint8_t direction);
void LCD_DrawHLine(uint16_t x ,uint16_t y,uint16_t len ,uint16_t color);
void LCD_DrawVLine(uint16_t x ,uint16_t y,uint16_t len ,uint16_t color);
void LCD_DrawRect(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color);
void LCD_DrawHollowrect(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color);
void LCD_ShowImage(uint16_t x, uint16_t y, const ImageShow *image);
void LCD_init(void);
void LCD_DrawRect_DMA(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color);
void LCD_ShowImage_DMA(uint16_t x, uint16_t y, const ImageShow *image);
void LCD_FlushArea_DMA(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint8_t *pixel_buf);	/* LVGL 专用：渲染缓冲整块刷入窗口（disp_flush 回调） */

extern volatile uint16_t lcd_width, lcd_height;
extern volatile uint8_t lcd_dir;


#endif

