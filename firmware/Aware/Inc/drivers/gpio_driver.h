#pragma once
#include "FreeRTOS.h"
#include "gpio.h"
#include "queue.h"
#include "stm32h7xx_hal.h"
#include "task.h"

/* notification bits for ADC sources */
#define GPIO_NOTIFY_GATE1 (1U << 0)
#define GPIO_NOTIFY_GATE2 (1U << 1)
#define GPIO_NOTIFY_GATE3 (1U << 2)
#define GPIO_NOTIFY_GATE4 (1U << 3)
#define GPIO_NOTIFY_BUTTON1 (1U << 4)
#define GPIO_NOTIFY_BUTTON2 (1U << 5)

struct gpio_config {
    TaskHandle_t userIfTaskHandle;
    TaskHandle_t controlIfTaskHandle;

    QueueHandle_t tape_cmd_q;
};

int init_gpio_interface(struct gpio_config* config);