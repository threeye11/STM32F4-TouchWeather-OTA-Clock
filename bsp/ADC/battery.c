#include "battery.h"

void Battery_Init(void)
{
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA ,ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 ,ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	ADC_InitTypeDef ADC_InitStructure;
	
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_1;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AN;
	GPIO_InitStructure.GPIO_PuPd  =GPIO_PuPd_NOPULL;
	GPIO_Init(GPIOA , &GPIO_InitStructure);
	
	ADC_StructInit(&ADC_InitStructure);
	ADC_InitStructure.ADC_Resolution = ADC_Resolution_12b;
	ADC_InitStructure.ADC_ScanConvMode = DISABLE;
	ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
	ADC_InitStructure.ADC_ExternalTrigConvEdge = ADC_ExternalTrigConvEdge_None;
	ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
	ADC_InitStructure.ADC_NbrOfConversion = 1;
	ADC_Init(ADC1 , &ADC_InitStructure);
	
	//480 周期，更适合电源噪声抑制 
	ADC_RegularChannelConfig(ADC1 , ADC_Channel_1 ,1 ,ADC_SampleTime_480Cycles);		
	ADC_Cmd(ADC1 ,ENABLE);
	
	
}

/* 简化读取使用PA1读取3.3V 电源轨电压（单位：mV）
 * 由于vbat未焊接使用不了内部纽扣电池
*/
uint16_t Battery_GetVoltage_mV(void)
{
	uint32_t sum = 0;
	uint8_t i ;
	
	for (i = 0; i < 16; i++)
	{
		ADC_SoftwareStartConv(ADC1);
		while(ADC_GetFlagStatus(ADC1 ,ADC_FLAG_EOC) == RESET);
		sum += ADC_GetConversionValue(ADC1);
	}
	uint16_t avg_adc = sum / 16;
	uint32_t voltage_mv = (uint32_t)avg_adc * 3300 / 4095;
	
	return (uint16_t)voltage_mv;
}

uint8_t Battery_GetPercent(void)
{
    uint16_t voltage_mv = Battery_GetVoltage_mV();

    if (voltage_mv >= 3300) return 100;
    if (voltage_mv <= 0) return 0;

    uint32_t percent = (uint32_t)voltage_mv * 100 / 3300;   /* 先乘后除，避免整数截断成 0 */
    return (uint8_t)(percent > 100 ? 100 : percent);
}

