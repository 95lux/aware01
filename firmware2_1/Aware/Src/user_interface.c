#include <FreeRTOS.h>
#include <queue.h>
#include "main.h"
#include "tape_player.h"

void HAL_GPIO_EXTI_Callback(uint16_t GPIO_Pin) {
    BaseType_t xHigherPriorityTaskWoken = pdFALSE;

    tape_cmd_msg_t msg;
    if (GPIO_Pin == BUTTON1_IN_Pin) {
        msg.cmd = TAPE_CMD_PLAY;
    } else if (GPIO_Pin == BUTTON2_IN_Pin) {
        msg.cmd = TAPE_CMD_STOP;
    }

    if (tape_player_send_cmd_from_isr(&msg, &xHigherPriorityTaskWoken) == pdTRUE) {
        portYIELD_FROM_ISR(xHigherPriorityTaskWoken);
    }
}