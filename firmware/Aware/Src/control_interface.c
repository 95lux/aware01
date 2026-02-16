#include "main.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <stdint.h>

#include "control_interface.h"
#include "drivers/adc_driver.h"
#include "drivers/gpio_driver.h"
#include "drivers/ws2812_driver.h"
#include "param_cache.h"
#include "project_config.h"
#include "ws2812_animations.h"

#include "tape_player.h"

static struct control_interface_config* active_ctrl_interface_cfg = NULL;

// TODO: create substructs for control_calib_data and ui_calib_data
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
void control_interface_process() {
    for (size_t i = 0; i < NUM_CV_CHANNELS; i++) {
        float v = float_value(active_ctrl_interface_cfg->adc_cv_working_buf[i]);
        active_ctrl_interface_cfg->cv_ins[i].val = v;
    }

    float v_oct_normalized = active_ctrl_interface_cfg->cv_ins[CV_V_OCT].val;    // 0..1
    float pitch_scale = active_ctrl_interface_cfg->calib_data->voct_pitch_scale; // pitch_scale = semitones per normalized CV unit
    float pitch_offset = active_ctrl_interface_cfg->calib_data->voct_pitch_offset;

    float semitones = v_oct_normalized * pitch_scale + pitch_offset; // apply offset and scale from calibration
    float pitch_factor_new = powf(2.0f, semitones / 12.0f);          // convert musical pitch (semitones) to linear playback speed

    param_cache_set_pitch_cv(pitch_factor_new);
}

// calibration procedure
// C1 should be 1V
// returns normalized C1 value (0..1) to be used as reference for C3 calibration and pitch calculations
float calibrate_C1() {
    uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    return float_value(adc_val);
}

// read and store offsets for all CV channels
// input 0V here, so that it can be subtracted from future readings to get 0-centered CV values
void calibrate_offsets() {
    for (size_t i = 0; i < NUM_CV_CHANNELS; ++i) {
        uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[i];
        active_ctrl_interface_cfg->calib_data->cv_offset[i] = float_value(adc_val);
    }
}

// C3 should be 3V
int calibrate_C3(float c1) {
    uint16_t adc_val = active_ctrl_interface_cfg->adc_cv_working_buf[ADC_V_OCT_CV];
    float c3 = float_value(adc_val);
    float delta = c3 - c1;
    if (delta > -0.5f && delta < -0.0f) {
        active_ctrl_interface_cfg->calib_data->voct_pitch_scale = 24.0f / (c3 - c1);
        active_ctrl_interface_cfg->calib_data->voct_pitch_offset =
            12.0f - active_ctrl_interface_cfg->calib_data->voct_pitch_scale * c1; // reference point for C0 (because c1 - 12st = c0)
        return 0;
    } else {
        return -1;
    }
}

int control_interface_calibrate_voct(struct calibration_data* calib_data) {
    // procedure:
    // 1. input C1 voltage, then wait for button press to store C1
    if (!wait_for_both_buttons_pushed())
        return -1;
    float c1 = calibrate_C1();
    ws2812_change_animation(&anim_setting_step_confirmed);
    ws2812_change_animation(&anim_breathe_led1);
    wait_for_both_buttons_released();

    // 2. input C3 voltage, then wait for button press to store C3 and compute scale/offset
    if (!wait_for_both_buttons_pushed())
        return -1;
    int res = calibrate_C3(c1);
    ws2812_change_animation(&anim_setting_step_confirmed);
    ws2812_change_animation(&anim_breathe_led2);
    wait_for_both_buttons_released();

    // 3. remove all cables and wait for button press to store offsets (assuming 0V input, so that it can be subtracted from future readings to get 0-centered CV values)
    if (!wait_for_both_buttons_pushed())
        return -1;
    calibrate_offsets();
    wait_for_both_buttons_released();
    ws2812_change_animation(&anim_setting_confirmed);

    return res;
}