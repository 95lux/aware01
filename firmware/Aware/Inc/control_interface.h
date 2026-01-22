#include "stm32h7xx_hal.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include "project_config.h"

struct control_interface_config {
    int16_t* adc_cv_buf_ptr;
    int16_t* adc_pot_buf_ptr;

    int16_t adc_cv_working_buf[NUM_CV_CHANNELS];
    int16_t adc_pot_working_buf[NUM_POT_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_cvs;
};

int init_control_interface(struct control_interface_config* config);
int start_control_interface();