#pragma once

#include "FreeRTOS.h"
#include "adc.h"
#include "stm32h7xx_hal.h"
#include "task.h"
#include <stdbool.h>

#include "project_config.h"
#include "settings.h"

struct control_interface_config {
    uint16_t adc_cv_working_buf[NUM_CV_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_cvs;

    struct calibration_data* calib_data;
    float cv_c1;
};

struct parameters {
    float v_oct;
    float cv1;
    float cv2;
    float cv3;
};

int init_control_interface(struct control_interface_config* config, struct calibration_data* calib_data);
int start_control_interface();
int process_cv_samples(struct parameters* params);
int control_interface_start_calibration(struct calibration_data* calib_data);