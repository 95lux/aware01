/**
 * @file user_interface.c
 * @brief Potentiometer calibration, button processing, LED PWM, and mode toggling.
 */
#include "user_interface.h"

#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "rtos.h"
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
#include "util.h"

static struct user_interface_config user_interface_cfg;

struct user_interface_config {
    uint16_t adc_pot_working_buf[NUM_POT_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    struct pot pots[NUM_POT_CHANNELS];
    struct fader_led pot_leds[NUM_POT_LEDS];

    struct calibration_data* calibration_data;

    bool cyclic_mode;
    bool reverse_mode;
    bool last_cyclic_state;
    bool last_reverse_state;
};

struct pot_pitch_calibration {
    float min;    // pot value at full CCW
    float center; // pot value at detent
    float max;    // pot value at full CW
};

int user_iface_init(struct calibration_data* calibration,
                    const user_interface_init_t* user_interface_init_cfg,
                    TaskHandle_t userIfTaskHandle) {
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        if (user_interface_init_cfg->pot_leds[i].htim_led == NULL)
            return -1;
    }

    user_interface_cfg.calibration_data = calibration;
    user_interface_cfg.userIfTaskHandle = userIfTaskHandle;

    memcpy(user_interface_cfg.pots, user_interface_init_cfg->pots, sizeof(user_interface_init_cfg->pots));
    memcpy(user_interface_cfg.pot_leds, user_interface_init_cfg->pot_leds, sizeof(user_interface_init_cfg->pot_leds));

    // init leds with 0;
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        user_interface_cfg.pot_leds[i].brightness_percent = 0;
    }

    user_interface_cfg.cyclic_mode = false;
    user_interface_cfg.reverse_mode = false;

    return 0;
}

int user_iface_start() {
    // start pwm timers
    for (int i = 0; i < NUM_POT_LEDS; i++) {
        struct fader_led led = user_interface_cfg.pot_leds[i];
        HAL_StatusTypeDef result = HAL_TIM_PWM_Start(led.htim_led, led.timer_channel);
        if (result != HAL_OK) {
            return -1;
        }
    }

    user_iface_set_led_brightness(0, 30);
    user_iface_set_led_brightness(1, 30);
    return 0;
}

int user_iface_populate_pot_bufs() {
    int res = adc_copy_pots_to_working_buf(user_interface_cfg.adc_pot_working_buf, NUM_POT_CHANNELS);
    if (res != 0) {
        // skip processing if adc fetch failed
        return -1;
    }
    return 0;
}

// calculates float values from potentiometer ADC samples
// will also do filtering if needed in the future
// will also do software debouncing of buttons if needed in the future

#define DEADZONE 0.01f
#define INV_RANGE (1.0f / (1.0f - DEADZONE)) // compile-time constant

// CubeMX: ADC 12-bit + 32x Oversampling + 1-bit Shift = 16-bit output.
// Smoothing: Hardware handles high-frequency noise; Software IIR handles remaining drift.
// Calibration: Re-run once to lock in the new high-res values.

void user_iface_process_gates(uint32_t notified) {
    if (notified & GPIO_NOTIFY_GATE1) {
        ws2812_trigger_led(0, (struct ws2812_color){.r = 0, .g = 255, .b = 0}, 3);
    }
    if (notified & GPIO_NOTIFY_GATE2) {
        ws2812_trigger_led(1, (struct ws2812_color){.r = 255, .g = 0, .b = 0}, 3);
    }
}

void user_iface_process_pots(void) {
    if (user_iface_populate_pot_bufs() != 0)
        return;

    for (size_t i = 0; i < NUM_POT_CHANNELS; i++) {
        float v = float_value(user_interface_cfg.adc_pot_working_buf[i]);
        if (user_interface_cfg.pots[i].inverted)
            v = 1.0f - v;
        user_interface_cfg.pots[i].val = smooth_filter(user_interface_cfg.pots[i].val, v, 0.1f);
    }

    // Base Pitch potentiometer
    float norm_pitch = user_interface_cfg.pots[POT_PITCH].val;
    struct calibration_data* cal = user_interface_cfg.calibration_data;

#ifdef CONFIG_ENABLE_PITCH_SLIDE_POT
    float pitch_bipolar;

    // peacewise mapping of normalized value
    if (norm_pitch <= cal->pitchpot_mid) {
        pitch_bipolar = (norm_pitch - cal->pitchpot_mid) / (cal->pitchpot_mid - cal->pitchpot_min);
    } else {
        pitch_bipolar = (norm_pitch - cal->pitchpot_mid) / (cal->pitchpot_max - cal->pitchpot_mid);
    }

    // deadzoning around center detent
    if (fabsf(pitch_bipolar) < DEADZONE) {
        pitch_bipolar = 0.0f;
    } else if (pitch_bipolar > 0.0f) {
        pitch_bipolar = (pitch_bipolar - DEADZONE) * INV_RANGE;
    } else {
        pitch_bipolar = (pitch_bipolar + DEADZONE) * INV_RANGE;
    }
    // clamp
    pitch_bipolar = fmaxf(-1.0f, fminf(pitch_bipolar, 1.0f));

    param_cache_set_pitch_ui(powf(2.0f, pitch_bipolar * UI_PITCH_MAX_SEMITONE_RANGE / 12.0f));
#else
    param_cache_set_pitch_ui(1.0f); // bypass pitch slide pot
#endif

    param_cache_set_env_attack(user_interface_cfg.pots[POT_PARAM2].val);
    param_cache_set_env_decay(user_interface_cfg.pots[POT_PARAM3].val);

    // TODO: for now just use power of 2 for decimation. Other values cause pitch issues. Resolve later
    // For continous decimation degradation, this have to be a continuous parameter.
    // For decimaton degradation of power of 2s, this can be optimized by bitshifting.
    // Have to decide which route to go and implement accordingly in both param_cache and tape_player_dsp.
    uint8_t pow = (uint8_t) (user_interface_cfg.pots[POT_PARAM4].val * (MAX_DECIMATION_POW + 1));
    if (pow > MAX_DECIMATION_POW)
        pow = MAX_DECIMATION_POW;
    param_cache_set_decimation(1u << pow);

    // TODO: single LED animation that flickers and glitches the more decimation is set.
    // Maybe reacting on current playback sample values as well. Lots of options here to explore!
}

void user_iface_process_buttons(uint32_t notified) {
    if (notified & GPIO_NOTIFY_BUTTON1) {
        user_interface_cfg.cyclic_mode = !user_interface_cfg.cyclic_mode;
        param_cache_set_cyclic(user_interface_cfg.cyclic_mode);
    }
    if (notified & GPIO_NOTIFY_BUTTON2) {
        user_interface_cfg.reverse_mode = !user_interface_cfg.reverse_mode;
        param_cache_set_reverse(user_interface_cfg.reverse_mode);
    }

    bool cyclic_mode = user_interface_cfg.cyclic_mode;
    bool reverse_mode = user_interface_cfg.reverse_mode;

    if (cyclic_mode != user_interface_cfg.last_cyclic_state || reverse_mode != user_interface_cfg.last_reverse_state) {
        if (cyclic_mode && reverse_mode) {
            ws2812_set_static_color(2, (struct ws2812_color){.r = 128, .g = 0, .b = 128});
        } else if (cyclic_mode) {
            ws2812_set_static_color(2, blue);
        } else if (reverse_mode) {
            ws2812_set_static_color(2, red);
        } else {
            ws2812_set_static_color(2, (struct ws2812_color){.r = 0, .g = 0, .b = 0});
        }
        user_interface_cfg.last_cyclic_state = cyclic_mode;
        user_interface_cfg.last_reverse_state = reverse_mode;
    }
}

// Example: set LED brightness 0..100%
void user_iface_set_led_brightness(uint8_t led_index, uint8_t percent) {
    if (led_index >= NUM_POT_LEDS || led_index < 0)
        return;

    struct fader_led led = user_interface_cfg.pot_leds[led_index];

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

    ws2812_change_animation_all(&anim_setting_step_confirmed);
    // Step-dependent LED feedback
    switch (step) {
    case 0:
        ws2812_change_animation_all(&anim_breathe_led1); // first LED
        break;
    case 1:
        ws2812_change_animation_all(&anim_breathe_led2); // 2 LEDs
        break;
    case 2:
        ws2812_change_animation_all(&anim_breathe_led3); // 3 LEDs
        break;
    default:
        ws2812_change_animation_all(&anim_breathe_blue); // fallback
        break;
    }

    wait_for_both_buttons_released();

    return 0;
}

int user_iface_calibrate_pitch_pot(struct calibration_data* cal) {
    uint16_t adc_buf[NUM_POT_CHANNELS];
    bool inverted = user_interface_cfg.pots[POT_PITCH].inverted;

    if (calibrate_pitch_point(&cal->pitchpot_min, inverted, adc_buf, 0) < 0)
        return -1;

    if (calibrate_pitch_point(&cal->pitchpot_mid, inverted, adc_buf, 1) < 0)
        return -1;

    if (calibrate_pitch_point(&cal->pitchpot_max, inverted, adc_buf, 2) < 0)
        return -1;

    // sanity check
    if (!(cal->pitchpot_min < cal->pitchpot_mid && cal->pitchpot_mid < cal->pitchpot_max))
        return -1;

    ws2812_change_animation_all(&anim_setting_confirmed);
    return 0;
}