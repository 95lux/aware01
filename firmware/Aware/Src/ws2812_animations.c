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

struct led_animation anim_breathe_red = {.stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                    {.duration = 10, .leds = {{32, 0, 0}, {32, 0, 0}, {32, 0, 0}, {32, 0, 0}}},
                                                    {.duration = 10, .leds = {{64, 0, 0}, {64, 0, 0}, {64, 0, 0}, {64, 0, 0}}},
                                                    {.duration = 10, .leds = {{128, 0, 0}, {128, 0, 0}, {128, 0, 0}, {128, 0, 0}}},
                                                    {.duration = 10, .leds = {{192, 0, 0}, {192, 0, 0}, {192, 0, 0}, {192, 0, 0}}},
                                                    {.duration = 10, .leds = {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}}},
                                                    {.duration = 10, .leds = {{192, 0, 0}, {192, 0, 0}, {192, 0, 0}, {192, 0, 0}}},
                                                    {.duration = 10, .leds = {{128, 0, 0}, {128, 0, 0}, {128, 0, 0}, {128, 0, 0}}},
                                                    {.duration = 10, .leds = {{64, 0, 0}, {64, 0, 0}, {64, 0, 0}, {64, 0, 0}}},
                                                    {.duration = 10, .leds = {{32, 0, 0}, {32, 0, 0}, {32, 0, 0}, {32, 0, 0}}},
                                                    {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                         .total_stages = 11,
                                         .duration = 0,
                                         .running = false};

struct led_animation anim_breathe_blue = {.stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 32}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 64}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 128}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 192}}},
                                                     {.duration = 10, .leds = {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 192}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 128}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 64}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 32}}},
                                                     {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                          .total_stages = 11,
                                          .duration = 0,
                                          .running = false};

// LED1 only
struct led_animation anim_breathe_led1 = {.stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 255}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                          .total_stages = 11,
                                          .duration = 0,
                                          .running = false};

// LED1 + LED2
struct led_animation anim_breathe_led2 = {.stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 255}, {0, 0, 255}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                          .total_stages = 11,
                                          .duration = 0,
                                          .running = false};

// LED1 + LED2 + LED3
struct led_animation anim_breathe_led3 = {.stages = {{.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 0}}},
                                                     {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                          .total_stages = 11,
                                          .duration = 0,
                                          .running = false};

struct led_animation anim_breathe_blue_fast = {.stages = {{.duration = 1, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                          {.duration = 1, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 32}}},
                                                          {.duration = 1, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 64}}},
                                                          {.duration = 1, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 128}}},
                                                          {.duration = 1, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 192}}},
                                                          {.duration = 1, .leds = {{0, 0, 255}, {0, 0, 255}, {0, 0, 255}, {0, 0, 255}}},
                                                          {.duration = 1, .leds = {{0, 0, 192}, {0, 0, 192}, {0, 0, 192}, {0, 0, 192}}},
                                                          {.duration = 1, .leds = {{0, 0, 128}, {0, 0, 128}, {0, 0, 128}, {0, 0, 128}}},
                                                          {.duration = 1, .leds = {{0, 0, 64}, {0, 0, 64}, {0, 0, 64}, {0, 0, 64}}},
                                                          {.duration = 1, .leds = {{0, 0, 32}, {0, 0, 32}, {0, 0, 32}, {0, 0, 32}}},
                                                          {.duration = 1, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
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

// Setting confirmed animation: flash green 3 times
struct led_animation anim_setting_confirmed = {.stages =
                                                   {// Flash 1 ON
                                                    {.duration = 10, .leds = {{0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}},
                                                    // Flash 1 OFF
                                                    {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                    // Flash 2 ON
                                                    {.duration = 10, .leds = {{0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}},
                                                    // Flash 2 OFF
                                                    {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                    // Flash 3 ON
                                                    {.duration = 10, .leds = {{0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}},
                                                    // Flash 3 OFF
                                                    {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                               .total_stages = 6,
                                               .duration = 60,
                                               .running = false};

// Setting confirmed animation: flash green 3 times
struct led_animation anim_setting_step_confirmed = {.stages =
                                                        {// Flash 1 ON
                                                         {.duration = 10, .leds = {{0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}},
                                                         // Flash 1 OFF
                                                         {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                         // Flash 2 ON
                                                         {.duration = 10, .leds = {{0, 255, 0}, {0, 255, 0}, {0, 255, 0}, {0, 255, 0}}},
                                                         // Flash 2 OFF
                                                         {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                                    .total_stages = 4,
                                                    .duration = 40,
                                                    .running = false};

// Setting error animation: flash red 3 times
struct led_animation anim_setting_error = {.stages =
                                               {// Flash 1 ON
                                                {.duration = 10, .leds = {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}}},
                                                // Flash 1 OFF
                                                {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                // Flash 2 ON
                                                {.duration = 10, .leds = {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}}},
                                                // Flash 2 OFF
                                                {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}},
                                                // Flash 3 ON
                                                {.duration = 10, .leds = {{255, 0, 0}, {255, 0, 0}, {255, 0, 0}, {255, 0, 0}}},
                                                // Flash 3 OFF
                                                {.duration = 10, .leds = {{0, 0, 0}, {0, 0, 0}, {0, 0, 0}, {0, 0, 0}}}},
                                           .total_stages = 6,
                                           .duration = 60,
                                           .running = false};
