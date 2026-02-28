#include "FreeRTOS.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_gpio.h"
#include "task.h"

#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "ws2812_animations.h"

struct ws2812_mode_state {
    volatile ws2812_mode_t mode;
    // for static and trigger mode
    struct ws2812_led leds[WS2812_LED_COUNT];
    volatile uint32_t timeout_ticks[WS2812_LED_COUNT];

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

// PWM = 280 MHz / ((PSC + 1) * (ARR + 1)) = 800 kHz
// with PSC of 0 -> ARR + 1 = ( 280 MHz / 800 kHz ) = 350 -> ARR = 349

// Software timer (TIM17) for animation. Cyclic mode.
// f_TIM17 = 280 MHz / (PSC + 1) = 280 MHz / 51 = 5.490 MHz
// ARR reg -> f = 120 Hz <=> p = 8.333 ms
// ARR = (5.490 MHz / 120) - 1 = 45749 ; PSC = 50

#define TIM_PERIOD 350
#define WS2812_BITS_PER_LED 24
#define WS2812_RESET_SLOTS 50                // >50 µs low
#define WS2812_T0H ((TIM_PERIOD * 33) / 100) // ~0.35 µs
#define WS2812_T1H ((TIM_PERIOD * 66) / 100) // ~0.7 µs
#define WS2812_BRIGHTNESS 170

DMA_BUFFER static uint16_t ws2812_pwm_buf[2][WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS];
static uint8_t buf_active = 0; // index of DMA buffer in use
static uint8_t buf_write = 1;  // index of buffer we can write to safely
static struct ws2812_config ws2812_config;

void ws2812_init(const ws2812_init_t* init_cfg) {
    //zeroize pwm buffer (idle high at timer output)
    memset(ws2812_pwm_buf, 0, sizeof(ws2812_pwm_buf));

    ws2812_config.state.mode = WS2812_MODE_OFF;
    // zeroize animation state
    ws2812_config.state.anim_tick = 0;
    ws2812_config.state.anim_stage = 0;

    // zeroize led state
    memset(ws2812_config.state.leds, 0, sizeof(ws2812_config.state.leds));
    memset(ws2812_config.state.timeout_ticks, 0, sizeof(ws2812_config.state.timeout_ticks));

    ws2812_config.htim_anim = init_cfg->htim_anim;
    ws2812_config.htim_pwm = init_cfg->htim_pwm;
    ws2812_config.tim_channel_pwm = init_cfg->tim_channel_pwm;
    ws2812_config.state.animation = *init_cfg->default_animation;
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
    swap_buffers();
    // start anim timer
    HAL_TIM_Base_Start_IT(ws2812_config.htim_anim);
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
    ws2812_config.state.animation = *anim;
    ws2812_config.state.anim_stage = 0;
    ws2812_config.state.anim_tick = 0;
    ws2812_config.state.next_animation = NULL;
}

void ws2812_run_step(void) {
    switch (ws2812_config.state.mode) {
    case WS2812_MODE_OFF:
        if (!ws2812_config.state.off_initialized) {
            for (int i = 0; i < WS2812_LED_COUNT; i++) {
                ws2812_set_led(i, 0, 0, 0);
            }
            ws2812_show(ws2812_config.htim_pwm, ws2812_config.tim_channel_pwm);
            ws2812_config.state.off_initialized = true;
        }
        break;
    case WS2812_MODE_STATIC:
        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            struct ws2812_led led = ws2812_config.state.leds[i];
            ws2812_set_led(i, led.r, led.g, led.b);
        }
        ws2812_show(ws2812_config.htim_pwm, ws2812_config.tim_channel_pwm);
        break;
    case WS2812_MODE_TRIGGER: {
        bool any_active = false;

        for (int i = 0; i < WS2812_LED_COUNT; i++) {
            if (ws2812_config.state.timeout_ticks[i] > 0) {
                ws2812_config.state.timeout_ticks[i]--;
                ws2812_set_led(i, ws2812_config.state.leds[i].r, ws2812_config.state.leds[i].g, ws2812_config.state.leds[i].b);
                any_active = true;
            } else {
                // timeout expired, clear LED state
                ws2812_config.state.leds[i].r = 0;
                ws2812_config.state.leds[i].g = 0;
                ws2812_config.state.leds[i].b = 0;
                ws2812_set_led(i, 0, 0, 0);
            }
        }

        ws2812_show(ws2812_config.htim_pwm, ws2812_config.tim_channel_pwm);

        // If no LEDs are active, go back to off mode
        if (!any_active) {
            ws2812_config.state.mode = WS2812_MODE_OFF;
        }
    } break;

    case WS2812_MODE_ANIMATION: {
        struct led_animation* anim = &ws2812_config.state.animation;

        if (anim->total_stages == 0)
            return;

        uint32_t current_stage_index = ws2812_config.state.anim_stage;
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

        ws2812_show(ws2812_config.htim_pwm, ws2812_config.tim_channel_pwm);

        // Advance time
        ws2812_config.state.anim_tick++;

        if (ws2812_config.state.anim_tick >= stage->duration) {
            // move to next stage
            ws2812_config.state.anim_tick = 0;
            ws2812_config.state.anim_stage++;

            if (ws2812_config.state.anim_stage >= anim->total_stages) {
                if (anim->duration == 0) {
                    // loop forever
                    ws2812_config.state.anim_stage = 0;
                } else if (ws2812_config.state.next_animation != NULL) {
                    // swap in queued animation
                    ws2812_force_animation(ws2812_config.state.next_animation);
                } else {
                    ws2812_config.state.mode = WS2812_MODE_OFF;
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
    ws2812_config.state.leds[idx] = color;
    ws2812_config.state.timeout_ticks[idx] = timeout_ticks;
    ws2812_config.state.off_initialized = false;
    ws2812_config.state.mode = WS2812_MODE_TRIGGER;
    taskEXIT_CRITICAL();
}

void ws2812_change_mode(ws2812_mode_t mode) {
    ws2812_config.state.mode = mode;
}

// TODO: since this is accessed by tasks, we should consider making this thread safe with a mutex or by disabling interrupts briefly while changing animations. For now we will just assume that animations are only changed from the user interface task and that the animation timer callback does not interrupt it, which should be the case as long as the animation steps are not too long.
// For a more robust solution, we could implement a queue of animations and have the timer callback check for new animations at the end of each animation cycle, but for simplicity we will just allow one queued animation.
void ws2812_change_animation(struct led_animation* anim) {
    // only allow direct switch if no animation is running, or the current animation is looping (duration == 0)
    taskENTER_CRITICAL();
    if (ws2812_config.state.animation.duration == 0) {
        // No animation running, start immediately
        ws2812_config.state.animation = *anim;
        ws2812_config.state.anim_stage = 0;
        ws2812_config.state.anim_tick = 0;
        ws2812_config.state.off_initialized = false;
        ws2812_config.state.mode = WS2812_MODE_ANIMATION;
    } else {
        // Animation is running, queue this one
        ws2812_config.state.next_animation = anim;
    }
    taskEXIT_CRITICAL();
}

void ws2812_timer_callback(TIM_HandleTypeDef* htim) {
    // software timer callback, that steps through the animation.
    ws2812_run_step();
}

void HAL_TIM_PWM_PulseFinishedCallback(TIM_HandleTypeDef* htim) {
    if (htim == ws2812_config.htim_pwm) {
        // when a full PWM cycle is finished, stop the dma to prevent it from restarting immediately and keep the line high (idle state)
        HAL_TIM_PWM_Stop_DMA(htim, ws2812_config.tim_channel_pwm);

        // Make absolutely sure output is low
        __HAL_TIM_SET_COMPARE(htim, ws2812_config.tim_channel_pwm, 0);
        HAL_GPIO_WritePin(RGB_LED_DATA_GPIO_Port, RGB_LED_DATA_Pin, GPIO_PIN_SET);
    }
}