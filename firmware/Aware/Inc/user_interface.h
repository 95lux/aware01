#include "stm32h7xx_hal.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include "project_config.h"

struct user_interface_config {
    uint16_t adc_pot_working_buf[NUM_POT_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_pots;
};

int init_user_interface(struct user_interface_config* config);
int start_user_interface();