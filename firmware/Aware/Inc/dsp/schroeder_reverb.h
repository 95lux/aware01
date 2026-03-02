#pragma once

#include <stdint.h>

#include "ressources.h"

#define SR_COMBS 4
#define SR_ALLPASSES 2
// delay line structure, used for both comb and allpass filters
typedef struct {
    float feedback;
    float* buf;
    uint32_t size;
    uint32_t idx;
    uint16_t length; // actual delay length in samples, <= size

    float lp_state;
    float lp_alpha; // coefficient for simple one-pole lowpass in the feedback path comb filters
} sr_delay_t;

// channel structure, contains comb and allpass filters
typedef struct {
    sr_delay_t combs[SR_COMBS];
    sr_delay_t allpasses[SR_ALLPASSES];
    arm_biquad_cascade_df2T_instance_f32
        iir_lp_instance; // optional lowpass after the allpass section to tame high frequencies in reverb tail.
    float32_t iir_lp_state[lp_fc2k_but_NUM_STAGES * 4]; // 4 state variables per stage
} sr_channel_t;

// stereo structure, contains left and right channels
typedef struct {
    sr_channel_t left;
    sr_channel_t right;
    float wet;
    float dry;
    float size;

    float32_t* iir_lp_coeffs; // 5 coefficients per stage (b0, b1, b2, a1, a2)
} schroeder_stereo_t;

void schroeder_rev_init(schroeder_stereo_t* rev);
void schroeder_rev_process(schroeder_stereo_t* rev, float inL, float inR, float* outL, float* outR);

void schroeder_rev_set_wet(schroeder_stereo_t* rev, float wet);
void schroeder_rev_set_feedback(schroeder_stereo_t* rev, float feedback);
void schroeder_rev_set_size(schroeder_stereo_t* rev, float size);
void schroeder_rev_set_lp_alpha(schroeder_stereo_t* rev, float alpha);