/****************************************************************************************************
//=========================================电源接口================================================//
//     LCD模块                 STM32F407VET6
//      VCC          接        DC5V/3.3V      //电源
//      GND          接          GND          //电源地
//=======================================液晶数据线接口==========================================//
//该模块默认通信方式为SPI通信
//     LCD模块               	STM32F407
//    SDI(MOSI)      接          PA7          //液晶的SPI通信数据写信号
//    SDO(MISO)      接          PA6          //液晶的SPI通信数据读信号，不需要读可以不接
//=======================================液晶控制线接口==========================================//
//     LCD模组 					      STM32F407
//       LED         接          PE8         //液晶背光控制信号，需要接限流电阻（5V转3.3V）
//       SCK         接          PA5          //液晶的SPI通信时钟信号
//  LCD_RS/LCD_DC    接          PE10         //液晶命令/数据选择信号
//     LCD_RST       接          PE12         //液晶复位控制信号
//     LCD_CS        接          PA4         //液晶片选控制信号
//=========================================触摸屏数据线接口=========================================//
//	   LCD模组                STM32F407
//     CTP_INT       接          PE0          //触摸屏中断信号
//     CTP_SDA       接          PB9         //触摸屏IIC数据信号
//     CTP_RST       接          PE1          //触摸屏复位信号
//     CTP_SCL       接          PB8          //触摸屏IIC时钟信号
**************************************************************************************************/

#include "lcd.h"
#include "spi.h"
#include "image.h"

#include <stdio.h>

volatile uint16_t lcd_width ,lcd_height;
volatile uint8_t lcd_dir;

/* 初始化 PE8,PE10,PE12,PA4 */
void LCD_GPIO_init(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA ,ENABLE);
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE ,ENABLE);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed =GPIO_Speed_100MHz;
	GPIO_Init(GPIOA ,&GPIO_InitStructure);

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_8 | GPIO_Pin_10 |GPIO_Pin_12 ;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed =GPIO_Speed_100MHz;
	GPIO_Init(GPIOE ,&GPIO_InitStructure);

	//初始状态
	LCD_CS_SET();
	LCD_DC_SET();
	LCD_RST_SET();
	LCD_BL_ON();
}

/* 写命令 */
void LCD_WriteCmd(uint8_t CMD)
{
	LCD_CS_CLR();				// 选择屏幕
	LCD_DC_CLR();				// DC=0，命令模式
	SPI_SendByte(CMD);
	LCD_CS_SET();				// 释放屏幕
}

/* 写数据 */
void LCD_WriteData(uint8_t Data)
{
	LCD_CS_CLR();				// 选择屏幕
	LCD_DC_SET();				// DC=1，数据模式
	SPI_SendByte(Data);
	LCD_CS_SET();				// 释放屏幕
}

/* 写16位数据 */
void LCD_Write_16Byte(uint16_t Data)
{
	LCD_CS_CLR();								// 选择屏幕
	LCD_DC_SET();								// DC=1，数据模式
	SPI_SendByte(Data >> 8); 		// 发送高8位
	SPI_SendByte(Data & 0xff);	// 再发送低 8 位
	LCD_CS_SET();								// 释放屏幕
}

/* 写寄存器 */
void LCD_WriteReg(uint8_t Adr, uint8_t Val)
{
	LCD_WriteCmd(Adr);
	LCD_WriteData(Val);
}

/* 屏幕复位 */
/*void LCD_Reset(void)
{
	LCD_RST_SET();        // 初始高电平，上电后会自定拉高
  delay_ms(10);

  LCD_RST_CLR();        // 复位拉低
  delay_ms(1);        	// 手册要求 10us ~ 5ms，取中间值 1ms

  LCD_RST_SET();        // 释放复位
  delay_ms(5);        	// 手册 note 7，约 5ms 后可发命令
}
*/
void LCD_RESET(void)
{
	LCD_RST_SET();
	delay_ms(50);
	LCD_RST_CLR();
	delay_ms(100);
	LCD_RST_SET();
	delay_ms(50);
}

/* 设置显示区域 */
void LCD_SetArea(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1 )
{
	//Column Address Set (2Ah) --x
	LCD_WriteCmd(0x2A);
	LCD_WriteData(x0 >> 8);       // X 起始高字节
  LCD_WriteData(x0 & 0xFF);     // X 起始低字节
  LCD_WriteData(x1 >> 8);       // X 结束高字节
  LCD_WriteData(x1 & 0xFF);     // X 结束低字节

	//Page Address Set (2Bh)  --y
	LCD_WriteCmd(0x2B);
	LCD_WriteData(y0 >> 8);
  LCD_WriteData(y0 & 0xFF);
  LCD_WriteData(y1 >> 8);
  LCD_WriteData(y1 & 0xFF);

	//Memory Write (2Ch) 准备写显示数据
	LCD_WriteCmd(0x2C);
}

/* 指定位置画点 */
void LCD_DrawPoint(uint16_t x, uint16_t y, uint16_t color )
{
	//设置光标位置
	LCD_SetArea(x, y, x, y);

	LCD_Write_16Byte(color);
}

/* LCD清屏 */
void LCD_Clear(uint16_t color)
{
	//计算总像素数
	uint32_t total = LCD_H * LCD_W;

	LCD_SetArea(0, 0, 239, 319);

	LCD_CS_CLR();								// 选择屏幕
	LCD_DC_SET();								// DC=1，数据模式

	for (uint32_t i=0; i < total; i++)
		LCD_Write_16Byte(color);

	LCD_CS_SET();								// 释放屏幕
}


/* LCD屏幕方向控制 */
/* 0=0度 1=90度 2=180度 3=270度 */
/* Memory Access Control (36h) */
/* Bit7 MY —— Row Address Order 行地址顺序
   Bit6 MX —— Column Address Order 列地址顺序
	 Bit5 MV —— Row / Column Exchange 行列交换，旋转屏幕
	 Bit3 BGR —— RGB / BGR 颜色通道顺序（交换红蓝）
*/
void LCD_SetDirection(uint8_t direction)
{
	uint16_t width , height;
	uint8_t madctl;

	direction %=4;
	lcd_dir =direction %4;
	//顺时针旋转
	switch (direction)
	{
		case 0:
			width =LCD_W;
			height =LCD_H;
			madctl = (1<<3) | (0<<6) | (0<<7);// BGR=1,MY=0,MX=0,MV=0
			break;
		case 1:
			width =LCD_W;
			height =LCD_H;
			madctl = (1<<3) | (0<<7) | (1<<6) | (1<<5);// BGR=1,MY=0,MX=1,MV=1
			break;
		case 2:
			width =LCD_W;
			height =LCD_H;
			madctl = (1<<3) | (1<<7) | (1<<6) | (0<<5);// BGR=1,MY=1,MX=1,MV=0
			break;
		case 3:
			width =LCD_W;
			height =LCD_H;
			madctl = (1<<3) | (1<<7) | (0<<6) | (1<<5);// BGR=1,MY=1,MX=0,MV=1
			break;
		default :break;
	}
	LCD_WriteReg(0x36 , madctl);
	lcd_width = width;
	lcd_height = height;
}

/* 画水平线 */
void LCD_DrawHLine(uint16_t x ,uint16_t y,uint16_t len ,uint16_t color)
{
	LCD_SetArea(x, y, x+len-1 ,y);
  for (uint16_t i = 0; i < len; i++)
		LCD_Write_16Byte(color);
}

/* 画竖直线 */
void LCD_DrawVLine(uint16_t x ,uint16_t y,uint16_t len ,uint16_t color)
{
	LCD_SetArea(x, y, x ,y + len-1);
  for (uint16_t i = 0; i < len; i++)
		LCD_Write_16Byte(color);
}

/* 画实心矩形 */
void LCD_DrawRect(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color)
{
	uint32_t total =(uint32_t )w * h;
	LCD_SetArea(x, y, x+w-1 , y+h-1);
	for(uint32_t i=0 ; i < total; i++ )
		LCD_Write_16Byte(color);
}

/* 画实心矩形 + DMA */
void LCD_DrawRect_DMA(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color)
{
	LCD_SetArea(x, y, x+w-1 , y+h-1);
	LCD_CS_CLR();				// 选择屏幕
	LCD_DC_SET();				// DC=1，数据模式
	//240 像素 x 2 字节 = 480 字节
	static uint8_t line_buf[240 * 2];
	uint16_t pixels = (w <=240) ? w : 240;
	for (uint16_t i = 0; i < pixels; i++)
	{
		line_buf[i * 2]  	 =(uint8_t)(color >> 8);
		line_buf[i * 2+1]  =(uint8_t)(color &0xFF);
	}

	//启动 DMA 传输
	for (uint16_t row = 0; row < h; row++ )
	{
		SPI_DMA_SendBytes_Wait(line_buf, pixels * 2);
	}
	LCD_CS_SET();
}

/* 画空心矩形 */
void LCD_DrawHollowrect(uint16_t x ,uint16_t y,uint16_t w ,uint16_t h ,uint16_t color)
{
	LCD_DrawHLine(x, y, w, color);			//上
	LCD_DrawHLine(x, y+h-1, w, color);	//下
	LCD_DrawVLine(x, y, h, color);			//左
	LCD_DrawVLine(x + w - 1, y, w, color);	//右
}


void LCD_ShowImage(uint16_t x, uint16_t y, const ImageShow *image)
{
		uint32_t i;
    uint16_t w = image->width;
    uint16_t h = image->height;
    const uint8_t *p = image->data;

    if(x + w > LCD_W)
        w = LCD_W - x;
    if(y + h > LCD_H)
        h = LCD_H - y;
    if(w == 0 || h == 0)
        return;

		LCD_SetArea(x ,y ,x + w-1,y + h-1);
		LCD_CS_CLR();				// 选择屏幕
		LCD_DC_SET();				// DC=1，数据模式

		for (i = 0; i < w * h; i++)
		{
			// RGB565中一个像素占2字节
			// byte0=原始低字节，byte1=原始高字节
			uint8_t byte_low  = p[2*i + 0];
			uint8_t byte_high = p[2*i + 1];

			//MSB
			LCD_WriteData(byte_high);
			LCD_WriteData(byte_low);
		}

		LCD_CS_SET();				// 释放屏幕
}

/* LCD 显示图片DMA版：直接 DMA 传输图片数据 */
void LCD_ShowImage_DMA(uint16_t x, uint16_t y, const ImageShow *image)
{
	uint16_t w = image->width;
	uint16_t h = image->height;
	LCD_SetArea(x ,y ,x + w-1 ,y + h-1);
	LCD_CS_CLR();				// 选择屏幕
	LCD_DC_SET();				// DC=1，数据模式
	SPI_DMA_SendBytes_Wait(image->data, w * h * 2);
	LCD_CS_SET();
}

/*===========================================================================
 *  LVGL 专用：把 LVGL 渲染好的整块像素缓冲 DMA 写入指定 GRAM 窗口
 *  - 窗口与 0x2C 写 RAM 命令由 LCD_SetArea 完成
 *  - pixel_buf 须为 RGB565、高字节在前
 *  - 阻塞至传输完成（SPI_DMA_SendBytes_Wait 内部带超时保护）
 *===========================================================================*/
void LCD_FlushArea_DMA(uint16_t x1, uint16_t y1, uint16_t x2, uint16_t y2, const uint8_t *pixel_buf)
{
	uint32_t size = (uint32_t)(x2 - x1 + 1) * (y2 - y1 + 1) * 2;	// RGB565 每像素 2 字节

	LCD_SetArea(x1, y1, x2, y2);
	LCD_CS_CLR();				// 选择屏幕
	LCD_DC_SET();				// DC=1，数据模式
	SPI_DMA_SendBytes_Wait(pixel_buf, size);
	LCD_CS_SET();				// 释放屏幕
}


/* LCD初始化 */
void LCD_init(void)
{
	//硬件初始化
	SPI1_init();
	SPI_DMA_Init();
	LCD_GPIO_init();
	LCD_RESET();


	//ILI9341 寄存器初始化序列
	  LCD_WriteCmd(0xCF);
    LCD_WriteData(0x00);
    LCD_WriteData(0xC1);
    LCD_WriteData(0x30);

    LCD_WriteCmd(0xED);
    LCD_WriteData(0x64);
    LCD_WriteData(0x03);
    LCD_WriteData(0x12);
    LCD_WriteData(0x81);

    LCD_WriteCmd(0xE8);
    LCD_WriteData(0x85);
    LCD_WriteData(0x00);
    LCD_WriteData(0x78);

    LCD_WriteCmd(0xCB);
    LCD_WriteData(0x39);
    LCD_WriteData(0x2C);
    LCD_WriteData(0x00);
    LCD_WriteData(0x34);
    LCD_WriteData(0x02);

    LCD_WriteReg(0xF7, 0x20);

    LCD_WriteCmd(0xEA);
    LCD_WriteData(0x00);
    LCD_WriteData(0x00);

    LCD_WriteReg(0xC0, 0x13);    // Power Control 1: VRH[5:0]
    LCD_WriteReg(0xC1, 0x13);    // Power Control 2: SAP[2:0]; BT[3:0]

    LCD_WriteCmd(0xC5);          // VCOM Control 1
    LCD_WriteData(0x22);
    LCD_WriteData(0x35);

    LCD_WriteReg(0xC7, 0xBD);    // VCOM Control 2

    LCD_WriteCmd(0x21);          // Display Inversion ON

    // ===== 内存访问控制 =====
    // 注意：BGR=1，颜色顺序为 BGR
    LCD_WriteReg(0x36, 0x08);    // MY=0, MX=0, MV=0, BGR=1

    // ===== 显示功能控制 =====
    LCD_WriteCmd(0xB6);
    LCD_WriteData(0x0A);
    LCD_WriteData(0xA2);

    LCD_WriteReg(0x3A, 0x55);    // 像素格式: 16bit RGB565

    LCD_WriteCmd(0xF6);          // 接口控制
    LCD_WriteData(0x01);
    LCD_WriteData(0x30);

    // ===== 帧率控制 =====
    LCD_WriteCmd(0xB1);
    LCD_WriteData(0x00);
    LCD_WriteData(0x1B);

    // ===== Gamma 校准 =====
    LCD_WriteReg(0xF2, 0x00);    // 3Gamma Function Disable
    LCD_WriteReg(0x26, 0x01);    // Gamma Curve Selected

    // 正Gamma（0xE0，共 15 字节）
    LCD_WriteCmd(0xE0);
    LCD_WriteData(0x0F); LCD_WriteData(0x35); LCD_WriteData(0x31);
    LCD_WriteData(0x0B); LCD_WriteData(0x0E); LCD_WriteData(0x06);
    LCD_WriteData(0x49); LCD_WriteData(0xA7); LCD_WriteData(0x33);
    LCD_WriteData(0x07); LCD_WriteData(0x0F); LCD_WriteData(0x03);
    LCD_WriteData(0x0C); LCD_WriteData(0x0A); LCD_WriteData(0x00);

    // 负Gamma（0xE1，共 15 字节）
    LCD_WriteCmd(0xE1);
    LCD_WriteData(0x00); LCD_WriteData(0x0A); LCD_WriteData(0x0F);
    LCD_WriteData(0x04); LCD_WriteData(0x11); LCD_WriteData(0x08);
    LCD_WriteData(0x36); LCD_WriteData(0x58); LCD_WriteData(0x4D);
    LCD_WriteData(0x07); LCD_WriteData(0x10); LCD_WriteData(0x0C);
    LCD_WriteData(0x32); LCD_WriteData(0x34); LCD_WriteData(0x0F);

    // ===== 显示开关 =====
    LCD_WriteCmd(0x11);       // Sleep Out（退出睡眠）
    delay_ms(120);            // 手册要求：至少等待 120ms

    LCD_WriteCmd(0x29);       // Display ON（开启显示）

    // 清为白色
    LCD_Clear(0xFFFF);

		//初始化竖屏显示方向
		LCD_SetDirection(2);

}
