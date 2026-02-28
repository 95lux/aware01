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

int init_control_interface(struct calibration_data* calib_data, TaskHandle_t userIfTaskHandle, ADC_HandleTypeDef* hadc_cvs);
int start_control_interface();
void control_interface_process();

int control_interface_calibrate_voct(struct calibration_data* calib_data);