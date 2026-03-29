/**
 * @file ws2812_animations.h
 * @brief Predefined WS2812B LED animations and colors.
 */
#pragma once

#include "drivers/ws2812_driver.h"

/* ===== colors ===== */
extern struct ws2812_color white;
extern struct ws2812_color blue;
extern struct ws2812_color red;

/* ===== Predefined animations ===== */
extern struct led_animation anim_off;
extern struct led_animation anim_breathe;
extern struct led_animation anim_breathe_red;
extern struct led_animation anim_chase;
extern struct led_animation anim_setting_confirmed;
extern struct led_animation anim_setting_step_confirmed;
extern struct led_animation anim_setting_error;
extern struct led_animation anim_breathe_blue;
extern struct led_animation anim_breathe_blue_fast;
extern struct led_animation anim_breathe_led1;
extern struct led_animation anim_breathe_led2;
extern struct led_animation anim_breathe_led3;
extern struct led_animation anim_bootup;