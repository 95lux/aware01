#include "user_interface.h"

#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "settings.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "string.h"
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
        HAL_TIM_PWM_Start(led.htim_led, led.timer_channel);
    }

    // for testing set some led shit.
    user_iface_set_led_brightness(0, 30);
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

    float pitch_factor_new = 1.0f; // default

    if (raw <= cal->pitchpot_mid) {
        // CCW → center segment
        float t = (raw - cal->pitchpot_min) / (cal->pitchpot_mid - cal->pitchpot_min);
        t = fmaxf(0.0f, fminf(t, 1.0f));
        pitch_factor_new = 1.0f + t * (1.0f - 1.0f); // can map min detent speed if needed
    } else {
        // Center → CW segment
        float t = (raw - cal->pitchpot_mid) / (cal->pitchpot_max - cal->pitchpot_mid);
        t = fmaxf(0.0f, fminf(t, 1.0f));
        pitch_factor_new = 1.0f + t * (UI_PITCH_MAX_SEMITONE_RANGE / 12.0f); // convert semitones to speed factor
        pitch_factor_new = powf(2.0f, pitch_factor_new);                     // playback speed
    }

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

// TODO: LED Mode for calibration routine
int user_iface_calibrate_pitch_pot(struct calibration_data* cal) {
    // CCW (min)
    if (!wait_for_both_buttons())
        return -1;
    cal->pitchpot_min = active_user_interface_cfg->pots[POT_PITCH].val;

    // Center detent
    if (!wait_for_both_buttons())
        return -1;
    cal->pitchpot_mid = active_user_interface_cfg->pots[POT_PITCH].val;

    // CW (max)
    if (!wait_for_both_buttons())
        return -1;
    cal->pitchpot_max = active_user_interface_cfg->pots[POT_PITCH].val;

    // sanity check
    if (!(cal->pitchpot_min < cal->pitchpot_mid && cal->pitchpot_mid < cal->pitchpot_max))
        return -1;

    return 0;
}