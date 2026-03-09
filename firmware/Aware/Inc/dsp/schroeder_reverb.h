#pragma once

#include <stdint.h>

#include "ressources.h"

#define SR_COMBS 4
#define SR_ALLPASSES 2

/** @brief Delay line used for both comb and allpass filters. */
typedef struct {
    float feedback;
    float* buf;
    uint32_t size;   /**< Buffer capacity in samples. */
    uint32_t idx;
    uint16_t length; /**< Active delay length in samples (<= size). */

    float lp_state;
    float lp_alpha;  /**< One-pole LP coefficient in the comb feedback path. */
} sr_delay_t;

/** @brief One stereo channel: parallel combs followed by series allpasses. */
typedef struct {
    sr_delay_t combs[SR_COMBS];
    sr_delay_t allpasses[SR_ALLPASSES];
    arm_biquad_cascade_df2T_instance_f32
        iir_lp_instance; /**< Optional post-allpass lowpass to tame HF tail. */
    float32_t iir_lp_state[lp_fc2k_but_NUM_STAGES * 4];
} sr_channel_t;

/** @brief Stereo Schroeder reverberator state. */
typedef struct {
    sr_channel_t left;
    sr_channel_t right;
    float wet;
    float dry;
    float size; /**< Room size scalar [MIN_ROOM_SIZE, 1.0]. */

    float32_t* iir_lp_coeffs;
} schroeder_stereo_t;

/** @brief Initialise reverb state and zero all delay buffers. */
void schroeder_rev_init(schroeder_stereo_t* rev);

/** @brief Process one stereo sample pair. */
void schroeder_rev_process(schroeder_stereo_t* rev, float inL, float inR, float* outL, float* outR);

/** @brief Set wet/dry mix. @p wet in [0, 1]; dry = 1 - wet. */
void schroeder_rev_set_wet(schroeder_stereo_t* rev, float wet);

/** @brief Set comb/allpass feedback (clamped to [0, 0.999]). Controls RT60. */
void schroeder_rev_set_feedback(schroeder_stereo_t* rev, float feedback);

/** @brief Set room size scalar [MIN_ROOM_SIZE, 1.0]. Scales all delay lengths. */
void schroeder_rev_set_size(schroeder_stereo_t* rev, float size);

/** @brief Set one-pole LP alpha for all comb feedback paths. @p alpha in [0, 1]. */
void schroeder_rev_set_lp_alpha(schroeder_stereo_t* rev, float alpha);