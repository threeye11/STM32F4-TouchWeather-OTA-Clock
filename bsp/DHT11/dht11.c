/* PE2  --> DATA */

#include "dht11.h"
#include "board.h"
#include "freertos.h"
#include "task.h"

/* 推挽输出：驱动总线 */
static void Dht11_Pin_Out(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_OUT;
	GPIO_InitStructure.GPIO_OType = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

/* 上拉输入：读总线 */
static void Dht11_Pin_In(void)
{
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Pin   = DHT11_PIN;
	GPIO_InitStructure.GPIO_Mode  = GPIO_Mode_IN;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_PuPd  = GPIO_PuPd_UP;
	GPIO_Init(DHT11_PORT, &GPIO_InitStructure);
}

/* DHT11 初始化 */
void Dht11_Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOE, ENABLE);
	Dht11_Pin_Out();
	DHT11_DQ_H();
}

/*
 * DHT11 读取（半双工单总线协议）
 *
 * 时序要点:
 *   1. 主机拉低 >= 1ms（起始信号），然后拉高 20~40us
 *   2. 切换为输入，等待 DHT11 响应：LOW 80us + HIGH 80us
 *   3. 读取 40 位数据：每位先 LOW ~50us，再 HIGH (26us=0, 70us=1)
 *   4. 校验：前 4 字节之和 == 第 5 字节
 *
 * FreeRTOS 注意:
 *   - 起始信号用 taskENTER_CRITICAL 关中断，保证精确时序
 *   - 读取 40 位时，每位采样窗口关中断 ~30us
 */
bool DHT11_Read(dht11_data_t *out)
{
	uint8_t data[5] = {0};
	uint32_t timeout;

	/* ---- 阶段 1：起始信号（必须精确时序，关中断保护） ---- */
	Dht11_Pin_Out();
	DHT11_DQ_L();
	taskENTER_CRITICAL();
	delay_us(18000);      /* 拉低 >= 1ms（用 DWT 精确 18ms，不受调度器影响） */
	taskEXIT_CRITICAL();
	DHT11_DQ_H();
	delay_us(30);         /* 拉高 20~40us */

	/* ---- 阶段 2：切换输入，等待 DHT11 响应 ---- */
	Dht11_Pin_In();

	/* 等 LOW（DHT11 响应起始，~80us） */
	timeout = 200;
	while (DHT11_DQ_IN() == 1 && timeout--) delay_us(1);

	/* 等 HIGH（DHT11 响应后半段，~80us） */
	timeout = 200;
	while (DHT11_DQ_IN() == 0 && timeout--) delay_us(1);

	/* 等 LOW（第一个数据位的起始，~50us） */
	timeout = 200;
	while (DHT11_DQ_IN() == 1 && timeout--) delay_us(1);

	/* ---- 阶段 3：读取 40 位数据 ---- */
	for (uint8_t i = 0; i < 40; i++)
	{
		/* 等待当前位的 LOW 结束（进入 HIGH 窗口） */
		timeout = 200;
		while (DHT11_DQ_IN() == 0 && timeout--) delay_us(1);

		/* 关中断采样：延时 30us 后读引脚 */
		/* 0-bit HIGH 持续 ~26us，30us 后已变 LOW → 读到 0 */
		/* 1-bit HIGH 持续 ~70us，30us 后仍 HIGH   → 读到 1 */
		taskENTER_CRITICAL();
		delay_us(30);
		uint8_t bit = DHT11_DQ_IN();
		taskEXIT_CRITICAL();

		/* 等待当前位的 HIGH 结束（为下一位做准备） */
		timeout = 200;
		while (DHT11_DQ_IN() == 1 && timeout--) delay_us(1);

		data[i / 8] <<= 1;
		if (bit) data[i / 8] |= 0x01;
	}

	/* ---- 阶段 4：校验 ---- */
	if ((uint8_t)(data[0] + data[1] + data[2] + data[3]) != data[4])
	{
		return false;
	}

	out->humi_int = data[0];
	out->humi_dec = data[1];
	out->temp_int = data[2];
	out->temp_dec = data[3];
	return true;
}
