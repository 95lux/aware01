#include "FreeRTOS.h"
#include "adc.h"
#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_tim.h"
#include "task.h"
#include "timers.h"
#include <math.h>
#include <stdbool.h>
#include <stdint.h>

#include "project_config.h"
#include "tape_player.h"

struct pot {
    float_t val;
    bool inverted;
};

struct led {
    uint8_t brightness_percent;
    bool inverted; // uses inverted pwm output
    TIM_HandleTypeDef* htim_led;
    uint32_t timer_channel;
};

struct user_interface_config {
    uint16_t adc_pot_working_buf[NUM_POT_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    struct pot pots[NUM_POT_CHANNELS];
    struct led pot_leds[NUM_POT_LEDS];
};

int user_iface_init(struct user_interface_config* config);
int user_iface_start();

// convert working pot buffer samples to float values in range [0.0, 1.0]
void user_iface_process();

void user_iface_set_led_brightness(uint8_t led_index, uint8_t percent);