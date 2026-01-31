#include "control_interface.h"
#include "drivers/adc_driver.h"

#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <stdint.h>

#include "tape_player.h"

static struct control_interface_config* active_ctrl_interface_cfg = NULL;

int init_control_interface(struct control_interface_config* config, struct calibration_data* calib_data) {
    if (config == NULL || config->hadc_cvs == NULL)
        return -1;

    active_ctrl_interface_cfg = config;
    active_ctrl_interface_cfg->calib_data = calib_data;

    return 0;
}

int start_control_interface() {
    // implement something else later if needed
    return 0;
}

// TODO: use offsets from calibration data to adjust CV values
int control_interface_process(struct parameters* params) {
    for (size_t i = 0; i < NUM_CV_CHANNELS; i++) {
        float v = float_value(active_ctrl_interface_cfg->adc_cv_working_buf[i]);
        active_ctrl_interface_cfg->cv_ins[i].val = v;
    }

    float v_oct_normalized = active_ctrl_interface_cfg->cv_ins[CV_V_OCT].val; // 0..1
    float pitch_scale = active_ctrl_interface_cfg->calib_data->pitch_scale;   // pitch_scale = semitones per normalized CV unit
    float pitch_c0_ref = active_ctrl_interface_cfg->calib_data->pitch_offset;

    float semitones = v_oct_normalized * pitch_scale + pitch_c0_ref; // pitch offset from reference (C0) in semitones
    float pitch_factor_new = powf(2.0f, semitones / 12.0f);          // convert musical pitch (semitones) to linear playback speed

    // TODO: evaluate minimum pitch factor change and outsource as MACRO (instead of 0.001f)
    if (fabsf(params->pitch_factor - pitch_factor_new) > 0.001f) {
        params->pitch_factor = pitch_factor_new;
        params->pitch_factor_dirty = true;
    }
    return 0;
}

// calibration procedure
// C1 should be 1V
void calibrate_C1() {
    uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    active_ctrl_interface_cfg->cv_ins[CV_V_OCT].val = float_value(adc_val);
}

// read and store offsets for all CV channels
void calibrate_offsets() {
    for (size_t i = 0; i < NUM_CV_CHANNELS; ++i) {
        uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[i];
        active_ctrl_interface_cfg->calib_data->offset[i] = float_value(adc_val);
    }
}

// C3 should be 3V
int calibrate_C3() {
    uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    float c3 = float_value(adc_val);
    float c1 = active_ctrl_interface_cfg->cv_ins[CV_V_OCT].val;
    float delta = c3 - c1;
    if (delta > -0.5f && delta < -0.0f) {
        active_ctrl_interface_cfg->calib_data->pitch_scale = 24.0f / (c3 - c1);
        active_ctrl_interface_cfg->calib_data->pitch_offset =
            12.0f - active_ctrl_interface_cfg->calib_data->pitch_scale * c1; // reference point for C0 (because c1 - 12st = c0)
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