#pragma once

#include <stdint.h>

#define SR_COMBS 4
#define SR_ALLPASSES 2
// delay line structure, used for both comb and allpass filters
typedef struct {
    float feedback;
    float* buf;
    uint32_t size;
    uint32_t idx;
    uint16_t length; // actual delay length in samples, <= size
} sr_delay_t;

// channel structure, contains comb and allpass filters
typedef struct {
    sr_delay_t combs[SR_COMBS];
    sr_delay_t allpasses[SR_ALLPASSES];
} sr_channel_t;

// stereo structure, contains left and right channels
typedef struct {
    sr_channel_t left;
    sr_channel_t right;
    float wet;
    float dry;
} schroeder_stereo_t;

void schroeder_rev_init(schroeder_stereo_t* rev);
void schroeder_rev_process(schroeder_stereo_t* rev, float inL, float inR, float* outL, float* outR);

void schroeder_rev_set_wet(schroeder_stereo_t* rev, float wet);
void schroeder_rev_set_feedback(schroeder_stereo_t* rev, float feedback);
void schroeder_rev_set_scale(schroeder_stereo_t* rev, float scale);