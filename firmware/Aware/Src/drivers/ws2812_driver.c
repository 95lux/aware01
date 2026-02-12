#include "drivers/ws2812_driver.h"
#include "project_config.h"

// PWM = 153.6 MHz / ((PSC + 1) * (ARR + 1)) = 800 kHz
// with PSC of 0 -> ARR + 1 = ( 153.6 MHz / 800 kHz ) - 1 = 192
// Software timer (TIM17) for animation. Cyclic mode.
// ARR reg -> f=60Hz <=> p=16.666ms
// ARR = 49998 ; PSC = 50

#define TIM_PERIOD 192
#define WS2812_BITS_PER_LED 24
#define WS2812_RESET_SLOTS 50          // >50 µs low
#define WS2812_T0H (TIM_PERIOD * 0.33) // ~0.35 µs
#define WS2812_T1H (TIM_PERIOD * 0.66) // ~0.7 µs
#define WS2812_LED_COUNT 4

DMA_BUFFER static uint16_t ws2812_pwm_buf[WS2812_LED_COUNT * WS2812_BITS_PER_LED + WS2812_RESET_SLOTS];
struct ws2812_config active_config;

void ws2812_set_led_inverted(uint32_t idx, uint8_t r, uint8_t g, uint8_t b) {
    uint32_t i = idx * WS2812_BITS_PER_LED;
    uint32_t color = (g << 16) | (r << 8) | b;

    for (int bit = 23; bit >= 0; bit--) {
        // Invert T0H/T1H for Schmitt inverter
        ws2812_pwm_buf[i++] = (color & (1 << bit)) ? (TIM_PERIOD - WS2812_T1H) : (TIM_PERIOD - WS2812_T0H);
    }
}

// Optionally, clear buffer (idle high at timer output)
void ws2812_clear_inverted(void) {
    for (int i = 0; i < sizeof(ws2812_pwm_buf) / sizeof(ws2812_pwm_buf[0]); i++) {
        ws2812_pwm_buf[i] = TIM_PERIOD; // PWM output stays high (idle)
    }
}

// TODO: Write init function
void ws2812_init(TIM_HandleTypeDef* htim, uint32_t channel) {
    HAL_TIM_PWM_Start(htim, channel);
}

void ws2812_start(TIM_HandleTypeDef* htim, uint32_t channel) {
    HAL_TIM_PWM_Start(htim, channel);
}

void ws2812_show(TIM_HandleTypeDef* htim, uint32_t channel) {
    HAL_TIM_PWM_Start_DMA(htim, channel, (uint32_t*) ws2812_pwm_buf, sizeof(ws2812_pwm_buf) / sizeof(uint16_t));
}

#define MAX_LEDS 4

void ws2812_stop_animation() {
    active_config.animation.running = false;
}

void ws2812_run_animation_step(void) {
    struct led_animation* anim = &active_config.animation;

    struct led_animation_stage* stage = &active_config.animation.stages[active_config.anim_stage];

    for (int i = 0; i < WS2812_LED_COUNT; i++) {
        ws2812_set_led_inverted(i, stage->r, stage->g, stage->b);
    }
    // TODO: use timer handles from active config, inited in int function
    ws2812_show(&active_config.htim_anim, TIM_CHANNEL_1);

    active_config.anim_tick++;
    if (active_config.anim_tick >= stage->duration) {
        active_config.anim_tick = 0;
        active_config.anim_stage++;
        if (active_config.anim_stage >= anim->total_stages) {
            if (anim->duration == 0)
                active_config.anim_stage = 0;
            else
                ws2812_stop_animation();
        }
    }
}

void ws2812_start_animation(struct led_animation* anim) {
    active_config.animation = *anim;
    active_config.anim_stage = 0;
    active_config.anim_tick = 0;
}

void ws2812_timer_callback(TIM_HandleTypeDef* htim) {
    // software timer callback, that steps through the animation.

    ws2812_run_animation_step();
}