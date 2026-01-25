#pragma once

#include "project_config.h"
#include <stdint.h>

struct calibration_data {
    float pitch_offset;            // 4 bytes
    float pitch_scale;             // 4 bytes
    float offset[NUM_CV_CHANNELS]; // 4 * 4 = 16 bytes
}; // total: 24 bytes

struct State {
    uint8_t tobe_reserved0; // 1 byte
    uint8_t tobe_reserved1; // 1 byte
    uint8_t tobe_reserved2; // 1 byte
    uint8_t tobe_reserved3; // 1 byte
}; // total: 4 bytes

struct SettingsData {
    struct calibration_data calibration_data; // 24 bytes
    struct State state;                       // 4 bytes
    uint8_t padding[0];                       // pad to 32 bytes
}; // total: 32 bytes

int write_calibration_data(struct calibration_data* calib_data);
int read_calibration_data(struct calibration_data* calib_data);