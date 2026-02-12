#include "drivers/ws2812_driver.h"

struct led_animation breathe_anim = {.stages = {{1, 0, 0, 0},
                                                {1, 32, 32, 32},
                                                {1, 64, 64, 64},
                                                {1, 128, 128, 128},
                                                {1, 192, 192, 192},
                                                {1, 255, 255, 255},
                                                {1, 192, 192, 192},
                                                {1, 128, 128, 128},
                                                {1, 64, 64, 64},
                                                {1, 32, 32, 32},
                                                {1, 0, 0, 0}},
                                     .total_stages = 11,
                                     .duration = 0};

struct led_animation chase_anim = {
    .stages = {{2, 255, 255, 255}, {2, 0, 0, 0}, {2, 0, 0, 0}, {2, 0, 0, 0}}, .total_stages = 4, .duration = 0};
