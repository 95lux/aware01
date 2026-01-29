#include "FreeRTOS.h"
#include "adc.h"
#include "stm32h7xx_hal.h"
#include "task.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "project_config.h"
#include "tape_player.h"

struct pot {
    float_t val;
    bool inverted;
};

struct user_interface_config {
    uint16_t adc_pot_working_buf[NUM_POT_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_pots;

    struct pot pots[NUM_POT_CHANNELS];
};

int init_user_interface(struct user_interface_config* config);
int start_user_interface();

// convert working pot buffer samples to float values in range [0.0, 1.0]
int user_interface_process(struct parameters* params);