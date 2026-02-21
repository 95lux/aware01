#include "param_cache.h"

#include "FreeRTOS.h"
#include "atomic.h"
#include "task.h"

static struct param_cache cache;

/* ===== Writers ===== */
void param_cache_set_pitch_cv(float v) {
    cache.pitch_cv = v;
}

void param_cache_set_pitch_ui(float v) {
    cache.pitch_ui = v;
}

void param_cache_set_env_attack(float attack) {
    cache.env_attack = attack;
}

void param_cache_set_env_decay(float decay) {
    cache.env_decay = decay;
}

void param_cache_set_cyclic(bool cyclic) {
    cache.cyclic_mode = cyclic;
}

void param_cache_set_reverse(bool reverse) {
    cache.reverse_mode = reverse;
}

void param_cache_set_decimation(uint8_t decimation) {
    cache.decimation = decimation;
}

/* ===== Reader ===== */
void param_cache_fetch(struct param_cache* out) {
    out->pitch_cv = cache.pitch_cv;
    out->pitch_ui = cache.pitch_ui;
    out->env_attack = cache.env_attack;
    out->env_decay = cache.env_decay;
    out->cyclic_mode = cache.cyclic_mode;
    out->reverse_mode = cache.reverse_mode;
    out->decimation = cache.decimation;
}
