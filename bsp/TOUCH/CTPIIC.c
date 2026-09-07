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
#include "board.h"          // delay_us

/* 软件模拟 I2C（PB8=SCL, PB9=SDA），移植自厂家例程（Demo_STM32/.../HARDWARE/TOUCH/ctpiic.c）。
   原因：FT6336G 手册 2.4 节时序表要求重复起始建立时间 4.7us、STOP 到 START 总线
   空闲 4.7us（接近标准模式 100kHz 的时序），硬件 I2C 跑 400kHz 时这些间隔只有
   0.6~1.5us，芯片可能认不出重复起始，导致读 ID 失败。软件 I2C 的微秒级延时
   天然满足全部时序要求，也是厂家例程验证过的方案。 */

#define FT_I2C_ADDR  0x38   // FT6336G 7bit 地址（写 0x70 / 读 0x71）

#define CTP_SCL_SET()  GPIO_SetBits(GPIOB, GPIO_Pin_8)
#define CTP_SCL_CLR()  GPIO_ResetBits(GPIOB, GPIO_Pin_8)
#define CTP_SDA_SET()  GPIO_SetBits(GPIOB, GPIO_Pin_9)
#define CTP_SDA_CLR()  GPIO_ResetBits(GPIOB, GPIO_Pin_9)
#define CTP_READ_SDA   GPIO_ReadInputDataBit(GPIOB, GPIO_Pin_9)

static void sda_out(void)
{
    GPIO_InitTypeDef s;
    s.GPIO_Pin   = GPIO_Pin_9;
    s.GPIO_Mode  = GPIO_Mode_OUT;
    s.GPIO_OType = GPIO_OType_PP;
    s.GPIO_PuPd  = GPIO_PuPd_UP;
    s.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &s);
}

static void sda_in(void)
{
    GPIO_InitTypeDef s;
    s.GPIO_Pin  = GPIO_Pin_9;
    s.GPIO_Mode = GPIO_Mode_IN;
    s.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOB, &s);
}

static void iic_delay(void) { delay_us(1); }

static void iic_start(void)            // SCL 高时 SDA 拉低 = 起始/重复起始
{
    sda_out();
    CTP_SDA_SET();
    CTP_SCL_SET();
    iic_delay();
    CTP_SDA_CLR();
    iic_delay();
    CTP_SCL_CLR();
}

static void iic_stop(void)             // SCL 高时 SDA 拉高 = 停止
{
    sda_out();
    CTP_SCL_CLR();
    CTP_SDA_CLR();
    iic_delay();
    CTP_SCL_SET();
    iic_delay();
    CTP_SDA_SET();
}

static uint8_t iic_wait_ack(void)      // 等从机拉低 SDA 应答，0=成功 1=无应答
{
    uint8_t err = 0;
    sda_in();
    CTP_SDA_SET();                     // 输入模式下写 ODR 无效，由外部上拉拉高
    delay_us(1);
    CTP_SCL_SET();
    delay_us(1);
    while (CTP_READ_SDA)
    {
        if (++err > 250)
        {
            iic_stop();
            return 1;
        }
    }
    CTP_SCL_CLR();
    return 0;
}

static void iic_send_byte(uint8_t txd) // MSB 先行发送 8 位
{
    uint8_t t;
    sda_out();
    CTP_SCL_CLR();
    for (t = 0; t < 8; t++)
    {
        if (txd & 0x80) CTP_SDA_SET(); else CTP_SDA_CLR();
        txd <<= 1;
        CTP_SCL_SET();
        iic_delay();
        CTP_SCL_CLR();
        iic_delay();
    }
}

static uint8_t iic_read_byte(uint8_t ack) // 读 8 位；ack=1 发应答，ack=0 发非应答（最后字节）
{
    uint8_t i, recv = 0;
    sda_in();
    for (i = 0; i < 8; i++)
    {
        CTP_SCL_CLR();
        delay_us(3);
        CTP_SCL_SET();
        recv <<= 1;
        if (CTP_READ_SDA) recv++;
    }
    // 第 9 个时钟：主机发 ACK/NACK
    sda_out();
    CTP_SCL_CLR();
    if (ack) CTP_SDA_CLR(); else CTP_SDA_SET();
    iic_delay();
    CTP_SCL_SET();
    iic_delay();
    CTP_SCL_CLR();
    CTP_SDA_SET();
    return recv;
}

/* 初始化：PB8=SCL、PB9=SDA（推挽输出，读时 SDA 切输入）+ PE0=INT 输入上拉、PE1=RST 输出 */
void I2C1_init(void)
{
    GPIO_InitTypeDef s;

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);

    s.GPIO_Pin   = GPIO_Pin_8 | GPIO_Pin_9;
    s.GPIO_Mode  = GPIO_Mode_OUT;
    s.GPIO_OType = GPIO_OType_PP;
    s.GPIO_PuPd  = GPIO_PuPd_UP;
    s.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &s);

    s.GPIO_Pin  = GPIO_Pin_0;           // PE0 = CTP_INT
    s.GPIO_Mode = GPIO_Mode_IN;
    s.GPIO_PuPd = GPIO_PuPd_UP;
    GPIO_Init(GPIOE, &s);

    s.GPIO_Pin   = GPIO_Pin_1;          // PE1 = CTP_RST
    s.GPIO_Mode  = GPIO_Mode_OUT;
    s.GPIO_OType = GPIO_OType_PP;
    s.GPIO_PuPd  = GPIO_PuPd_NOPULL;
    s.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOE, &s);

    CTP_SCL_SET();
    CTP_SDA_SET();
}

/* 写：START -> 0x70 -> 寄存器地址 -> 数据... -> STOP；0=成功 1=无应答 */
uint8_t I2C1_WriteReg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;

    iic_start();
    iic_send_byte(FT_I2C_ADDR << 1);           // 0x70 写命令
    if (iic_wait_ack()) return 1;
    iic_send_byte(reg);
    if (iic_wait_ack()) return 1;
    for (i = 0; i < len; i++)
    {
        iic_send_byte(buf[i]);
        if (iic_wait_ack()) return 1;
    }
    iic_stop();
    return 0;
}

/* 读：START -> 0x70 -> 寄存器地址 -> 重复START -> 0x71 -> 读数据... -> STOP；0=成功 1=无应答 */
uint8_t I2C1_ReadReg(uint8_t reg, uint8_t *buf, uint8_t len)
{
    uint8_t i;
    if (len == 0) return 0;

    iic_start();
    iic_send_byte(FT_I2C_ADDR << 1);           // 0x70 写命令
    if (iic_wait_ack()) return 1;
    iic_send_byte(reg);
    if (iic_wait_ack()) return 1;

    iic_start();                               // 重复起始（不产生 STOP）
    iic_send_byte((FT_I2C_ADDR << 1) | 1);     // 0x71 读命令
    if (iic_wait_ack()) return 1;

    for (i = 0; i < len; i++)
        buf[i] = iic_read_byte(i == len - 1 ? 0 : 1);  // 最后一字节发 NACK

    iic_stop();
    return 0;
}
