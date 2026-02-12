#pragma once

#include "stdbool.h"
#include "stdint.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include "tim.h"
#include <stdint.h>

struct led_animation_stage {
    uint32_t duration;
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct led_animation {
    struct led_animation_stage stages[16];
    uint32_t total_stages;
    uint32_t duration;
    bool running;
};

struct ws2812_config {
    TIM_HandleTypeDef htim_anim;
    TIM_HandleTypeDef htim_pwm;
    struct led_animation animation;
    uint32_t anim_tick;
    uint32_t anim_stage;
};

extern struct led_animation breathe_anim;
extern struct led_animation chase_anim;

/* ===== API ===== */

void ws2812_init(TIM_HandleTypeDef* htim, uint32_t channel);
void ws2812_start(TIM_HandleTypeDef* htim, uint32_t channel);
void ws2812_show(TIM_HandleTypeDef* htim, uint32_t channel);

void ws2812_clear_inverted(void);
void ws2812_set_led_inverted(uint32_t idx, uint8_t r, uint8_t g, uint8_t b);

/* ===== Animation control ===== */

void ws2812_start_animation(struct led_animation* anim);
void ws2812_stop_animation(void);
void ws2812_run_animation_step(void);

/* ===== Timer hook (called from HAL_TIM_PeriodElapsedCallback) ===== */
void ws2812_timer_callback(TIM_HandleTypeDef* htim);