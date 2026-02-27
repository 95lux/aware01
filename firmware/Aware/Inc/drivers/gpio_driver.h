#pragma once
#include "FreeRTOS.h"
#include "gpio.h"
#include "queue.h"
#include "stdbool.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "task.h"
#include "tim.h"

struct gpio_config {
    TaskHandle_t userIfTaskHandle;
    TaskHandle_t controlIfTaskHandle;

    QueueHandle_t tape_cmd_q;
    QueueHandle_t ui_cmd_q;

    TIM_HandleTypeDef* htim_button1_debounce;
    TIM_HandleTypeDef* htim_button2_debounce;

    bool button1_debounce;
    bool button2_debounce;
};

int init_gpio_interface(TaskHandle_t controlIfTaskHandle,
                        TaskHandle_t userIfTaskHandle,
                        TIM_HandleTypeDef* htim_button1_debounce,
                        TIM_HandleTypeDef* htim_button2_debounce,
                        QueueHandle_t tape_cmd_q);
bool wait_for_both_buttons_pushed();
bool wait_for_both_buttons_released();
bool are_both_buttons_pushed();
void button_debounce_timer_callback(TIM_HandleTypeDef* htim);