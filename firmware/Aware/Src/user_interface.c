#include "user_interface.h"

#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>

#include "drivers/adc_driver.h"

static struct user_interface_config* active_user_interface_cfg = NULL;

int init_user_interface(struct user_interface_config* config) {
    if (config == NULL || config->hadc_pots == NULL)
        return -1;

    active_user_interface_cfg = config;

    return 0;
}

int start_user_interface() {
    return 0;
}

// calculates float values from potentiometer ADC samples
// will also do filtering if needed in the future
// will also do software debouncing of buttons if needed in the future
// TODO: implement processing of user interface data
// and maps them to parameters
int user_interface_process(struct parameters* params) {
    for (size_t i = 0; i < NUM_POT_CHANNELS; i++) {
        float v = float_value(active_user_interface_cfg->adc_pot_working_buf[i]);
        active_user_interface_cfg->pots[i].val = v;
    }

    // TODO: dirty marking maybe in tape player?
    // map pots to tape player params
    float pitch_factor_new = active_user_interface_cfg->pots[POT_PITCH].val * TAPE_PLAYER_PITCH_RANGE; // map [0.0, 1.0] to [0.0, 2.0]
    // e.g., pitch factor 0.0 = stop, 1.0 = normal speed, 2.0 = double speed TODO: evaluate max/min pitch factor and map accordingly.

    // TODO: add smoothing/filtering to avoid zipper noise when turning pots quickly?
    // for now, just check if value changed significantly
    if (fabsf(params->pitch_factor - pitch_factor_new) > 0.001f) {
        params->pitch_factor = pitch_factor_new;
        params->pitch_factor_dirty = true;
    }
    return 0;
}