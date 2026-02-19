#include "FreeRTOS.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_gpio.h"
#include "task.h"

#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "ws2812_animations.h"

// PWM = 153.6 MHz / ((PSC + 1) * (ARR + 1)) = 800 kHz
// with PSC of 0 -> ARR + 1 = ( 153.6 MHz / 800 kHz ) - 1 = 192
// Software timer (TIM17) for animation. Cyclic mode.
// ARR reg -> f=60Hz <=> p=16.666ms
// ARR = 49998 ; PSC = 50

#define TIM_PERIOD 192
#define WS2812_BITS_PER_LED 24
#define WS2812_RESET_SLOTS 50                // >50 µs low
#define WS2812_T0H ((TIM_PERIOD * 33) / 100) // ~0.35 µs
#define WS2812_T1H ((TIM_PERIOD * 66) / 100) // ~0.7 µs
#define WS2812_BRIGHTNESS 170

DMA_BUFFER static uint16_t ws2812_pwm_buf[2][WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS];
static uint8_t buf_active = 0; // index of DMA buffer in use
static uint8_t buf_write = 1;  // index of buffer we can write to safely
static struct ws2812_config* active_config;

void ws2812_init(struct ws2812_config* config) {
    if (config == NULL)
        return;

    //zeroize pwm buffer (idle high at timer output)
    memset(ws2812_pwm_buf, 0, sizeof(ws2812_pwm_buf));

    config->state.mode = WS2812_MODE_OFF;

    // zeroize animation state
    config->state.anim_tick = 0;
    config->state.anim_stage = 0;

    // zeroize led state
    memset(config->state.leds, 0, sizeof(config->state.leds));
    memset(config->state.timeout_ticks, 0, sizeof(config->state.timeout_ticks));
    active_config = config;
}

// helper to get current write buffer
static uint16_t* get_write_buf(void) {
    return ws2812_pwm_buf[buf_write];
}

// helper to get current DMA buffer
static uint16_t* get_dma_buf(void) {
    return ws2812_pwm_buf[buf_active];
}

// swap buffers after writing
static void swap_buffers(void) {
    uint8_t tmp = buf_active;
    buf_active = buf_write;
    buf_write = tmp;
}

/* ----- Internal API ----- */
void ws2812_start() {
    if (active_config == NULL)
        return;

    swap_buffers();
    // start anim timer
    HAL_TIM_Base_Start_IT(active_config->htim_anim);
}

void ws2812_show(TIM_HandleTypeDef* htim, uint32_t channel) {
    // swap write buffer to DMA
    swap_buffers();

    // start DMA with the active buffer
    HAL_TIM_PWM_Start_DMA(htim, channel, (uint32_t*) get_dma_buf(), sizeof(ws2812_pwm_buf[0]) / sizeof(uint16_t));
}

// sets led in pwm buf based on color values (0..255)
void ws2812_set_led(uint32_t idx, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t i = idx * WS2812_BITS_PER_LED;
    uint32_t color;

    // apply global dampening
    r = (r * WS2812_BRIGHTNESS) / 255;
    g = (g * WS2812_BRIGHTNESS) / 255;
    b = (b * WS2812_BRIGHTNESS) / 255;

    color = (g << 16) | (r << 8) | b;

    uint16_t* buf = get_write_buf();

    for (int bit = 23; bit >= 0; bit--) {
        buf[i++] = (color & (1 << bit)) ? WS2812_T1H : WS2812_T0H;
    }
}

static void ws2812_force_animation(struct led_animation* anim) {
    active_config->state.animation = *anim;
    active_config->state.anim_stage = 0;
    active_config->state.anim_tick = 0;
    active_config->state.next_animation = NULL;
}

void ws2812_run_step(void) {
    if (active_config == NULL)
        return;

    switch (active_config->state.mode) {
    case WS2812_MODE_OFF:
        if (!active_config->state.off_initialized) {
            for (int i = 0; i < WS2812_LED_COUNT; i++) {
                ws2812_set_led(i, 0, 0, 0);
            }
            ws2812_show(active_config->htim_pwm, active_config->tim_channel_pwm);
            active_config->state.off_initialized = true;
        }
        break;
    case WS2812_MODE_STATIC:
        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            struct ws2812_led led = active_config->state.leds[i];
            ws2812_set_led(i, led.r, led.g, led.b);
        }
        ws2812_show(active_config->htim_pwm, active_config->tim_channel_pwm);
        break;
    case WS2812_MODE_TRIGGER: {
        bool any_active = false;

        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            if (active_config->state.timeout_ticks[i] > 0) {
                active_config->state.timeout_ticks[i]--;
                ws2812_set_led(i, active_config->state.leds[i].r, active_config->state.leds[i].g, active_config->state.leds[i].b);
                any_active = true;
            } else {
                // timeout expired, clear LED state
                active_config->state.leds[i].r = 0;
                active_config->state.leds[i].g = 0;
                active_config->state.leds[i].b = 0;
                ws2812_set_led(i, 0, 0, 0);
            }
        }

        ws2812_show(active_config->htim_pwm, active_config->tim_channel_pwm);

        // If no LEDs are active, go back to off mode
        if (!any_active) {
            active_config->state.mode = WS2812_MODE_OFF;
        }
    } break;

    case WS2812_MODE_ANIMATION: {
        struct led_animation* anim = &active_config->state.animation;

        if (anim->total_stages == 0)
            return;

        uint32_t current_stage_index = active_config->state.anim_stage;
        struct led_animation_stage* stage = &anim->stages[current_stage_index];

        struct led_animation_stage empty_stage = {.duration = 0, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}};
        struct led_animation_stage* next_stage;

        if (current_stage_index + 1 < anim->total_stages) {
            next_stage = &anim->stages[current_stage_index + 1];
        } else if (anim->duration == 0) {
            // looping, wrap to first stage
            next_stage = &anim->stages[0];
        } else {
            // non-looping, fade to black
            next_stage = &empty_stage;
        }

        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            struct ws2812_led a = stage->leds[i];
            struct ws2812_led b = next_stage->leds[i];

            // fixed step size per tick (0..255)
#define LERP_STEP 64
            uint8_t r = a.r + ((int32_t) (b.r - a.r) * LERP_STEP) / 256;
            uint8_t g = a.g + ((int32_t) (b.g - a.g) * LERP_STEP) / 256;
            uint8_t bl = a.b + ((int32_t) (b.b - a.b) * LERP_STEP) / 256;

            ws2812_set_led(i, r, g, bl);
        }

        ws2812_show(active_config->htim_pwm, active_config->tim_channel_pwm);

        // Advance time
        active_config->state.anim_tick++;

        if (active_config->state.anim_tick >= stage->duration) {
            // move to next stage
            active_config->state.anim_tick = 0;
            active_config->state.anim_stage++;

            if (active_config->state.anim_stage >= anim->total_stages) {
                if (anim->duration == 0) {
                    // loop forever
                    active_config->state.anim_stage = 0;
                } else if (active_config->state.next_animation != NULL) {
                    // swap in queued animation
                    ws2812_force_animation(active_config->state.next_animation);
                } else {
                    active_config->state.mode = WS2812_MODE_OFF;
                }
            }
        }
    }
    }
}

/* ----- API ----- */

// small delay from gate in to led flash.
// TODO: Measure actual delay and maybe optimize.
void ws2812_trigger_led(uint32_t idx, struct ws2812_led color, uint32_t timeout_ticks) {
    if (idx >= WS2812_LED_COUNT)
        return;
    taskENTER_CRITICAL();
    active_config->state.leds[idx] = color;
    active_config->state.timeout_ticks[idx] = timeout_ticks;
    active_config->state.off_initialized = false;
    active_config->state.mode = WS2812_MODE_TRIGGER;
    taskEXIT_CRITICAL();
}

void ws2812_change_mode(ws2812_mode_t mode) {
    if (active_config == NULL)
        return;

    active_config->state.mode = mode;
}

// TODO: since this is accessed by tasks, we should consider making this thread safe with a mutex or by disabling interrupts briefly while changing animations. For now we will just assume that animations are only changed from the user interface task and that the animation timer callback does not interrupt it, which should be the case as long as the animation steps are not too long.
// For a more robust solution, we could implement a queue of animations and have the timer callback check for new animations at the end of each animation cycle, but for simplicity we will just allow one queued animation.
void ws2812_change_animation(struct led_animation* anim) {
    // only allow direct switch if no animation is running, or the current animation is looping (duration == 0)
    taskENTER_CRITICAL();
    if (active_config->state.animation.duration == 0) {
        // No animation running, start immediately
        active_config->state.animation = *anim;
        active_config->state.anim_stage = 0;
        active_config->state.anim_tick = 0;
        active_config->state.off_initialized = false;
        active_config->state.mode = WS2812_MODE_ANIMATION;
    } else {
        // Animation is running, queue this one
        active_config->state.next_animation = anim;
    }
    taskEXIT_CRITICAL();
}

void ws2812_timer_callback(TIM_HandleTypeDef* htim) {
    // software timer callback, that steps through the animation.
    ws2812_run_step();
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim) {
    if (active_config == NULL)
        return;

    if (htim == active_config->htim_pwm) {
        // when a full PWM cycle is finished, stop the dma to prevent it from restarting immediately and keep the line high (idle state)
        HAL_TIM_PWM_Stop_DMA(htim, active_config->tim_channel_pwm);

        // Make absolutely sure output is low
        __HAL_TIM_SET_COMPARE(htim, active_config->tim_channel_pwm, 0);
        HAL_GPIO_WritePin(RGB_LED_DATA_GPIO_Port, RGB_LED_DATA_Pin, GPIO_PIN_SET);
    }
}