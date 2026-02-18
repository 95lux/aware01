#pragma once

#include "stdint.h"

typedef enum {
    SAMPLERATE_48KHZ = 48000UL,
    SAMPLERATE_24KHZ = 24000UL,
    SAMPLERATE_12KHZ = 12000UL,
    SAMPLERATE_8KHZ = 8000UL,
} audio_sample_rate_t;

void codec_read_reg(uint8_t reg, uint8_t* buf);
void codec_write_reg(uint8_t reg, uint8_t value);

void codec_beep_test(void);
int codec_check_status();

int8_t codec_i2c_is_ok();
void codec_init(audio_sample_rate_t rate);
