#include "user_interface.h"

#include <FreeRTOS.h>
#include <queue.h>
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "string.h"

#include "tape_player.h"

// local DMA buffers for adc potentiometer channels - will be allocated in DMA-capable memory, not in FREERTOS task stack!
DMA_BUFFER static int16_t adc_pot_buf[NUM_POT_CHANNELS];

static struct user_interface_config* active_cfg = NULL;

int init_user_interface(struct user_interface_config* config) {
    if (config == NULL || config->hadc_pots == NULL)
        return -1;

    active_cfg = config;

    active_cfg->adc_pot_buf_ptr = &adc_pot_buf[0];

    return 0;
}

int start_user_interface() {
    return 0;
}

int process_pot_samples(int16_t* pot_samples) {
    return 0;
}

// Button interrupt callback
// TODO: move to gpio.c or a dedicated button.c file? since this can only be overwritten once?
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