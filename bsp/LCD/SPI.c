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

#include "spi.h"
#include "bsp_uart.h"
/* DMA2 Stream3 Channel3  SPI1_TX */
static volatile bool SPI_DMA_Done = true;   // DMA 传输完成标志

void SPI_DMA_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure;
	NVIC_InitTypeDef NVIC_InitStructure;
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2, ENABLE);

	DMA_DeInit(DMA2_Stream3);
	while (DMA_GetCmdStatus(DMA2_Stream3) != DISABLE);

	DMA_InitStructure.DMA_Channel                      = DMA_Channel_3;
	DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&SPI1->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr    = 0;
	DMA_InitStructure.DMA_DIR                          = DMA_DIR_MemoryToPeripheral;
	DMA_InitStructure.DMA_BufferSize         = 0;
	DMA_InitStructure.DMA_PeripheralInc      = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_MemoryInc                  = DMA_MemoryInc_Enable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize      = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode                              = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority                      = DMA_Priority_High;
	DMA_InitStructure.DMA_FIFOMode                      = DMA_FIFOMode_Enable;
	DMA_InitStructure.DMA_FIFOThreshold              = DMA_FIFOStatus_Full;
	DMA_InitStructure.DMA_MemoryBurst                  = DMA_MemoryBurst_Single;
	DMA_InitStructure.DMA_PeripheralBurst          = DMA_PeripheralBurst_Single;
	DMA_Init(DMA2_Stream3, &DMA_InitStructure);

	// DMA 完成中断（备用，主路径用轮询）
	DMA_ITConfig(DMA2_Stream3, DMA_IT_TC, ENABLE);

	NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream3_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_Init(&NVIC_InitStructure);

}

/* DMA 完成中断（备用） */
void DMA2_Stream3_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream3, DMA_IT_TCIF3))
	{
		DMA_ClearITPendingBit(DMA2_Stream3, DMA_IT_TCIF3);
		SPI_DMA_Done = true;
	}
}

/* 等待 DMA 完成：轮询 Stream 使能位，ISR 备用 */
static void SPI_DMA_WaitDone(void)
{
	uint32_t timeout = 0xFFFFF;  //约1秒超时(168MHz)

	/* 主路径：轮询 EN 位，传输完成后硬件自动清除 */
	while (DMA_GetCmdStatus(DMA2_Stream3) != DISABLE)
	{
		if (--timeout == 0)
		{
			safe_printf("SPI DMA wait timeout!\r\n");
			SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
			DMA_Cmd(DMA2_Stream3, DISABLE);
			DMA_ClearITPendingBit(DMA2_Stream3, DMA_IT_TCIF3);
			SPI_DMA_Done = true;
			return;
		}
	}
	/* 关闭SPI DMA请求，恢复阻塞SPI模式 */
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, DISABLE);
	/* 清除中断标志，同步 ISR 状态 */
	DMA_ClearITPendingBit(DMA2_Stream3, DMA_IT_TCIF3);
	/* 刷新SPI：清空RXNE残留数据，等待总线空闲 */
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_BSY));
	if (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE))
		SPI_I2S_ReceiveData(SPI1);
	SPI_DMA_Done = true;
}

/* DMA 发送，非阻塞，由调用者等待完成 */
uint8_t SPI_DMA_SendBytes(const uint8_t *data, uint32_t len)
{
	/* 等待上次传输完成 */
	SPI_DMA_WaitDone();

	/* 确保Stream已关闭 */
	while (DMA_GetCmdStatus(DMA2_Stream3) != DISABLE);

	DMA2_Stream3->M0AR = (uint32_t)data;
	DMA2_Stream3->NDTR = len;

	SPI_DMA_Done = false;
	SPI_I2S_DMACmd(SPI1, SPI_I2S_DMAReq_Tx, ENABLE);
	DMA_Cmd(DMA2_Stream3, ENABLE);

	return 0;
}

/* DMA发送 + 等待完成 */
void SPI_DMA_SendBytes_Wait(const uint8_t *data, uint32_t len)
{
	SPI_DMA_SendBytes(data, len);
	SPI_DMA_WaitDone();
}

/* 查询 DMA 是否空闲 */
uint8_t SPI_DMA_IsIdle(void)
{
    return (DMA_GetCmdStatus(DMA2_Stream3) == DISABLE);
}

/* 初始化PA5 PA6 PA7 */
void SPI1_init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_SPI1, ENABLE);

	GPIO_InitTypeDef GPIO_InitStructure;
	SPI_InitTypeDef SPI_InitStructure;

	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5 | GPIO_Pin_6 | GPIO_Pin_7;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd = GPIO_PuPd_UP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);

	GPIO_PinAFConfig(GPIOA, GPIO_PinSource5, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource6, GPIO_AF_SPI1);
	GPIO_PinAFConfig(GPIOA, GPIO_PinSource7, GPIO_AF_SPI1);

	SPI_I2S_DeInit(SPI1);
	SPI_InitStructure.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;  //提高刷新率 10.5 MHz
	SPI_InitStructure.SPI_CPHA = SPI_CPHA_2Edge;
	SPI_InitStructure.SPI_CPOL = SPI_CPOL_High;
	SPI_InitStructure.SPI_CRCPolynomial = 7;
	SPI_InitStructure.SPI_DataSize = SPI_DataSize_8b;
	SPI_InitStructure.SPI_FirstBit = SPI_FirstBit_MSB;
	SPI_InitStructure.SPI_Mode = SPI_Mode_Master;
	SPI_InitStructure.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
	SPI_InitStructure.SPI_NSS = SPI_NSS_Soft;
	SPI_Init(SPI1, &SPI_InitStructure);

	SPI_Cmd(SPI1, ENABLE);
}


/* 发送一个字节 */
void SPI_SendByte(uint8_t Byte)
{
	uint32_t timeout;

	timeout = 0xFFFF;
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET && timeout--);
	if (timeout == 0)
	{
		return;
	}

	SPI_I2S_SendData(SPI1, Byte);

	timeout = 0xFFFF;
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET && timeout--);
	if (timeout == 0)
	{
		return;
	}

	SPI_I2S_ReceiveData(SPI1);
}


/* 读取一个字节 */
uint8_t SPI_ReadByte(void)
{
	uint32_t Timeout = 0xffff;

	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_TXE) == RESET && Timeout--)
		if (Timeout == 0) {
			printf("读取状态超时");
			return 0;
		}
		SPI_I2S_SendData(SPI1, 0xff);
	while (SPI_I2S_GetFlagStatus(SPI1, SPI_I2S_FLAG_RXNE) == RESET && Timeout--)
			if (Timeout == 0) {
			printf("读取状态超时");
			return 0;
		}
		return SPI_I2S_ReceiveData(SPI1);
}
