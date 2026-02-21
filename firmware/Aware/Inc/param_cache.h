#pragma once
#include <stdbool.h>
#include <stdint.h>

// Shared memory structure for parameters that are set from multiple sources (UI, CV) and need to be accessed in the audio processing code.

struct param_cache {
    float pitch_cv;
    float pitch_ui;
    float env_attack;
    float env_decay;

    bool cyclic_mode;
    bool reverse_mode;

    uint8_t decimation;
};

/* public API */
void param_cache_set_pitch_cv(float v);
void param_cache_set_pitch_ui(float v);
void param_cache_set_env_attack(float attack);
void param_cache_set_env_decay(float decay);
void param_cache_set_cyclic(bool cyclic);
void param_cache_set_reverse(bool reverse);
void param_cache_set_decimation(uint8_t decimation);

void param_cache_fetch(struct param_cache* out);
