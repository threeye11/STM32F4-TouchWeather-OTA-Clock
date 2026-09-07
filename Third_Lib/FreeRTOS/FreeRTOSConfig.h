#ifndef FREERTOS_CONFIG_H
#define FREERTOS_CONFIG_H


#include "stm32f4xx.h"

/* ================================================================
 *  Ò»¡¢»ù´¡µ÷¶ÈÅäÖÃ
 * ================================================================ */
#define configUSE_PREEMPTION                     1   /* 1=ÇÀÕ¼Ê½µ÷¶È */
#define configCPU_CLOCK_HZ                       ( 168000000UL )
#define configTICK_RATE_HZ                       ( ( TickType_t ) 1000 )  /* 1ms tick */
#define configMAX_PRIORITIES                     7   /* ÓÅÏÈ¼¶ 0~6£¬ÊýÖµÔ½´óÓÅÏÈ¼¶Ô½¸ß */
#define configMINIMAL_STACK_SIZE                 ( ( uint16_t ) 256 )  /* 256 words = 1KB£¨¿ÕÏÐÈÎÎñÕ»¼Ó¿í£¬·ÀÏàÁÚÔ½½ç²ÈÕ»£© */
#define configMAX_TASK_NAME_LEN                  16
#define configUSE_16_BIT_TICKS                   0   /* 32 ¦Ë tick ¼ÆÊýÆ÷ */
#define configIDLE_SHOULD_YIELD                  1
#define configUSE_TIME_SLICING                   1   /* 1=Í¬ÓÅÏÈ¼¶ÈÎÎñÊ±¼äÆ¬ÂÖ×ª */

/* ================================================================
 *  ¶þ¡¢ÈÎÎñ¼äÍ¨ÐÅÓëÍ¬²½
 * ================================================================ */
#define configUSE_TASK_NOTIFICATIONS             1   /* ÈÎÎñÍ¨Öª£¨ÇáÁ¿¼¶£¬Ìæ´úÐÅºÅÁ¿£© */
#define configTASK_NOTIFICATION_ARRAY_ENTRIES    3   /* Ã¿¸öÈÎÎñ 3 ¸öÍ¨ÖªË÷Òý */
#define configUSE_MUTEXES                        1   /* »¥³âËø */
#define configUSE_RECURSIVE_MUTEXES              1   /* µÝ¹÷ô³âËø */
#define configUSE_COUNTING_SEMAPHORES            1   /* ¼ÆÊýÐÅºÅÁ¿ */
#define configUSE_QUEUE_SETS                     0   /* ¶ÓÁ§Þ¯£¨±¾ÏîÄ¿²»ÐèÒª£© */

/* ================================================================
 *  Èý¡¢ÄÚ´æ¹ÜÀí
 * ================================================================ */
#define configTOTAL_HEAP_SIZE                    ( ( size_t ) ( 24 * 1024 ) )  /* 24KB£¬heap_4.c */
#define configSUPPORT_STATIC_ALLOCATION         0   /* 0=²»ÓÃ¾²Ì¬·ÖÅä£¬1=ÐèÒªÊµÏÖ»Øµ÷ */
#define configSUPPORT_DYNAMIC_ALLOCATION        1   /* 1=ÔÊÐí pvPortMalloc */

/* ================================================================
 *  ËÄ¡¢¹³×Óº¯Êý
 *  ×¢Òâ: ¿ªÆôÐèÒªÔÚ main.c ÖÐÊµÏÖ¶ÔÓ¦»Øµ÷º¯Êý£¬·ñÔòÁ´½Ó±¨´í
 * ================================================================ */
#define configUSE_IDLE_HOOK                      0
#define configUSE_TICK_HOOK                      1
#define configCHECK_FOR_STACK_OVERFLOW           2   /* 2=Õ»Òç³ö¼ì²â£¨vApplicationStackOverflowHook ÔÚ mytasks.c£© */
#define configUSE_MALLOC_FAILED_HOOK             1   /* 1=¶ÑºÄ¾¡¼ì²â£¨vApplicationMallocFailedHook ÔÚ mytasks.c£© */

/* ================================================================
 *  Î‰]Èí¼þ¶¨Ê±Æ÷
 * ================================================================ */
#define configUSE_TIMERS                         1
#define configTIMER_TASK_PRIORITY                2   /* ¶¨Ê±Æ÷·þÎñÈÎÎñÓÅÏÈ¼¶ */
#define configTIMER_QUEUE_LENGTH                 10
#define configTIMER_TASK_STACK_DEPTH             ( configMINIMAL_STACK_SIZE * 2 )

/* ================================================================
 *  Áù¡¢¿ÉÑ¡¹¦ÄÜ£¨¹Ø±Õ½ÚÊ¡¿Õ¼ä£©
 * ================================================================ */
#define configUSE_CO_ROUTINES                    0
#define configMAX_CO_ROUTINE_PRIORITIES          2
#define configUSE_TRACE_FACILITY                 1   /* 1=¿ªÆô vTaskList µÈµ÷ÊÔ½Ó¿Ú */
#define configUSE_STATS_FORMATTING_FUNCTIONS     1
#define configGENERATE_RUN_TIME_STATS            0   /* 0=²»Í³¼Æ CPU Õ¼ÓÃ */

/* ================================================================
 *  Æß¡¢NVIC ÓÅÏÈ¼¶ÅäÖÃ (STM32F407: 4 bit ÇÀÕ¼ÓÅÏÈ¼¶)
 *
 *  FreeRTOS Ö»¹ÜÀíÓÅÏÈ¼¶ 11~15 µÄÖ§ØÏ£¨ÊÜ configMAX_SYSCALL_INTERRUPT_PRIORITY ÏÞÖÆ£©
 *  ÓÅÏÈ¼¶ 0~10 µÄÖ§ØÏ²»ÊÜ FreeRTOS Ó°Ïì£¬¿ÉÓÃÓÚ¸ßÊµÊ±ÐÔÍâÉñÔÈç SPI¡¢DMA£©
 *
 *  NVIC ÓÅÏÈ¼¶¼Ä´æÆ÷ÊÇ 8 ¦Ë¿í£¬µ« STM32F407 Ö»ÓÃÁË¸ß 4 ¦Ë
 *  ËùÒÔ configLIBRARY_* µÄÖµÐèÒª×óÒÆ (8 - 4) = 4 ¦Ë²ÅÄÜ§ÕÈë¼Ä´æÆ÷
 *
 *  configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY = 5 Òâ¦Æ×Å:
 *  ÓÅÏÈ¼¶ 5~15 µÄ ISR Ö§áÉÒÔµ÷ÓÃ FreeRTOS API£¨Èç xSemaphoreGiveFromISR£©
 *  ÓÅÏÈ¼¶ 0~4 µÄ ISR Ö§Ó»ÄÜµ÷ÓÃ FreeRTOS API£¨µ«Ö´Ð§Úü¿ì£¬²»ÊÜµ÷¶ÈÆ÷Ó°Ïì£©
 * ================================================================ */
#define configPRIO_BITS                          4   /* STM32F407: 4 ¦ËÓÅÏÈ¼¶ */
#define configLIBRARY_LOWEST_INTERRUPT_PRIORITY         15
#define configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY     5
#define configKERNEL_INTERRUPT_PRIORITY                  ( configLIBRARY_LOWEST_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )
#define configMAX_SYSCALL_INTERRUPT_PRIORITY             ( configLIBRARY_MAX_SYSCALL_INTERRUPT_PRIORITY << (8 - configPRIO_BITS) )

/* ================================================================
 *  °Ë¡¢INCLUDE º¯Êý¿ª¹Ø
 *  ÉèÎª 1 Ôò±àÒë¶ÔÓ¦µÄ FreeRTOS API£¬ÉèÎª 0 Ôò²¨¹ôµô£¨½ÚÊ¡ ROM£©
 * ================================================================ */
#define INCLUDE_vTaskPrioritySet                 1
#define INCLUDE_uxTaskPriorityGet                1
#define INCLUDE_vTaskDelete                      1
#define INCLUDE_vTaskSuspend                     1
#define INCLUDE_xTaskResumeFromISR               1
#define INCLUDE_vTaskDelayUntil                  1
#define INCLUDE_vTaskDelay                       1
#define INCLUDE_xTaskGetSchedulerState           1   /* delay_ms ÖÐÅ§ØÏµ÷¶ÈÆ÷×´Ì¬ */
#define INCLUDE_xTaskGetCurrentTaskHandle        1   /* stream_buffer.c ÐèÒª */
#define INCLUDE_xTaskGetIdleTaskHandle           1   /* µ÷ÊÔ£ºÈ¡¿ÕÏÐÈÎÎñTCBµØÖ·£¬ÏÂÓ²¼þ¹Û²ìµãÓÃ */
#define INCLUDE_uxTaskGetStackHighWaterMark      1   /* µ÷ÊÔ£º¼ì²éÕ»Ê¹ÓÃÂÊ */
#define INCLUDE_eTaskGetState                    1   /* µ÷ÊÔ£º²éÑ¯ÈÎÎñ×´Ì¬ */
#define INCLUDE_xTimerPendFunctionCall           1   /* ¶¨Ê±Æ÷´Ó ISR µ÷ÓÃ */
#define INCLUDE_xTaskAbortDelay                  1
#define INCLUDE_xTaskGetHandle                   1

/* ================================================================
 *  ¾Å¡¢¶ÏÑÔ£¨µ÷ÊÔÓÃ£¬ËÀÑ­»·¿¨×¡±ãÓÚ·¢ÏÖÎÊÌâ£©
 * ================================================================ */
#define configASSERT( x )  if( ( x ) == 0 ) { taskDISABLE_INTERRUPTS(); for( ;; ); }

#endif /* FREERTOS_CONFIG_H */
