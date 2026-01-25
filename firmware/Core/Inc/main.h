/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32h7xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define V_OCT_IN_Pin GPIO_PIN_0
#define V_OCT_IN_GPIO_Port GPIOC
#define CV1_IN_Pin GPIO_PIN_1
#define CV1_IN_GPIO_Port GPIOC
#define CV2_IN_Pin GPIO_PIN_2
#define CV2_IN_GPIO_Port GPIOC
#define CV3_IN_Pin GPIO_PIN_3
#define CV3_IN_GPIO_Port GPIOC
#define FADER1_IN_Pin GPIO_PIN_7
#define FADER1_IN_GPIO_Port GPIOA
#define FADER2_IN_Pin GPIO_PIN_5
#define FADER2_IN_GPIO_Port GPIOC
#define FADER3_IN_Pin GPIO_PIN_0
#define FADER3_IN_GPIO_Port GPIOB
#define FADER4_IN_Pin GPIO_PIN_1
#define FADER4_IN_GPIO_Port GPIOB
#define FAD_LED4_OUT_Pin GPIO_PIN_12
#define FAD_LED4_OUT_GPIO_Port GPIOB
#define FAD_LED3_OUT_Pin GPIO_PIN_13
#define FAD_LED3_OUT_GPIO_Port GPIOB
#define FAD_LED2_OUT_Pin GPIO_PIN_14
#define FAD_LED2_OUT_GPIO_Port GPIOB
#define FAD_LED1_OUT_Pin GPIO_PIN_15
#define FAD_LED1_OUT_GPIO_Port GPIOB
#define GATE4_IN_Pin GPIO_PIN_7
#define GATE4_IN_GPIO_Port GPIOC
#define GATE4_IN_EXTI_IRQn EXTI9_5_IRQn
#define GATE3_IN_Pin GPIO_PIN_9
#define GATE3_IN_GPIO_Port GPIOC
#define GATE2_IN_Pin GPIO_PIN_8
#define GATE2_IN_GPIO_Port GPIOA
#define GATE2_IN_EXTI_IRQn EXTI9_5_IRQn
#define GATE1_IN_Pin GPIO_PIN_9
#define GATE1_IN_GPIO_Port GPIOA
#define GATE1_IN_EXTI_IRQn EXTI9_5_IRQn
#define BUTTON1_IN_Pin GPIO_PIN_10
#define BUTTON1_IN_GPIO_Port GPIOA
#define BUTTON1_IN_EXTI_IRQn EXTI15_10_IRQn
#define BUTTON2_IN_Pin GPIO_PIN_11
#define BUTTON2_IN_GPIO_Port GPIOA
#define BUTTON2_IN_EXTI_IRQn EXTI15_10_IRQn
#define RGB_LED_DATA_Pin GPIO_PIN_12
#define RGB_LED_DATA_GPIO_Port GPIOC

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
