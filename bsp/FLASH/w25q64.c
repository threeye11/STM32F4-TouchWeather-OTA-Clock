#include "w25q64.h"

/* ---- W25Q64 命令集（数据手册指令表） ---- */
#define CMD_JEDEC_ID     0x9F
#define CMD_WRITE_ENABLE 0x06
#define CMD_READ_DATA    0x03
#define CMD_PAGE_PROG    0x02
#define CMD_SECTOR_ER4K  0x20
#define CMD_BLOCK_ER64K  0xD8
#define CMD_READ_STATUS  0x05

#define W25Q_CS_LOW()    GPIO_ResetBits(GPIOB, GPIO_Pin_12)
#define W25Q_CS_HIGH()   GPIO_SetBits(GPIOB, GPIO_Pin_12)

/* 单字节 SPI 全双工收发（轮询式；BootLoader 里同样可用） */
static uint8_t spi_rw(uint8_t byte)
{
	while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_TXE) == RESET) ;
	SPI_I2S_SendData(SPI2, byte);
	while (SPI_I2S_GetFlagStatus(SPI2, SPI_I2S_FLAG_RXNE) == RESET) ;
	return (uint8_t)SPI_I2S_ReceiveData(SPI2);
}

static uint8_t read_status(void)
{
	uint8_t v;
	W25Q_CS_LOW();
	spi_rw(CMD_READ_STATUS);
	v = spi_rw(0xFF);
	W25Q_CS_HIGH();
	return v;
}

/* 等待 BUSY 清零；带超时保护（Flash 未接时避免死循环） */
static void wait_busy(void)
{
	uint32_t t = 2000000;
	while ((read_status() & 0x01) && --t) ;
}

static void write_enable(void)
{
	W25Q_CS_LOW();
	spi_rw(CMD_WRITE_ENABLE);
	W25Q_CS_HIGH();
}

void W25Q_Init(void)
{
	GPIO_InitTypeDef g;
	SPI_InitTypeDef   s;

	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_SPI2, ENABLE);

	/* PB13/14/15 复用为 SPI2 */
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource13, GPIO_AF_SPI2);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource14, GPIO_AF_SPI2);
	GPIO_PinAFConfig(GPIOB, GPIO_PinSource15, GPIO_AF_SPI2);

	g.GPIO_Pin   = GPIO_Pin_13 | GPIO_Pin_14 | GPIO_Pin_15;
	g.GPIO_Mode  = GPIO_Mode_AF;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_PuPd  = GPIO_PuPd_UP;
	g.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &g);

	/* PB12 = CS：推挽输出，空闲拉高 */
	g.GPIO_Pin   = GPIO_Pin_12;
	g.GPIO_Mode  = GPIO_Mode_OUT;
	g.GPIO_OType = GPIO_OType_PP;
	g.GPIO_PuPd  = GPIO_PuPd_UP;
	g.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOB, &g);
	W25Q_CS_HIGH();

	SPI_I2S_DeInit(SPI2);
	s.SPI_Direction         = SPI_Direction_2Lines_FullDuplex;
	s.SPI_Mode              = SPI_Mode_Master;
	s.SPI_DataSize          = SPI_DataSize_8b;
	s.SPI_CPOL              = SPI_CPOL_Low;
	s.SPI_CPHA              = SPI_CPHA_1Edge;
	s.SPI_NSS               = SPI_NSS_Soft;
	s.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_8;  /* APB1 42MHz/8 = 5.25MHz：面包板走线降速保稳定 */
	s.SPI_FirstBit          = SPI_FirstBit_MSB;
	s.SPI_CRCPolynomial     = 7;
	SPI_Init(SPI2, &s);
	SPI_Cmd(SPI2, ENABLE);
}

uint32_t W25Q_ReadID(void)
{
	uint32_t id;
	W25Q_CS_LOW();
	spi_rw(CMD_JEDEC_ID);
	id  = (uint32_t)spi_rw(0xFF) << 16;
	id |= (uint32_t)spi_rw(0xFF) << 8;
	id |= (uint32_t)spi_rw(0xFF);
	W25Q_CS_HIGH();
	return id;
}

void W25Q_Read(uint32_t addr, uint8_t *buf, uint32_t len)
{
	uint32_t i;
	W25Q_CS_LOW();
	spi_rw(CMD_READ_DATA);
	spi_rw((uint8_t)(addr >> 16));
	spi_rw((uint8_t)(addr >> 8));
	spi_rw((uint8_t)addr);
	for (i = 0; i < len; i++) buf[i] = spi_rw(0xFF);
	W25Q_CS_HIGH();
}

void W25Q_PageProgram(uint32_t addr, const uint8_t *buf, uint16_t len)
{
	uint16_t i;
	if (len > W25Q_PAGE_SIZE) len = W25Q_PAGE_SIZE;   /* 硬约束：不超页、不跨页 */

	write_enable();
	W25Q_CS_LOW();
	spi_rw(CMD_PAGE_PROG);
	spi_rw((uint8_t)(addr >> 16));
	spi_rw((uint8_t)(addr >> 8));
	spi_rw((uint8_t)addr);
	for (i = 0; i < len; i++) spi_rw(buf[i]);
	W25Q_CS_HIGH();
	wait_busy();
}

void W25Q_SectorErase(uint32_t addr)
{
	write_enable();
	W25Q_CS_LOW();
	spi_rw(CMD_SECTOR_ER4K);
	spi_rw((uint8_t)(addr >> 16));
	spi_rw((uint8_t)(addr >> 8));
	spi_rw((uint8_t)addr);
	W25Q_CS_HIGH();
	wait_busy();                       /* 典型 45ms */
}

void W25Q_BlockErase64K(uint32_t addr)
{
	write_enable();
	W25Q_CS_LOW();
	spi_rw(CMD_BLOCK_ER64K);
	spi_rw((uint8_t)(addr >> 16));
	spi_rw((uint8_t)(addr >> 8));
	spi_rw((uint8_t)addr);
	W25Q_CS_HIGH();
	wait_busy();                       /* 典型 150ms */
}


/* 擦除 [base, base+size)：64K 对齐处用块擦（快），余量用 4K 扇区补齐。
 * 必须覆盖整个写入区间：NOR 写入是按位与，漏擦处会留下旧数据残影 */
void W25Q_EraseRange(uint32_t base, uint32_t size)
{
	uint32_t off;

	for (off = 0; off < size; )
	{
		if ((size - off >= 64u * 1024u) && ((base + off) % (64u * 1024u) == 0))
		{
			W25Q_BlockErase64K(base + off);
			off += 64u * 1024u;
		}
		else
		{
			W25Q_SectorErase(base + off);
			off += 4u * 1024u;
		}
	}
}


