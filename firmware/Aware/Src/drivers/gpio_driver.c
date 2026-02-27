#include "drivers/gpio_driver.h"

#include "main.h"
#include "queue.h"
#include "stdbool.h"
#include "stm32h7a3xx.h"
#include "stm32h7xx_hal_gpio.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

#include "drivers/ws2812_driver.h"
#include "rtos.h"
#include "tape_player.h"

static struct gpio_config gpio_config;

int init_gpio_interface(TaskHandle_t controlIfTaskHandle,
                        TaskHandle_t userIfTaskHandle,
                        TIM_HandleTypeDef* htim_button1_debounce,
                        TIM_HandleTypeDef* htim_button2_debounce,
                        QueueHandle_t tape_cmd_q) {
    if (controlIfTaskHandle == NULL || userIfTaskHandle == NULL || tape_cmd_q == NULL)
        return -1;

    gpio_config.controlIfTaskHandle = controlIfTaskHandle;
    gpio_config.userIfTaskHandle = userIfTaskHandle;
    gpio_config.tape_cmd_q = tape_cmd_q;
    gpio_config.htim_button1_debounce = htim_button1_debounce;
    gpio_config.htim_button2_debounce = htim_button2_debounce;

    return 0;
}

bool are_both_buttons_pushed() {
    return (HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
            HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET);
}

// used only for calibration, so its ok to block here for a while.
bool wait_for_both_buttons_pushed() {
    uint32_t cycles = 0;
    // TODO: does polling create problems? since its only calibration, maybe just go for polling
    while (!are_both_buttons_pushed()) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
        cycles++;
        if (cycles >= 6000) // waited for 60 seconds, probably no buttons pressed, exit
            return false;
    }
    return true;
}

// used only for calibration, so its ok to block here for a while.
bool wait_for_both_buttons_released() {
    uint32_t cycles = 0;
    // TODO: does polling create problems? since its only calibration, maybe just go for polling
    while (are_both_buttons_pushed()) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
        cycles++;
        if (cycles >= 2000)
            return false;
    }
    return true;
}

void button_debounce_timer_callback(TIM_HandleTypeDef* htim) {
    if (htim->Instance == gpio_config.htim_button1_debounce->Instance) {
        gpio_config.button1_debounce = false;
    } else if (htim->Instance == gpio_config.htim_button2_debounce->Instance) {
        gpio_config.button2_debounce = false;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (gpio_config.userIfTaskHandle == NULL) {
        return;
    }
    BaseType_t hpw = pdFALSE;
    tape_cmd_msg_t msg;

    // button presses trigger notifications to interface task
    if (GPIO_Pin == BUTTON1_IN_Pin) {
        if (gpio_config.button1_debounce) {
            return;
        }
        gpio_config.button1_debounce = true;
        HAL_TIM_Base_Start_IT(gpio_config.htim_button1_debounce);
        xTaskNotifyFromISR(gpio_config.userIfTaskHandle, GPIO_NOTIFY_BUTTON1, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == BUTTON2_IN_Pin) {
        if (gpio_config.button2_debounce) {
            return;
        }
        gpio_config.button2_debounce = true;
        HAL_TIM_Base_Start_IT(gpio_config.htim_button2_debounce);
        xTaskNotifyFromISR(gpio_config.userIfTaskHandle, GPIO_NOTIFY_BUTTON2, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == GATE1_IN_Pin) {
        msg.cmd = TAPE_CMD_PLAY;
        xQueueSendFromISR(gpio_config.tape_cmd_q, &msg, &hpw);
        xTaskNotifyFromISR(gpio_config.userIfTaskHandle, GPIO_NOTIFY_GATE1, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == GATE2_IN_Pin) {
        msg.cmd = TAPE_CMD_RECORD;
        xQueueSendFromISR(gpio_config.tape_cmd_q, &msg, &hpw);
        xTaskNotifyFromISR(gpio_config.userIfTaskHandle, GPIO_NOTIFY_GATE2, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }

    // if (GPIO_Pin == GATE3_IN_Pin) {
    //     msg.cmd = TAPE_CMD_STOP;
    //     valid_cmd = true;
    // }
    if (GPIO_Pin == GATE4_IN_Pin) {
        msg.cmd = TAPE_CMD_SLICE;
        xQueueSendFromISR(gpio_config.tape_cmd_q, &msg, &hpw);
        xTaskNotifyFromISR(gpio_config.userIfTaskHandle, GPIO_NOTIFY_GATE4, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}
