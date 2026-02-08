#pragma once
#include <stdint.h>

/* dirty flags */
#define PARAM_DIRTY_PITCH_CV (1u << 0)
#define PARAM_DIRTY_PITCH_UI (1u << 1)

struct param_cache {
    float pitch_cv;
    float pitch_ui;
};

/* public API */
void param_cache_set_pitch_cv(float v);
void param_cache_set_pitch_ui(float v);
uint32_t param_cache_fetch(struct param_cache* out);
