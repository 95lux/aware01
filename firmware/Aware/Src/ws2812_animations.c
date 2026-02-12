#include "drivers/ws2812_driver.h"

// Breathe animation for 4 LEDs (same color on all LEDs)
struct led_animation anim_breathe = {
    .stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
               {.duration = 10, .leds = {{32, 32, 32}, {32, 32, 32}, {32, 32, 32}, {32, 32, 32}}},
               {.duration = 10, .leds = {{64, 64, 64}, {64, 64, 64}, {64, 64, 64}, {64, 64, 64}}},
               {.duration = 10, .leds = {{128, 128, 128}, {128, 128, 128}, {128, 128, 128}, {128, 128, 128}}},
               {.duration = 10, .leds = {{192, 192, 192}, {192, 192, 192}, {192, 192, 192}, {192, 192, 192}}},
               {.duration = 10, .leds = {{255, 255, 255}, {255, 255, 255}, {255, 255, 255}, {255, 255, 255}}},
               {.duration = 10, .leds = {{192, 192, 192}, {192, 192, 192}, {192, 192, 192}, {192, 192, 192}}},
               {.duration = 10, .leds = {{128, 128, 128}, {128, 128, 128}, {128, 128, 128}, {128, 128, 128}}},
               {.duration = 10, .leds = {{64, 64, 64}, {64, 64, 64}, {64, 64, 64}, {64, 64, 64}}},
               {.duration = 10, .leds = {{32, 32, 32}, {32, 32, 32}, {32, 32, 32}, {32, 32, 32}}},
               {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
    .total_stages = 11,
    .duration = 0,
    .running = false};

// Chase animation (only first LED lit, others off)
struct led_animation anim_chase = {.stages = {{.duration = 10, .leds = {{255, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                              {.duration = 10, .leds = {{0, 0, 0}, {255, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                              {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {255, 0, 0}, {0, 0, 0}}},
                                              {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {255, 0, 0}}}},
                                   .total_stages = 4,
                                   .duration = 0,
                                   .running = false};

// Off animation (all LEDs off)
struct led_animation anim_off = {
    .stages = {{.duration = 1, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}}, .total_stages = 1, .duration = 0, .running = false};
