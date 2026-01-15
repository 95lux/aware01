/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
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
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
typedef StaticTask_t osStaticThreadDef_t;
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */

/* USER CODE END Variables */
/* Definitions for audioTask */
osThreadId_t audioTaskHandle;
uint32_t audioTaskBuffer[ 128 ];
osStaticThreadDef_t audioTaskControlBlock;
const osThreadAttr_t audioTask_attributes = {
  .name = "audioTask",
  .cb_mem = &audioTaskControlBlock,
  .cb_size = sizeof(audioTaskControlBlock),
  .stack_mem = &audioTaskBuffer[0],
  .stack_size = sizeof(audioTaskBuffer),
  .priority = (osPriority_t) osPriorityRealtime7,
};
/* Definitions for controlIfTask */
osThreadId_t controlIfTaskHandle;
uint32_t controlInterfacBuffer[ 128 ];
osStaticThreadDef_t controlInterfacControlBlock;
const osThreadAttr_t controlIfTask_attributes = {
  .name = "controlIfTask",
  .cb_mem = &controlInterfacControlBlock,
  .cb_size = sizeof(controlInterfacControlBlock),
  .stack_mem = &controlInterfacBuffer[0],
  .stack_size = sizeof(controlInterfacBuffer),
  .priority = (osPriority_t) osPriorityNormal,
};
/* Definitions for userIfTask */
osThreadId_t userIfTaskHandle;
const osThreadAttr_t userIfTask_attributes = {
  .name = "userIfTask",
  .stack_size = 128 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal,
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */

/* USER CODE END FunctionPrototypes */

void StartAudioTask(void *argument);
void StartControlInterfaceTask(void *argument);
void StartUserInterfaceTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of audioTask */
  audioTaskHandle = osThreadNew(StartAudioTask, NULL, &audioTask_attributes);

  /* creation of controlIfTask */
  controlIfTaskHandle = osThreadNew(StartControlInterfaceTask, NULL, &controlIfTask_attributes);

  /* creation of userIfTask */
  userIfTaskHandle = osThreadNew(StartUserInterfaceTask, NULL, &userIfTask_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartAudioTask */
/**
  * @brief  Function implementing the audioTask thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartAudioTask */
void StartAudioTask(void *argument)
{
  /* USER CODE BEGIN StartAudioTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartAudioTask */
}

/* USER CODE BEGIN Header_StartControlInterfaceTask */
/**
* @brief Function implementing the controlIfTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartControlInterfaceTask */
void StartControlInterfaceTask(void *argument)
{
  /* USER CODE BEGIN StartControlInterfaceTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartControlInterfaceTask */
}

/* USER CODE BEGIN Header_StartUserInterfaceTask */
/**
* @brief Function implementing the userIfTask thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartUserInterfaceTask */
void StartUserInterfaceTask(void *argument)
{
  /* USER CODE BEGIN StartUserInterfaceTask */
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
  }
  /* USER CODE END StartUserInterfaceTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */

/* USER CODE END Application */

