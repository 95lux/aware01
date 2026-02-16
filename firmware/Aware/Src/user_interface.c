#include "user_interface.h"

#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "settings.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "string.h"
#include "ws2812_animations.h"
#include <FreeRTOS.h>
#include <queue.h>

#include "drivers/adc_driver.h"
#include "drivers/gpio_driver.h"
#include "param_cache.h"

static struct user_interface_config* active_user_interface_cfg = NULL;

struct pot_pitch_calibration {
    float min;    // pot value at full CCW
    float center; // pot value at detent
    float max;    // pot value at full CW
};

int user_iface_init(struct user_interface_config* config, struct calibration_data* calibration) {
    if (config == NULL)
        return -1;

    for (int i = 0; i < NUM_POT_LEDS; i++) {
        // TODO: || config->pot_leds[i].timer_channel == faulty? check if channel was actually populated.
        if (config->pot_leds[i].htim_led == NULL)
            return -1;
    }

    // init leds with 0;
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        config->pot_leds[i].brightness_percent = 0;
    }

    config->calibration_data = calibration;

    active_user_interface_cfg = config;

    return 0;
}

int user_iface_start() {
    // TODO: apply peacewise linear calibration in user_iface_process.
    // start pwm timers
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        struct fader_led led = active_user_interface_cfg->pot_leds[i];
        // TODO: || config->pot_leds[i].timer_channel == faulty? check if channel was actually populated.
        HAL_StatusTypeDef result = HAL_TIM_PWM_Start(led.htim_led, led.timer_channel);
        if (result != HAL_OK) {
            return -1;
        }
    }

    user_iface_set_led_brightness(0, 30);
    user_iface_set_led_brightness(1, 30);
    return 0;
}

// calculates float values from potentiometer ADC samples
// will also do filtering if needed in the future
// will also do software debouncing of buttons if needed in the future
// TODO: implement processing of user interface data
// and maps them to parameters
void user_iface_process() {
    for (size_t i = 0; i < NUM_POT_CHANNELS; i++) {
        float v = float_value(active_user_interface_cfg->adc_pot_working_buf[i]);
        if (active_user_interface_cfg->pots[i].inverted)
            v = 1.0f - v;

        active_user_interface_cfg->pots[i].val = v;
    }

    float raw = active_user_interface_cfg->pots[POT_PITCH].val;
    struct calibration_data* cal = active_user_interface_cfg->calibration_data;

    float t;
    if (raw <= cal->pitchpot_mid) {
        t = (raw - cal->pitchpot_mid) / (cal->pitchpot_mid - cal->pitchpot_min); // -1 … 0
    } else {
        t = (raw - cal->pitchpot_mid) / (cal->pitchpot_max - cal->pitchpot_mid); // 0 … +1
    }

    t = fmaxf(-1.0f, fminf(t, 1.0f));

    float semitones = t * UI_PITCH_MAX_SEMITONE_RANGE;
    float pitch_factor_new = powf(2.0f, semitones / 12.0f);

    param_cache_set_pitch_ui(pitch_factor_new);
}

// Example: set LED brightness 0..100%
void user_iface_set_led_brightness(uint8_t led_index, uint8_t percent) {
    if (led_index >= NUM_POT_LEDS && led_index < 0)
        return;

    struct fader_led led = active_user_interface_cfg->pot_leds[led_index];

    uint32_t pulse = (led.htim_led->Init.Period + 1) * percent / 100;
    if (led.inverted)
        pulse = led.htim_led->Init.Period - pulse;

    __HAL_TIM_SET_COMPARE(led.htim_led, led.timer_channel, pulse);
}

static int calibrate_pitch_point(float* dst, bool inverted, uint16_t* adc_buf, int step) {
    if (!wait_for_both_buttons_pushed())
        return -1;

    adc_copy_pots_to_working_buf(adc_buf, NUM_POT_CHANNELS);

    float fval = float_value(adc_buf[POT_PITCH]);
    if (inverted)
        fval = 1.0f - fval;

    *dst = fval;

    ws2812_change_animation(&anim_setting_step_confirmed);
    // Step-dependent LED feedback
    switch (step) {
    case 0:
        ws2812_change_animation(&anim_breathe_led1); // first LED
        break;
    case 1:
        ws2812_change_animation(&anim_breathe_led2); // 2 LEDs
        break;
    case 2:
        ws2812_change_animation(&anim_breathe_led3); // 3 LEDs
        break;
    default:
        ws2812_change_animation(&anim_breathe_blue); // fallback
        break;
    }

    wait_for_both_buttons_released();

    return 0;
}

int user_iface_calibrate_pitch_pot(struct calibration_data* cal) {
    uint16_t adc_buf[NUM_POT_CHANNELS];
    bool inverted = active_user_interface_cfg->pots[POT_PITCH].inverted;

    if (calibrate_pitch_point(&cal->pitchpot_min, inverted, adc_buf, 0) < 0)
        return -1;

    if (calibrate_pitch_point(&cal->pitchpot_mid, inverted, adc_buf, 1) < 0)
        return -1;

    if (calibrate_pitch_point(&cal->pitchpot_max, inverted, adc_buf, 2) < 0)
        return -1;

    // sanity check
    if (!(cal->pitchpot_min < cal->pitchpot_mid && cal->pitchpot_mid < cal->pitchpot_max))
        return -1;

    ws2812_change_animation(&anim_setting_confirmed);
    return 0;
}