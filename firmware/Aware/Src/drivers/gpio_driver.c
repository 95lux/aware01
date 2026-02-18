#include "drivers/gpio_driver.h"

#include "main.h"
#include "queue.h"
#include "stdbool.h"
#include "stm32h7a3xx.h"
#include "stm32h7xx_hal_gpio.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>

#include "rtos.h"
#include "tape_player.h"

static struct gpio_config* active_cfg = NULL;

int init_gpio_interface(struct gpio_config* config) {
    if (config == NULL || config->controlIfTaskHandle == NULL || config->userIfTaskHandle == NULL || config->tape_cmd_q == NULL)
        return -1;

    active_cfg = config;

    return 0;
}

bool are_both_buttons_pushed() {
    return (HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
            HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET);
}

bool wait_for_both_buttons_pushed() {
    uint32_t cycles = 0;
    // TODO: does polling create problems? since its only calibration, maybe just go for polling
    while (!(HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
             HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET)) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
        cycles++;
        if (cycles >= 6000) // waited for 60 seconds, probably no buttons pressed, exit
            return false;
    }
    return true;
}

bool wait_for_both_buttons_released() {
    uint32_t cycles = 0;
    // TODO: does polling create problems? since its only calibration, maybe just go for polling
    while (!(HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_SET &&
             HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_SET)) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
        cycles++;
        if (cycles >= 2000)
            return false;
    }
    return true;
}

void button_debounce_timer_callback(TIM_HandleTypeDef* htim) {
    if (htim->Instance == TIM13) {
        active_cfg->button1_debounce = false;
    } else if (htim->Instance == TIM14) {
        active_cfg->button2_debounce = false;
    }
}

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    if (active_cfg == NULL) {
        return;
    }
    /* Temporarily send tape commands directly from the ISR.
       TODO: Planned change: notify the control-interface task here and let it
       translate/forward commands to the tape task. 
       Further processing can be done there (e.g., debouncing, long-press detection, etc.)
       Maybe debouncing in gpio driver directly?
       elaborate later
       */
    BaseType_t hpw = pdFALSE;
    tape_cmd_msg_t msg;
    msg.pitch = 1.0f;

    if (GPIO_Pin == BUTTON1_IN_Pin) {
        if (active_cfg->button1_debounce) {
            return;
        }
        active_cfg->button1_debounce = true;
        HAL_TIM_Base_Start_IT(active_cfg->htim_button1_debounce);
        xTaskNotifyFromISR(active_cfg->controlIfTaskHandle, GPIO_NOTIFY_BUTTON1, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == BUTTON2_IN_Pin) {
        if (active_cfg->button2_debounce) {
            return;
        }
        active_cfg->button2_debounce = true;
        HAL_TIM_Base_Start_IT(active_cfg->htim_button2_debounce);
        xTaskNotifyFromISR(active_cfg->controlIfTaskHandle, GPIO_NOTIFY_BUTTON2, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == GATE1_IN_Pin) {
        msg.cmd = TAPE_CMD_PLAY;
        xQueueSendFromISR(active_cfg->tape_cmd_q, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
        // xTaskNotify(active_cfg->controlIfTaskHandle, GPIO_NOTIFY_GATE1, eSetBits, &hpw);
        // portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == GATE2_IN_Pin) {
        msg.cmd = TAPE_CMD_RECORD;
        xQueueSendFromISR(active_cfg->tape_cmd_q, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
    // if (GPIO_Pin == GATE3_IN_Pin) {
    //     msg.cmd = TAPE_CMD_STOP;
    //     valid_cmd = true;
    // }
    if (GPIO_Pin == GATE4_IN_Pin) {
    }
}
