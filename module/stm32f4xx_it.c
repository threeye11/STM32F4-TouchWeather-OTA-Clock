/**
  ******************************************************************************
  * @file    Project/STM32F4xx_StdPeriph_Templates/stm32f4xx_it.c 
  * @author  MCD Application Team
  * @version V1.8.1
  * @date    27-January-2022
  * @brief   Main Interrupt Service Routines.
  *          This file provides template for all exceptions handler and 
  *          peripherals interrupt service routine.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2016 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */

/* Includes ------------------------------------------------------------------*/
#include "stm32f4xx_it.h"
#include "FreeRTOS.h"
#include "task.h"
#include <stdio.h>      

/* 故障转储：MSP/PSP 双栈帧（sp[0..7]=R0..R3,R12,LR,PC,xPSR）+ HFSR/CFSR/BFAR */
static void fault_dump(uint32_t tag)
{
    uint32_t *psp = (uint32_t *)__get_PSP();
    uint32_t *msp = (uint32_t *)__get_MSP();

    /* 先停掉可能仍在传输的 safe_printf DMA，否则轮询 printf 会和 DMA
     * 交织乱码，故障现场本身会被破坏 */
    DMA_Cmd(DMA2_Stream7, DISABLE);
    USART_DMACmd(USART1, USART_DMAReq_Tx, DISABLE);

    printf("\r\n[FAULT] tag=%u HFSR=0x%08X CFSR=0x%08X BFAR=0x%08X\r\n",
           (unsigned)tag, (unsigned)SCB->HFSR, (unsigned)SCB->CFSR, (unsigned)SCB->BFAR);
    printf("[FAULT] MSP=0x%08X: PC=0x%08X LR=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X\r\n",
           (unsigned)msp, (unsigned)msp[6], (unsigned)msp[5],
           (unsigned)msp[0], (unsigned)msp[1], (unsigned)msp[2], (unsigned)msp[3]);
    printf("[FAULT] PSP=0x%08X: PC=0x%08X LR=0x%08X R0=0x%08X R1=0x%08X R2=0x%08X R3=0x%08X\r\n",
           (unsigned)psp, (unsigned)psp[6], (unsigned)psp[5],
           (unsigned)psp[0], (unsigned)psp[1], (unsigned)psp[2], (unsigned)psp[3]);

    /* 当前任务名 + 空闲任务 TCB 区域转储 */
    {
        TaskHandle_t cur = xTaskGetCurrentTaskHandle();
        printf("[FAULT] current=%08X name=%08X\r\n",
               (unsigned)cur,
               (unsigned)*(uint32_t *)((uint32_t)cur + 0x34u));   /* TCB pcTaskName */
    }
    {
        TaskHandle_t h = xTaskGetIdleTaskHandle();
        if (h != NULL)
        {
            uint32_t tcb = (uint32_t)h;
            printf("[FAULT] idle-0x40: %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                   (unsigned)*(uint32_t *)(tcb - 0x40u),
                   (unsigned)*(uint32_t *)(tcb - 0x3Cu),
                   (unsigned)*(uint32_t *)(tcb - 0x38u),
                   (unsigned)*(uint32_t *)(tcb - 0x34u),
                   (unsigned)*(uint32_t *)(tcb - 0x30u),
                   (unsigned)*(uint32_t *)(tcb - 0x2Cu),
                   (unsigned)*(uint32_t *)(tcb - 0x28u),
                   (unsigned)*(uint32_t *)(tcb - 0x24u));
            printf("[FAULT] idle+0x00: %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                   (unsigned)*(uint32_t *)(tcb + 0x00u),
                   (unsigned)*(uint32_t *)(tcb + 0x04u),
                   (unsigned)*(uint32_t *)(tcb + 0x08u),
                   (unsigned)*(uint32_t *)(tcb + 0x0Cu),
                   (unsigned)*(uint32_t *)(tcb + 0x10u),
                   (unsigned)*(uint32_t *)(tcb + 0x14u),
                   (unsigned)*(uint32_t *)(tcb + 0x18u),
                   (unsigned)*(uint32_t *)(tcb + 0x1Cu));
            printf("[FAULT] idle+0x20: %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                   (unsigned)*(uint32_t *)(tcb + 0x20u),
                   (unsigned)*(uint32_t *)(tcb + 0x24u),
                   (unsigned)*(uint32_t *)(tcb + 0x28u),
                   (unsigned)*(uint32_t *)(tcb + 0x2Cu),
                   (unsigned)*(uint32_t *)(tcb + 0x30u),
                   (unsigned)*(uint32_t *)(tcb + 0x34u),
                   (unsigned)*(uint32_t *)(tcb + 0x38u),
                   (unsigned)*(uint32_t *)(tcb + 0x3Cu));
            printf("[FAULT] idle+0x40: %08X %08X %08X %08X %08X %08X %08X %08X\r\n",
                   (unsigned)*(uint32_t *)(tcb + 0x40u),
                   (unsigned)*(uint32_t *)(tcb + 0x44u),
                   (unsigned)*(uint32_t *)(tcb + 0x48u),
                   (unsigned)*(uint32_t *)(tcb + 0x4Cu),
                   (unsigned)*(uint32_t *)(tcb + 0x50u),
                   (unsigned)*(uint32_t *)(tcb + 0x54u),
                   (unsigned)*(uint32_t *)(tcb + 0x58u),
                   (unsigned)*(uint32_t *)(tcb + 0x5Cu));
        }
    }
    while (1) {}
}

/** @addtogroup Template_Project
  * @{
  */

/* Private typedef -----------------------------------------------------------*/
/* Private define ------------------------------------------------------------*/
/* Private macro -------------------------------------------------------------*/
/* Private variables ---------------------------------------------------------*/
/* Private function prototypes -----------------------------------------------*/
/* Private functions ---------------------------------------------------------*/

/******************************************************************************/
/*            Cortex-M4 Processor Exceptions Handlers                         */
/******************************************************************************/

/**
  * @brief  This function handles NMI exception.
  * @param  None
  * @retval None
  */
void NMI_Handler(void)
{
}

/**
  * @brief  This function handles Hard Fault exception.
  * @param  None
  * @retval None
  */
void HardFault_Handler(void)
{
  /* 故障发生在任务上下文（PSP），打印现场后停机 */
  fault_dump(0);
}

/**
  * @brief  This function handles Memory Manage exception.
  * @param  None
  * @retval None
  */
void MemManage_Handler(void)
{
  fault_dump(1);
}

/**
  * @brief  This function handles Bus Fault exception.
  * @param  None
  * @retval None
  */
void BusFault_Handler(void)
{
  fault_dump(2);
}

/**
  * @brief  This function handles Usage Fault exception.
  * @param  None
  * @retval None
  */
void UsageFault_Handler(void)
{
  fault_dump(3);
}

/**
  * @brief  This function handles SVCall exception.
  * @param  None
  * @retval None
  */
/* SVC 支叵：任务首次启动时恢复上下文 */
void SVC_Handler(void)
{
	extern void vPortSVCHandler(void);
	vPortSVCHandler();
}

/**
  * @brief  This function handles Debug Monitor exception.
  * @param  None
  * @retval None
  */
void DebugMon_Handler(void)
{
}

/**
  * @brief  This function handles PendSVC exception.
  * @param  None
  * @retval None
  */
/* PendSV 支叵：上下文千莼（任务千莼的实际执行者） */
void PendSV_Handler(void)
{
	extern void xPortPendSVHandler(void);
	xPortPendSVHandler();	
}

/**
  * @brief  This function handles SysTick Handler.
  * @param  None
  * @retval None
  */
/* SysTick 支叵：1ms 系统节拍 */
void SysTick_Handler(void)
{
	extern void xPortSysTickHandler(void);
	xPortSysTickHandler();
}

/******************************************************************************/
/*                 STM32F4xx Peripherals Interrupt Handlers                   */
/*  Add here the Interrupt Handler for the used peripheral(s) (PPP), for the  */
/*  available peripheral interrupt handler's name please refer to the startup */
/*  file (startup_stm32f4xx.s).                                               */
/******************************************************************************/

/**
  * @brief  This function handles PPP interrupt request.
  * @param  None
  * @retval None
  */
/*void PPP_IRQHandler(void)
{
}*/

/**
  * @}
  */ 


