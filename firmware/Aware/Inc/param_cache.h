#pragma once
#include <stdint.h>

// TODO: is param cache still needed? maybe just use a single global parameters struct with atomic access? or use a queue for each parameter that gets updated when the parameter changes and read all queues in the audio task at the beginning of each cycle? maybe also add a dirty flag to only update parameters that have changed since last fetch.

struct param_cache {
    float pitch_cv;
    float pitch_ui;
};

/* public API */
void param_cache_set_pitch_cv(float v);
void param_cache_set_pitch_ui(float v);
uint32_t param_cache_fetch(struct param_cache* out);
