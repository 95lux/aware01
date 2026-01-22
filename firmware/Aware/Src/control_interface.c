#include "control_interface.h"
#include "adc_interface.h"

#include <FreeRTOS.h>
#include <queue.h>
#include <stdint.h>
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "string.h"

#include "tape_player.h"

static struct control_interface_config* active_cfg = NULL;

int init_control_interface(struct control_interface_config* config, struct calibration_data* calib_data) {
    if (config == NULL || config->hadc_cvs == NULL)
        return -1;

    active_cfg = config;
    active_cfg->calib_data = calib_data;

    return 0;
}

int start_control_interface() {
    // implement something else later if needed
    return 0;
}

// TODO: implement processing of CV samples
int process_cv_samples() {
    // control parameters based on CV inputs
    return 0;
}

void calibrate_C1() {
    uint16_t adc_val = active_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    active_cfg->cv_c1 = float_value(adc_val);
}

void calibrate_offsets() {
    for (size_t i = 0; i < NUM_CV_CHANNELS; ++i) {
        uint16_t adc_val = active_cfg->adc_cv_working_buf[i];
        active_cfg->calib_data->offset[i] = float_value(adc_val);
    }
}

int calibrate_C3() {
    uint16_t adc_val = active_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    float c3 = float_value(adc_val);
    float c1 = active_cfg->cv_c1;
    float delta = c3 - c1;
    if (delta > -0.5f && delta < -0.0f) {
        active_cfg->calib_data->pitch_scale = 24.0f / (c3 - c1);
        active_cfg->calib_data->pitch_offset = 12.0f - active_cfg->calib_data->pitch_scale * c1;
        return 0;
    } else {
        return -1;
    }
}

int control_interface_start_calibration(struct calibration_data* calib_data) {
    // procedure:
    // 1. input C1 voltage, then wait for button press to store C1
    while (!(HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
             HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET)) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
    }
    calibrate_C1();
    // 2. input C3 voltage, then wait for button press to store C3 and compute scale/offset
    while (!(HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
             HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET)) {
        vTaskDelay(pdMS_TO_TICKS(10)); // yield to other tasks
    }
    int res = calibrate_C3();
    calibrate_offsets();
    return res;
}