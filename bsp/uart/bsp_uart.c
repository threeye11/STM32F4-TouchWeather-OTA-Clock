#include "bsp_uart.h" 
#include "stdio.h"
#include <stdbool.h>
#include <stdarg.h>
#include "freertos.h"
#include "semphr.h"


extern SemaphoreHandle_t xMtxPrintf ;

//DMA2 Stream7 Channle4--USART1_TX
static volatile bool Usart1_DMA_Done = true;

void USART1_DMA_Init(void)
{
	DMA_InitTypeDef DMA_InitStructure;
  NVIC_InitTypeDef NVIC_InitStructure;
	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA2 ,ENABLE);
	
	DMA_DeInit(DMA2_Stream7);													//»Ö¸´Ä¬ÈÏ×´Ì¬
	while (DMA_GetCmdStatus(DMA2_Stream7) !=DISABLE);	//µÈ´ıDMAÁ÷ÍêÈ«¹Ø±Õ
	
	DMA_StructInit(&DMA_InitStructure);	/* ÏÈÌîÄ¬ÈÏÖµ±ØĞë³õÊ¼»¯£¬Õ»ÉÏÀ¬»øÖµ»áÈÃÁ÷³ö´í¿¨ËÀ */
	DMA_InitStructure.DMA_Channel = DMA_Channel_4;
	DMA_InitStructure.DMA_PeripheralBaseAddr =(uint32_t)&USART1->DR;
	DMA_InitStructure.DMA_Memory0BaseAddr = 0;
	DMA_InitStructure.DMA_DIR = DMA_DIR_MemoryToPeripheral;
	DMA_InitStructure.DMA_BufferSize = 0;
	DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;	/* ÄÚ´æµØÖ·µİÔö£¬·ñÔòÃ¿´¦Æ¼Ö»·¢ buf[0] */
	DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
	DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
	DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
	DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
	DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
	DMA_Init(DMA2_Stream7 ,&DMA_InitStructure);
	
	DMA_ITConfig(DMA2_Stream7,DMA_IT_TC , ENABLE);
	
	NVIC_InitStructure.NVIC_IRQChannel = DMA2_Stream7_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
	NVIC_InitStructure.NVIC_IRQChannelCmd =ENABLE;
	NVIC_Init(&NVIC_InitStructure);
	
	USART_DMACmd(USART1 ,USART_DMAReq_Tx ,ENABLE);
}

void DMA2_Stream7_IRQHandler(void)
{
	if (DMA_GetITStatus(DMA2_Stream7 ,DMA_IT_TCIF7))
	{
		DMA_ClearITPendingBit(DMA2_Stream7 ,DMA_IT_TCIF7);
		Usart1_DMA_Done = true;
	}
}

/* DMA °æ printf
 * ×¢Òâ£º¸ñÊ½»¯±ØĞëÔÚ»¥³âËøÄÚÍê³¨Àª¡ªbuf ÊÇ¦·Ò»¹²Ïí»º³ÈÉDMA »¹ÔÚ¶ÁËüµÄÊ±ºò
 * È¦ÊÎÈË¸Ä§Õ¶¼»áµ¼ÖÂÊä³ö´íÂÒ¡£¾É°æÔÚËøÍâ vsnprintf£¬¸ßÓÅÏÈ¼¶ÈÎÎñÇÀÕ¼ºó»á
 * ¸²¸ÇÕıÔÚ´«ÊäµÄ buf¡£ÏÖÔÚ"µÈÉÏ´ÎDMAÍê³É -> ¸ñÊ½»¯ -> Æô¶¯DMA"È«³Ì³ÖËø */
void safe_printf(const char *fmt,...)
{
	static char buf[128];   //¾²Ì¬»º³ÈÉ±ÜÃâÕ»ÉÏ´óÊı×é
	va_list ap;
	
	xSemaphoreTake(xMtxPrintf, portMAX_DELAY);
	while (!Usart1_DMA_Done);	/* µÈÉÏÒ»´Î DMA ·¢Íê£¬buf ²ÅÄÜ°²È«¸´ÓÃ */

	va_start(ap ,fmt);
	int len = vsnprintf(buf ,sizeof (buf) ,fmt ,ap);
	va_end(ap);
	
	if (len>0)
	{
		Usart1_DMA_Done = 0;
		DMA2_Stream7->M0AR = (uint32_t)buf;
		DMA2_Stream7->NDTR = len;
		DMA_Cmd(DMA2_Stream7, ENABLE);
	}
	xSemaphoreGive(xMtxPrintf);
	/* »¥³âËø±£»¤"µÈ´ı+¸ñÊ½»¯+Æô¶¯"È«¹ı³Ì£ºbuf ²»»á±»²¢·¢¸Ä§Õ£¬
	 * ÏÂÒ»¸ö´òÓ¡Õß»á×èÈûµ½±¾´Î DMA ·¢Íê£¬Êä³ö²»ÔÙ½»´í */
}

void uart1_init(uint32_t __Baud)
{
	GPIO_InitTypeDef GPIO_InitStructure;	
	

	
	RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOA,ENABLE);	

	GPIO_PinAFConfig(GPIOA,GPIO_PinSource9,GPIO_AF_USART1);//IO¿ÚÓÃ×÷´®¿ÚÒı½ÅÒªÅäÖÃ¸´ÓÃÄ£Ê½
	GPIO_PinAFConfig(GPIOA,GPIO_PinSource10,GPIO_AF_USART1);

	GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin           = GPIO_Pin_9;//TXÒı½Å
	GPIO_InitStructure.GPIO_Mode          = GPIO_Mode_AF;//IO¿ÚÓÃ×÷´®¿ÚÒı½ÅÒªÅäÖÃ¸´ÓÃÄ£Ê½
	GPIO_InitStructure.GPIO_Speed         = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_OType         = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd          = GPIO_PuPd_UP;
	GPIO_Init(GPIOA,&GPIO_InitStructure);

	GPIO_StructInit(&GPIO_InitStructure);
	GPIO_InitStructure.GPIO_Pin           = GPIO_Pin_10;//RXÒı½Å
	GPIO_InitStructure.GPIO_Mode          = GPIO_Mode_AF;
	GPIO_InitStructure.GPIO_Speed         = GPIO_Speed_100MHz;
	GPIO_InitStructure.GPIO_OType         = GPIO_OType_PP;
	GPIO_InitStructure.GPIO_PuPd          = GPIO_PuPd_UP;
	GPIO_Init(GPIOA,&GPIO_InitStructure);
  
	USART_InitTypeDef USART_InitStructure;//¶¨ÒåÅäÖÃ´®¿ÚµÄ½á¹¹Ìå±äÁ¿

	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);//¿ªÆô´®¿Ú1µÄÊ±ÖÓ

	USART_DeInit(USART1);//´ó¸ÅÒâË¼ÊÇ½â³ı´Ë´®¿ÚµÄÆäËûÅäÖÃ

	USART_StructInit(&USART_InitStructure);
	USART_InitStructure.USART_BaudRate              = __Baud;//ÉèÖ¨°¨ÌØÂÊ
	USART_InitStructure.USART_WordLength            = USART_WordLength_8b;//×Ö½Ú³¤¶ÈÎª8bit
	USART_InitStructure.USART_StopBits              = USART_StopBits_1;//1¸öÍ£Ö¹¦Ë
	USART_InitStructure.USART_Parity                = USART_Parity_No ;//Ã»ÓĞ§µÑé¦Ë
	USART_InitStructure.USART_Mode                  = USART_Mode_Rx | USART_Mode_Tx;//½«´®¿ÚÅäÖÃÎªÊÕ·¢Ä£Ê½
	USART_InitStructure.USART_HardwareFlowControl   = USART_HardwareFlowControl_None; //²»Ìá¹©Á÷¿Ø 
	USART_Init(USART1,&USART_InitStructure);//½«Ïà¹Ø²ÎÊı³õÊ¼»¯¸ø´®¿Ú1
	
	USART_ClearFlag(USART1,USART_FLAG_RXNE);//³õÊ¼ÅäÖÃÊ±Çå³ı½ÓÊÜÖÃ¦Ë

	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);//³õÊ¼ÅäÖÃ½ÓÊÜÖ§ØÏ

	USART_Cmd(USART1,ENABLE);//¿ªÆô´®¿Ú1
	
	NVIC_InitTypeDef NVIC_InitStructure;//Ö§ØÏ¿ØÖÆ½á¹¹Ìå±äÁ¿¶¨Òå

	NVIC_InitStructure.NVIC_IRQChannel                    = USART1_IRQn;//Ö§ØÏÍ¨µÀÖ¸¶¨ÎªUSART1
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority  = 0;//Ö÷ÓÅÏÈ¼¶Îª0
	NVIC_InitStructure.NVIC_IRQChannelSubPriority         = 1;//´ÎÓÅÏÈ¼¶Îª1
	NVIC_InitStructure.NVIC_IRQChannelCmd                 = ENABLE;//È·¶¨Ê¹ÄÜ
	NVIC_Init(&NVIC_InitStructure);//³õÊ¼»¯ÅäÖÃ´ËÖ§ØÏÍ¨µÀ
		
	USART1_DMA_Init();
}

#if !defined(__MICROLIB)
//²»Ê¹ÓÃÎ¢¿âµÄ»°¾ÍĞèÒªÌí¼ÓÏÂÃæµÄº¯Êı
#if (__ARMCLIB_VERSION <= 6000000)
//Èç¹û±àÒëÆ÷ÊÇAC5  ¾Í¶¨ÒåÏÂÃæÕâ¸ö½á¹¹Ìå
struct __FILE
{
	int handle;
};
#endif

FILE __stdout;

//¶¨Òå_sys_exit()ÒÔ±ÜÃâÊ¹ÓÃ°ëÖ÷»úÄ£Ê½
void _sys_exit(int x)
{
	x = x;
}
#endif

/* retarget the C library printf function to the USART */
int fputc(int ch, FILE *f)
{
    USART_SendData(USART1, (uint8_t)ch);
	
	while( RESET == USART_GetFlagStatus(USART1, USART_FLAG_TXE) ){}
	
    return ch;
}



/******** ´®¿Ú1 Ö§ØÏ·şÎñº¯Êı ***********/
void USART1_IRQHandler(void)
{
	if(USART_GetITStatus(USART1, USART_IT_RXNE) == SET)//Å§ØÏÊÇ²»ÊÇÕæµÄÓĞÖ§ØÏ·¢Éú
	{
		//USART_SendData(USART1,USART_ReceiveData(USART1));//ÓÖ½«Êı¾İ·¢»ØÈ¥(ÓÃÓÚÑéÖ¤)
		
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE); //ÒÑ¾­´¦Àí¾ÍÇå³ş±êÖ¾¦Ë 
	}  
}


