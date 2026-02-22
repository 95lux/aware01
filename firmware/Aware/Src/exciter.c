#include "arm_math.h"
#include <stdint.h>

#define NUM_STAGE_IIR 5
#define ALPHA 0.4f

struct excite_config {
    arm_biquad_cascade_stereo_df2T_instance_f32* initial_hp_instance;
    float32_t iirState[4 * NUM_STAGE_IIR];  // 4 state variables per stage
    float32_t iirCoeffs[5 * NUM_STAGE_IIR]; // 5 coefficients per stage (b0, b1, b2, a1, a2)
};

static struct excite_config* active_config;

// b1 = -1.3856, b2 = 0.6, a0 = 0.74641, a1 = -1.4928, a2 = 0.74641

void exice_init(struct excite_config* config) {
    config->iirCoeffs[0] = -1.3856;
    config->iirCoeffs[1] = 0.6;
    config->iirCoeffs[2] = 0.74641;
    config->iirCoeffs[3] = -1.4928;
    config->iirCoeffs[4] = 0.74641;

    active_config = config;

    arm_biquad_cascade_stereo_df2T_init_f32(
        active_config->initial_hp_instance, NUM_STAGE_IIR, active_config->iirCoeffs, active_config->iirState);
}

static inline float softclip_sample(float in, float alpha) {
    float abs_in = fabsf(in);

    if (abs_in >= alpha) {
        return (in > 0.0f) ? (2.0f / 3.0f) * alpha : -(2.0f / 3.0f) * alpha;
    }

    float k = 1.0f / (3.0f * alpha * alpha);
    float in2 = in * in;

    return in - k * in * in2;
}

void excite_block(const int16_t* in_buf, int16_t* out_buf, uint32_t block_size, float freq) {
    float work_buf_in[block_size * 2];
    float work_buf_out[block_size * 2];

    for (uint32_t i = 0; i < block_size * 2; i++) {
        work_buf_in[i] = (float) in_buf[i] / 32768.0f;
    }
    // 1. hipass signal
    arm_biquad_cascade_stereo_df2T_f32(active_config->initial_hp_instance, work_buf_in, work_buf_out, block_size);

    // 2. nolinear distortion to create harmonics of decimated signal
    // use cubic softclip, taken from https://wiki.analog.com/resources/tools-software/sigmastudio/toolbox/nonlinearprocessors/standardcubic
    for (uint32_t i = 0; i < block_size; i++) {
        work_buf_out[i] = softclip_sample(work_buf_out[i], ALPHA);
    }

    for (uint32_t i = 0; i < block_size * 2; i++)
        out_buf[i] = (int16_t) (work_buf_out[i] * 32767.0f);
}