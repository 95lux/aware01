#include "drivers/ws2812_driver.h"
#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal_gpio.h"
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
#define WS2812_LED_COUNT 4

DMA_BUFFER static uint16_t ws2812_pwm_buf[WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS];
static struct ws2812_config* active_config;

// TODO: Write init function
void ws2812_init(struct ws2812_config* config) {
    // TODO: check config validity for fields needed later
    if (config == NULL)
        return;

    memset(ws2812_pwm_buf, 0, sizeof(ws2812_pwm_buf));

    config->anim_tick = 0;
    config->anim_stage = 0;
    active_config = config;
}

/* ----- Internal API ----- */
void ws2812_start() {
    if (active_config == NULL)
        return;
    // start anim timer
    HAL_TIM_Base_Start_IT(active_config->htim_anim);
}

void ws2812_show(TIM_HandleTypeDef* htim, uint32_t channel) {
    HAL_TIM_PWM_Start_DMA(htim, channel, (uint32_t*) ws2812_pwm_buf, sizeof(ws2812_pwm_buf) / sizeof(uint16_t));
}

void ws2812_set_led(uint32_t idx, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t i = idx * WS2812_BITS_PER_LED;
    uint32_t color = (g << 16) | (r << 8) | b;

    for (int bit = 23; bit >= 0; bit--) {
        ws2812_pwm_buf[i++] = (color & (1 << bit)) ? WS2812_T1H : WS2812_T0H;
    }
}

// Optionally, clear buffer (idle high at timer output)
void ws2812_clear(void) {
    for (int i = 0; i < sizeof(ws2812_pwm_buf) / sizeof(ws2812_pwm_buf[0]); i++) {
        ws2812_pwm_buf[i] = 0; // PWM output stays high (idle)
    }
}

static void ws2812_force_animation(struct led_animation* anim) {
    active_config->animation = *anim;
    active_config->anim_stage = 0;
    active_config->anim_tick = 0;
    active_config->next_animation = NULL;
}

// worker function to step through animation
void ws2812_run_animation_step(void) {
    struct led_animation* anim = &active_config->animation;

    struct led_animation_stage* stage = &active_config->animation.stages[active_config->anim_stage];

    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        struct ws2812_led led = stage->leds[i];
        ws2812_set_led(i, led.r, led.g, led.b);
    }
    // TODO: use timer handles from active config, inited in int function
    ws2812_show(active_config->htim_pwm, active_config->tim_channel_pwm);

    active_config->anim_tick++;
    if (active_config->anim_tick >= stage->duration) {
        active_config->anim_tick = 0;
        active_config->anim_stage++;
        if (active_config->anim_stage >= anim->total_stages) {
            if (anim->duration == 0) {
                active_config->anim_stage = 0; // loop
            } else if (active_config->next_animation != NULL) {
                // swap in queued animation
                ws2812_force_animation(active_config->next_animation);
            } else {
                ws2812_force_animation(&anim_off);
            }
        }
    }
}

/* ----- Animation API ----- */
void ws2812_change_animation(struct led_animation* anim) {
    // only allow direct switch if no animation is running, or the current animation is looping (duration == 0)
    if (active_config->animation.duration == 0) {
        // No animation running, start immediately
        active_config->animation = *anim;
        active_config->anim_stage = 0;
        active_config->anim_tick = 0;
    } else {
        // Animation is running, queue this one
        active_config->next_animation = anim;
    }
}

void ws2812_timer_callback(TIM_HandleTypeDef* htim) {
    // software timer callback, that steps through the animation.
    ws2812_run_animation_step();
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