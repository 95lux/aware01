#include "user_interface.h"

#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>

#include "drivers/adc_driver.h"
#include "param_cache.h"

static struct user_interface_config* active_user_interface_cfg = NULL;

int user_iface_init(struct user_interface_config* config) {
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

    active_user_interface_cfg = config;

    return 0;
}

int user_iface_start() {
    // start pwm timers
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        struct led led = active_user_interface_cfg->pot_leds[i];
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

    float pot = active_user_interface_cfg->pots[POT_PITCH].val;
    // 0..1 -> -1..+1
    float bipolar = (pot * 2.0f) - 1.0f;
    // +-semitone range
    float semitones = bipolar * UI_PITCH_MAX_SEMITONE_RANGE;
    // semitones -> playback speed
    float pitch_factor_new = powf(2.0f, semitones / 12.0f);

    param_cache_set_pitch_ui(pitch_factor_new);
}

// Example: set LED brightness 0..100%
void user_iface_set_led_brightness(uint8_t led_index, uint8_t percent) {
    if (led_index >= NUM_POT_LEDS && led_index < 0)
        return;

    struct led led = active_user_interface_cfg->pot_leds[led_index];

    uint32_t pulse = (led.htim_led->Init.Period + 1) * percent / 100;
    if (led.inverted)
        pulse = led.htim_led->Init.Period - pulse;

    __HAL_TIM_SET_COMPARE(led.htim_led, led.timer_channel, pulse);
}