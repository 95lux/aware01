#include "param_cache.h"
#include "FreeRTOS.h"
#include "atomic.h"
#include "task.h"

static struct param_cache cache;
static uint32_t dirty_flags;

/* ===== Writers ===== */

void param_cache_set_pitch_cv(float v) {
    taskENTER_CRITICAL(); // disable interrupts / preemption
    cache.pitch_cv = v;
    dirty_flags |= PARAM_DIRTY_PITCH_CV; // OR the dirty bit
    taskEXIT_CRITICAL();                 // re-enable interrupts
}

void param_cache_set_pitch_ui(float v) {
    taskENTER_CRITICAL();
    cache.pitch_ui = v;
    dirty_flags |= PARAM_DIRTY_PITCH_UI;
    taskEXIT_CRITICAL();
}

/* ===== Reader ===== */
uint32_t param_cache_fetch(struct param_cache* out) {
    uint32_t dirty;

    taskENTER_CRITICAL(); // disable interrupts / preemption
    dirty = dirty_flags;
    dirty_flags = 0;
    taskEXIT_CRITICAL();

    if (dirty & PARAM_DIRTY_PITCH_CV)
        out->pitch_cv = cache.pitch_cv;

    if (dirty & PARAM_DIRTY_PITCH_UI)
        out->pitch_ui = cache.pitch_ui;

    return dirty;
}
