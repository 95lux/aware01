#pragma once

#include "stdbool.h"
#include "stdint.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include "tim.h"
#include <stdint.h>

#define WS2812_LED_COUNT 4        // number of LEDs on the device
#define WS2812_ANIM_STAGE_COUNT 4 // maximum number of stages per animation, can be extended if needed

struct ws2812_led {
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

struct led_animation_stage {
    uint32_t duration;
    struct ws2812_led leds[WS2812_LED_COUNT];
};

struct led_animation {
    struct led_animation_stage stages[WS2812_ANIM_STAGE_COUNT];
    uint32_t total_stages;
    uint32_t duration;
    bool running;
};

typedef enum { WS2812_MODE_OFF = 0, WS2812_MODE_STATIC, WS2812_MODE_TRIGGER, WS2812_MODE_ANIMATION } ws2812_mode_t;

struct ws2812_mode_state {
    ws2812_mode_t mode;
    // for static and trigger mode
    struct ws2812_led leds[WS2812_LED_COUNT];
    uint32_t timeout_ticks[WS2812_LED_COUNT];

    // for animation mode
    uint32_t anim_tick;
    uint32_t anim_stage;
    struct led_animation animation;
    struct led_animation* next_animation;

    bool off_initialized;
};

struct ws2812_config {
    TIM_HandleTypeDef* htim_anim;
    TIM_HandleTypeDef* htim_pwm;
    uint32_t tim_channel_pwm;

    struct ws2812_mode_state state;
};

/* ===== API ===== */
void ws2812_init(struct ws2812_config* config);
void ws2812_start();

void ws2812_trigger_led(uint32_t idx, struct ws2812_led color, uint32_t timeout_ticks);
/* ===== Animation control ===== */

void ws2812_change_animation(struct led_animation* anim);
void ws2812_stop_animation(void);
void ws2812_run_step(void);

/* ===== Timer hook (called from HAL_TIM_PeriodElapsedCallback) ===== */
void ws2812_timer_callback(TIM_HandleTypeDef* htim);