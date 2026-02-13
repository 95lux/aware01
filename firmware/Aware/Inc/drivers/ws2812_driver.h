#pragma once

#include "stdbool.h"
#include "stdint.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include "tim.h"
#include <stdint.h>

struct ws2812_led {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct led_animation_stage {
    uint32_t duration;
    struct ws2812_led leds[4]; // support up to 4 LEDs, can be extended if needed
};

struct led_animation {
    struct led_animation_stage stages[16];
    uint32_t total_stages;
    uint32_t duration;
    bool running;
};

struct ws2812_config {
    TIM_HandleTypeDef* htim_anim;
    TIM_HandleTypeDef* htim_pwm;
    uint32_t tim_channel_pwm;

    struct led_animation animation;
    struct led_animation* next_animation;
    uint32_t anim_tick;
    uint32_t anim_stage;
};

/* ===== Predefined animations ===== */
extern struct led_animation anim_off;
extern struct led_animation anim_breathe;
extern struct led_animation anim_breathe_red;
extern struct led_animation anim_chase;
extern struct led_animation anim_setting_confirmed;
extern struct led_animation anim_setting_step_confirmed;
extern struct led_animation anim_setting_error;
extern struct led_animation anim_breathe_blue;
extern struct led_animation anim_breathe_blue_fast;

/* ===== API ===== */
void ws2812_init(struct ws2812_config* config);
void ws2812_start();

/* ===== Animation control ===== */

void ws2812_change_animation(struct led_animation* anim);
void ws2812_stop_animation(void);
void ws2812_run_animation_step(void);

/* ===== Timer hook (called from HAL_TIM_PeriodElapsedCallback) ===== */
void ws2812_timer_callback(TIM_HandleTypeDef* htim);