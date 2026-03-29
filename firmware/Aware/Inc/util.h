/**
 * @file util.h
 * @brief CPU load statistics and utility functions.
 */
#pragma once

#include <stdint.h>

typedef struct {
    uint32_t audio_percent;
    uint32_t control_percent;
    uint32_t userif_percent;
    uint32_t idle_percent;
} cpu_stats_t;

extern volatile cpu_stats_t cpu_stats;

void update_cpu_stats(void);

static inline uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}

/**
 * @brief Simple First-Order IIR Filter (Exponential Smoothing)
 * * @param current The current smoothed value (stored state)
 * @param target  The new raw input value
 * @param alpha   Smoothing factor (0.0 to 1.0). 
 * Lower = smoother/slower, Higher = snappier.
 * @return float  The updated smoothed value
 */
static inline float smooth_filter(float current, float target, float alpha) {
    return current + alpha * (target - current);
}