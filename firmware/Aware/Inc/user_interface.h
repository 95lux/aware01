#include "FreeRTOS.h"
#include "adc.h"
#include "settings.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "task.h"
#include "timers.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "drivers/ws2812_driver.h"
#include "project_config.h"
#include "tape_player.h"

struct pot {
    float_t val;
    bool inverted;
};

struct fader_led {
    uint8_t brightness_percent;
    bool inverted; // uses inverted pwm output
    TIM_HandleTypeDef* htim_led;
    uint32_t timer_channel;
};

typedef struct {
    TaskHandle_t userIfTaskHandle;
    struct pot pots[NUM_POT_CHANNELS];
    struct fader_led pot_leds[NUM_POT_LEDS];
} user_interface_init_t;

int user_iface_init(struct calibration_data* calibration,
                    const user_interface_init_t* user_interface_init_cfg,
                    TaskHandle_t userIfTaskHandle);
int user_iface_start();

// convert working pot buffer samples to float values in range [0.0, 1.0]
void user_iface_process(uint32_t notified);
int user_iface_populate_pot_bufs();
int user_iface_calibrate_pitch_pot(struct calibration_data* cal);
void user_iface_set_led_brightness(uint8_t led_index, uint8_t percent);