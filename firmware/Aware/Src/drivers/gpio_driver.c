#include "drivers/gpio_driver.h"
#include "main.h"
#include "queue.h"
#include "stdbool.h"
#include "task.h"

#include "tape_player.h"
#include <stdbool.h>

static struct gpio_config* active_cfg = NULL;

int init_gpio_interface(struct gpio_config* config) {
    if (config == NULL || config->controlIfTaskHandle == NULL || config->userIfTaskHandle == NULL)
        return -1;

    active_cfg = config;

    return 0;
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
    bool valid_cmd = false;

    if (GPIO_Pin == BUTTON1_IN_Pin) {
        msg.cmd = TAPE_CMD_PLAY;
        valid_cmd = true;
    }
    if (GPIO_Pin == BUTTON2_IN_Pin) {
        msg.cmd = TAPE_CMD_RECORD;
        valid_cmd = true;
    }
    if (GPIO_Pin == GATE1_IN_Pin) {
        msg.cmd = TAPE_CMD_PLAY;
        valid_cmd = true;
        // xTaskNotify(active_cfg->controlIfTaskHandle, GPIO_NOTIFY_GATE1, eSetBits, &hpw);
        // portYIELD_FROM_ISR(hpw);
    }
    if (GPIO_Pin == GATE2_IN_Pin) {
        msg.cmd = TAPE_CMD_RECORD;
        valid_cmd = true;
    }
    if (GPIO_Pin == GATE3_IN_Pin) {
        msg.cmd = TAPE_CMD_STOP;
        valid_cmd = true;
    }
    if (GPIO_Pin == GATE4_IN_Pin) {
    }

    if (valid_cmd) {
        xQueueSendFromISR(active_cfg->tape_cmd_q, &msg, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}
