#include "rtc.h"

void MyRTC_Init(void)
{
	RTC_InitTypeDef RTC_InitStructure;
	RCC_APB1PeriphClockCmd(RCC_APB1Periph_PWR ,ENABLE);

	PWR_BackupAccessCmd(ENABLE);

	if (RTC_ReadBackupRegister(RTC_BKP_DR0) != 0xAAAA)
	{
		RTC_TimeTypeDef t;
		RTC_DateTypeDef d;

		RCC_LSEConfig(RCC_LSE_ON);
		while (RCC_GetFlagStatus(RCC_FLAG_LSERDY) != SET);
		RCC_RTCCLKConfig(RCC_RTCCLKSource_LSE);
		RTC_InitStructure.RTC_AsynchPrediv = 0x7F;
		RTC_InitStructure.RTC_HourFormat = RTC_HourFormat_24;
		RTC_InitStructure.RTC_SynchPrediv = 0x7FFF;
		RTC_Init(&RTC_InitStructure);
		RCC_RTCCLKCmd(ENABLE);
		t.RTC_Hours   = 0;
		t.RTC_Minutes = 59;
		t.RTC_Seconds = 0;
		d.RTC_Year    = 26;
		d.RTC_Month   = RTC_Month_August;
		d.RTC_Date    = 27;
		d.RTC_WeekDay = RTC_Weekday_Thursday;
		RTC_SetTime(RTC_Format_BIN ,&t);
		RTC_SetDate(RTC_Format_BIN ,&d);
		RTC_WriteBackupRegister(RTC_BKP_DR0 ,0xAAAA);
	}
}

/* 读取 RTC 当前日期和时间 */
void MyRTC_ReadTime(RTC_TimeTypeDef *t , RTC_DateTypeDef *d)
{
	RTC_GetTime(RTC_Format_BIN ,t);
	RTC_GetDate(RTC_Format_BIN ,d);
}

/* 写入日期和时间到 RTC（用于 SNTP 校时后同步） */
void MyRTC_SetTime(const RTC_TimeTypeDef *t , const RTC_DateTypeDef *d)
{
	RTC_SetTime(RTC_Format_BIN ,(RTC_TimeTypeDef *)t);
	RTC_SetDate(RTC_Format_BIN ,(RTC_DateTypeDef *)d);
}
