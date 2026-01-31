#pragma once

#include "FreeRTOS.h"
#include "adc.h"
#include "stm32h7xx_hal.h"
#include "task.h"
#include <stdbool.h>

#include "project_config.h"
#include "settings.h"
#include "tape_player.h"

struct cv_in {
    float val;
    bool is_v_oct;
};

struct control_interface_config {
    uint16_t adc_cv_working_buf[NUM_CV_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_cvs;

    struct calibration_data* calib_data; // only calibration data for v/oct CV

    struct cv_in cv_ins[NUM_CV_CHANNELS];
};

int init_control_interface(struct control_interface_config* config, struct calibration_data* calib_data);
int start_control_interface();
int control_interface_process(struct parameters* params);

int control_interface_start_calibration(struct calibration_data* calib_data);