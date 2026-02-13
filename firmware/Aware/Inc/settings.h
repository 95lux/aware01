#pragma once

#include "project_config.h"
#include <stdint.h>

// aligned settings partition size needed
#define SETTINGS_SIZE 64

// TODO: evaluate proper usage: pitch_offset: offset to be added to pitch CV input, caused by DC voltage offsets from opamps etc.
// pitch_scale: scaling factor for 1V/octave CV input. Float change per semitone.
// offset[]: per-channel offsets to be subtracted from CV inputs before further processing.
struct calibration_data {
    float voct_pitch_offset;          // 4 bytes
    float voct_pitch_scale;           // 4 bytes
    float cv_offset[NUM_CV_CHANNELS]; // 4 * 4 = 16 bytes
    float pitchpot_min;               // 4 bytes
    float pitchpot_mid;               // 4 bytes
    float pitchpot_max;               // 4 bytes
}; // total: 36 bytes

struct State {
    uint8_t tobe_reserved0; // 1 byte
    uint8_t tobe_reserved1; // 1 byte
    uint8_t tobe_reserved2; // 1 byte
    uint8_t tobe_reserved3; // 1 byte
}; // total: 4 bytes

struct SettingsData {
    struct calibration_data calibration_data; // 36 bytes
    struct State state;                       // 4 bytes
    uint8_t padding[SETTINGS_SIZE - sizeof(struct calibration_data) - sizeof(struct State) -
                    sizeof(uint32_t)]; // 20 bytes to make total size 64 bytes
    uint32_t magic;                    // 4 bytes magic number to verify valid data
} __attribute__((aligned(16)));        // total: 64 bytes

int write_settings_data(const struct SettingsData* settings_data);
int read_settings_data(struct SettingsData* settings_data);
int flash_roundtrip();