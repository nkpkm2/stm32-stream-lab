/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    stm32f4xx_it.c
  * @brief   Interrupt Service Routines.
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "main.h"
#include "stm32f4xx_it.h"
/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#if defined(STREAM_LAB_R2_W6)
#include "r2_w6_matrix.h"
#if defined(STREAM_LAB_R2_CT)
#include "FreeRTOS.h"
#include "task.h"
#include "r2_ct_control.h"
#endif
#elif defined(STREAM_LAB_R2_W5)
#include "r2_w5_capacity.h"
#elif defined(STREAM_LAB_R2_W4)
#include "r2_w4_roundtrip.h"
#elif defined(STREAM_LAB_R2_W3)
#include "r2_w3_rebind.h"
#elif defined(STREAM_LAB_R3_LIFECYCLE)
#include "FreeRTOS.h"
#include "task.h"
#include "r3_w3_runtime.h"
#if defined(STREAM_LAB_R4_RUNTIME)
#include "r4_runtime_target.h"
#endif
#else
#include "r1_acquisition.h"
#endif
/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN TD */

/* USER CODE END TD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN PV */

/* USER CODE END PV */

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN PFP */

/* USER CODE END PFP */

/* Private user code ---------------------------------------------------------*/
/* USER CODE BEGIN 0 */

/* USER CODE END 0 */

/* External variables --------------------------------------------------------*/
extern DMA_HandleTypeDef hdma_adc1;
extern TIM_HandleTypeDef htim7;

/* USER CODE BEGIN EV */

/* USER CODE END EV */

/******************************************************************************/
/*           Cortex-M4 Processor Interruption and Exception Handlers          */
/******************************************************************************/
/**
  * @brief This function handles Non maskable interrupt.
  */
void NMI_Handler(void)
{
  /* USER CODE BEGIN NonMaskableInt_IRQn 0 */

  /* USER CODE END NonMaskableInt_IRQn 0 */
  /* USER CODE BEGIN NonMaskableInt_IRQn 1 */
   while (1)
  {
  }
  /* USER CODE END NonMaskableInt_IRQn 1 */
}

/**
  * @brief This function handles Hard fault interrupt.
  */
void HardFault_Handler(void)
{
  /* USER CODE BEGIN HardFault_IRQn 0 */

  /* USER CODE END HardFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_HardFault_IRQn 0 */
    /* USER CODE END W1_HardFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Memory management fault.
  */
void MemManage_Handler(void)
{
  /* USER CODE BEGIN MemoryManagement_IRQn 0 */

  /* USER CODE END MemoryManagement_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_MemoryManagement_IRQn 0 */
    /* USER CODE END W1_MemoryManagement_IRQn 0 */
  }
}

/**
  * @brief This function handles Pre-fetch fault, memory access fault.
  */
void BusFault_Handler(void)
{
  /* USER CODE BEGIN BusFault_IRQn 0 */

  /* USER CODE END BusFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_BusFault_IRQn 0 */
    /* USER CODE END W1_BusFault_IRQn 0 */
  }
}

/**
  * @brief This function handles Undefined instruction or illegal state.
  */
void UsageFault_Handler(void)
{
  /* USER CODE BEGIN UsageFault_IRQn 0 */

  /* USER CODE END UsageFault_IRQn 0 */
  while (1)
  {
    /* USER CODE BEGIN W1_UsageFault_IRQn 0 */
    /* USER CODE END W1_UsageFault_IRQn 0 */
  }
}

/**
  * @brief This function handles System service call via SWI instruction.
  */

/**
  * @brief This function handles Debug monitor.
  */
void DebugMon_Handler(void)
{
  /* USER CODE BEGIN DebugMonitor_IRQn 0 */

  /* USER CODE END DebugMonitor_IRQn 0 */
  /* USER CODE BEGIN DebugMonitor_IRQn 1 */

  /* USER CODE END DebugMonitor_IRQn 1 */
}

/**
  * @brief This function handles Pendable request for system service.
  */

/**
  * @brief This function handles System tick timer.
  */

/******************************************************************************/
/* STM32F4xx Peripheral Interrupt Handlers                                    */
/* Add here the Interrupt Handlers for the used peripherals.                  */
/* For the available peripheral interrupt handler names,                      */
/* please refer to the startup file (startup_stm32f4xx.s).                    */
/******************************************************************************/

/**
  * @brief This function handles EXTI line[15:10] interrupts.
  */
void EXTI15_10_IRQHandler(void)
{
  /* USER CODE BEGIN EXTI15_10_IRQn 0 */

  /* USER CODE END EXTI15_10_IRQn 0 */
  HAL_GPIO_EXTI_IRQHandler(USER_BUTTON_PIN);
  /* USER CODE BEGIN EXTI15_10_IRQn 1 */

  /* USER CODE END EXTI15_10_IRQn 1 */
}

/**
  * @brief This function handles TIM7 global interrupt.
  */
void TIM7_IRQHandler(void)
{
  BaseType_t higher_priority_task_woken = pdFALSE;
  /* USER CODE BEGIN TIM7_IRQn 0 */
#if defined(STREAM_LAB_R4_RUNTIME)
  traceISR_ENTER();
#if defined(STREAM_LAB_R4_HW)
  R4_RuntimeTarget_TestPendHighFromLowIrq();
#endif
#endif

  /* USER CODE END TIM7_IRQn 0 */
  HAL_TIM_IRQHandler(&htim7);
  /* USER CODE BEGIN TIM7_IRQn 1 */
#if defined(STREAM_LAB_R4_RUNTIME)
  /* All peripheral IRQ return paths use the port-owned common tail.  The
   * HAL tick cannot wake a task, so this deliberately exercises pdFALSE
   * while retaining exactly one R4 IRQ_EXIT from the ARM_CM4F port macro. */
  portYIELD_FROM_ISR(higher_priority_task_woken);
#endif

  /* USER CODE END TIM7_IRQn 1 */
}

#if defined(STREAM_LAB_R4_HW)
/**
  * @brief Board-only T17 software-pended IRQ.
  * The handler has the same single entry/single common-tail contract as a
  * production peripheral handler.  It deliberately has no HAL callback.
  */
void TIM6_DAC_IRQHandler(void)
{
  BaseType_t higher_priority_task_woken = pdFALSE;

  traceISR_ENTER();
  R4_RuntimeTarget_TestObserveCompletionPendingIrq();
  NVIC_ClearPendingIRQ(TIM6_DAC_IRQn);
  portYIELD_FROM_ISR(higher_priority_task_woken);
}
#endif

#if defined(STREAM_LAB_R2_CT)
/**
  * @brief This function handles DMA1 stream6 global interrupt.
  */
void DMA1_Stream6_IRQHandler(void)
{
  BaseType_t higher_priority_task_woken;

#if defined(STREAM_LAB_R4_RUNTIME)
  /* R2 control traffic may be combined with an R4 profile.  Its existing
   * portYIELD_FROM_ISR tail owns the single matching exit. */
  traceISR_ENTER();
#endif
  higher_priority_task_woken =
      R2_CT_TxDmaIrqHandler() != 0U ? pdTRUE : pdFALSE;
  portYIELD_FROM_ISR(higher_priority_task_woken);
}

/**
  * @brief This function handles USART2 global interrupt.
  */
void USART2_IRQHandler(void)
{
  BaseType_t higher_priority_task_woken;

#if defined(STREAM_LAB_R4_RUNTIME)
  /* See DMA1_Stream6_IRQHandler: one outer entry, port-owned common exit. */
  traceISR_ENTER();
#endif
  higher_priority_task_woken =
      R2_CT_UsartIrqHandler() != 0U ? pdTRUE : pdFALSE;
  portYIELD_FROM_ISR(higher_priority_task_woken);
}
#endif

/**
  * @brief This function handles DMA2 stream0 global interrupt.
  */
void DMA2_Stream0_IRQHandler(void)
{
#if defined(STREAM_LAB_R4_RUNTIME)
  BaseType_t higher_priority_task_woken = pdFALSE;
#if defined(STREAM_LAB_R4_PERTURBATION_AB)
  R4_RuntimeTarget_ObserveDmaServiceEnter();
#endif
  /* The one hardware-IRQ entry for all DMA completion/error paths.  HAL
   * callbacks only accumulate their wake request; the tail below emits the
   * sole matching traceISR_EXIT via portYIELD_FROM_ISR. */
  traceISR_ENTER();
#endif
  /* USER CODE BEGIN DMA2_Stream0_IRQn 0 */
#if defined(STREAM_LAB_R2_W6)
  R2_W6_IrqEnter(DMA2->LISR);
#elif defined(STREAM_LAB_R2_W5)
  R2_W5_IrqEnter(DMA2->LISR);
#elif defined(STREAM_LAB_R2_W4)
  R2_W4_IrqEnter(DMA2->LISR);
#elif defined(STREAM_LAB_R2_W3)
  R2_W3_IrqEnter(DMA2->LISR);
#elif defined(STREAM_LAB_R3_LIFECYCLE)
  /* AdcDbmDriver owns the R3 DMA completion callback chain.  In particular,
   * do not enter the historical R1 acquisition state machine here. */
#else
  R1_Acquisition_DmaIrqEnter(DMA2->LISR);
#endif

  /* USER CODE END DMA2_Stream0_IRQn 0 */
  HAL_DMA_IRQHandler(&hdma_adc1);
  /* USER CODE BEGIN DMA2_Stream0_IRQn 1 */
#if defined(STREAM_LAB_R2_W6)
  R2_W6_IrqExit();
#elif defined(STREAM_LAB_R2_W5)
  R2_W5_IrqExit();
#elif defined(STREAM_LAB_R2_W4)
  R2_W4_IrqExit();
#elif defined(STREAM_LAB_R2_W3)
  R2_W3_IrqExit();
#endif

#if defined(STREAM_LAB_R4_RUNTIME)
#if defined(STREAM_LAB_R3_LIFECYCLE)
  higher_priority_task_woken = R3W3Runtime_TakeDmaYieldRequest();
#endif
  R4_RuntimeTarget_TraceDmaTailYield(
      higher_priority_task_woken != pdFALSE ? 1U : 0U);
#if defined(STREAM_LAB_R4_PERTURBATION_AB)
  R4_RuntimeTarget_ObserveDmaServiceExit();
#endif
  portYIELD_FROM_ISR(higher_priority_task_woken);
#endif

  /* USER CODE END DMA2_Stream0_IRQn 1 */
}

/* USER CODE BEGIN 1 */

/* USER CODE END 1 */
