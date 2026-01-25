#include "user_interface.h"

#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>

#include "tape_player.h"

static struct user_interface_config* active_cfg = NULL;

int init_user_interface(struct user_interface_config* config) {
    if (config == NULL || config->hadc_pots == NULL)
        return -1;

    active_cfg = config;

    return 0;
}

int start_user_interface() {
    return 0;
}

int process_pot_samples(int16_t* pot_samples) {
    return 0;
}