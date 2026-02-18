#pragma once

#include "FreeRTOS.h"
#include "adc.h"
#include "stm32h7xx_hal.h"
#include "task.h"

#include "project_config.h"
#include <stdint.h>

struct adc_config {
    uint16_t* adc_cv_buf_ptr;
    uint16_t* adc_pot_buf_ptr;

    TaskHandle_t userIfTaskHandle;
    TaskHandle_t controlIfTaskHandle;

    ADC_HandleTypeDef* hadc_pots;
    ADC_HandleTypeDef* hadc_cvs;
};

#define ADC_MAX_VALUE 65535.0f

int init_adc_interface(struct adc_config* config);
int start_adc_interface(void);

int adc_copy_cv_to_working_buf(uint16_t* working_buf, size_t len);
int adc_copy_pots_to_working_buf(uint16_t* working_buf, size_t len);

static inline float float_value(uint16_t val) {
    float v = (float) val / ADC_MAX_VALUE;
    // clamp to [0.0, 1.0]
    if (v < 0.0f)
        v = 0.0f;
    if (v > 1.0f)
        v = 1.0f;
    return v;
}