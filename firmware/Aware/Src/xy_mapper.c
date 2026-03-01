// XY Mapper
//
// Maps normalized XY CV inputs to DSP parameters and updates the param_cache.

#include "xy_mapper.h"

#include "arm_math.h"
#include <stddef.h>

#include "param_cache.h"

/* ===== XY mapping ranges ===== */
typedef struct {
    void (*setter)(float); // function to update DSP parameter in param_cache
    float min_val;         // output for this half
    float max_val;
    float exponent; // optional curve (1.0 = linear, <1.0 = logarithmicallishy, >1.0 = exponentially)
} xy_half_map_t;

typedef struct {
    xy_half_map_t negative; // for t < 0
    xy_half_map_t positive; // for t >= 0
} xy_map_piecewise_t;

/* Example mappings */
static xy_map_piecewise_t x_map[] = {
    {.negative = {param_cache_set_schroeder_verb_feedback, 0.02f, 1.0f, 0.9f},
     .positive = {param_cache_set_schroeder_verb_feedback, 0.02f, 1.0f, 0.9f}},
    // add more mappings on x axis here
};

static xy_map_piecewise_t y_map[] = {
    {.negative = {param_cache_set_schroeder_verb_size, 0.01f, 1.0f, 1.0f},
     .positive = {param_cache_set_schroeder_verb_size, 0.01f, 1.0f, 1.0f}},
    // add more mappings on y axis here
};

static inline void map_xy_piecewise(float t, const xy_map_piecewise_t* map) {
    if (t < 0.0f) {
        float v = powf(-t, map->negative.exponent); // -t maps [-1..0] to [0..1]
        // Map negative half to 0..1 range (ignore the sign)
        map->negative.setter(map->negative.min_val + v * (map->negative.max_val - map->negative.min_val));
    } else {
        float v = powf(t, map->positive.exponent); // 0..1
        map->positive.setter(map->positive.min_val + v * (map->positive.max_val - map->positive.min_val));
    }
}

/* ===== XY orchestration ===== */
void xy_mapper_update(float x, float y) {
    // X-axis mappings
    for (size_t i = 0; i < sizeof(x_map) / sizeof(x_map[0]); i++) {
        map_xy_piecewise(x, &x_map[i]);
    }

    // Y-axis mappings
    for (size_t i = 0; i < sizeof(y_map) / sizeof(y_map[0]); i++) {
        map_xy_piecewise(y, &y_map[i]);
    }

    // Update cache with raw XY values for display or other uses. Not strictly needed for mapping itself.
    param_cache_set_xy_fx(x, y);
}