#include "arm_math.h"
#include <stdint.h>
#include <string.h>

#include "project_config.h"

#include "exciter.h"

static struct excite_config* active_config;

float alpha = 0.8f;

// TODO: coefficients configurable or at least pass in as parameters from ressources.h
// b1 = -1.3856, b2 = 0.6, a0 = 0.74641, a1 = -1.4928, a2 = 0.74641
void excite_init(struct excite_config* config) {
    active_config = config;

    memset(active_config->iir_in_state, 0, sizeof(active_config->iir_in_state));
    active_config->iir_in_coeffs = iir_coeffs;

    arm_biquad_cascade_stereo_df2T_init_f32(
        &active_config->iir_in_instance, BIQUAD_CASCADE_NUM_STAGES, active_config->iir_in_coeffs, active_config->iir_in_state);

    // arm_biquad_cascade_stereo_df2T_init_f32(
    // &active_config->iir_out_instance, NUM_STAGE_IIR, active_config->iir_out_coeffs, active_config->iir_out_state);
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

static inline float tanh_distortion(float in, float gain) {
    float v = tanhf(in * gain);
    return v / tanhf(gain); // normalize to keep output in [-1, 1]
}

static inline float fast_tanh(float x) {
    const float x2 = x * x;
    return x * (27.0f + x2) / (27.0f + 9.0f * x2);
}

void bitcrusher(const float* input, float* output, uint32_t len, float normfreq, uint8_t bits) {
    float step = 1.0f / (1 << bits); // quantization step
    float phasor = 0.0f;
    float last = 0.0f;

    for (uint32_t i = 0; i < len; i++) {
        phasor += normfreq;
        if (phasor >= 1.0f) {
            phasor -= 1.0f;
            // quantize
            last = step * floorf(input[i] / step + 0.5f);
        }
        output[i] = last;
    }
}

void excite_block(const int16_t* in_buf, int16_t* out_buf, uint32_t block_size, float freq) {
    float work_buf[block_size];

    for (uint32_t i = 0; i < block_size; i++) {
        work_buf[i] = (float) in_buf[i] / 32768.0f;
    }

    uint32_t frames = block_size / 2;
    // 1. hipass signal
    arm_biquad_cascade_stereo_df2T_f32(&active_config->iir_in_instance, work_buf, work_buf, frames);

    // bitcrush the block
    // bitcrusher(work_buf_out, work_buf_out, block_size, 48000, 16);

    // 2. nolinear distortion to create harmonics of decimated signal
    // use cubic softclip, taken from https : //wiki.analog.com/resources/tools-software/sigmastudio/toolbox/nonlinearprocessors/standardcubic
    // for (uint32_t i = 0; i < block_size; i++) {
    //     work_buf[i] = softclip_sample(work_buf[i], alpha);
    //     // work_buf[i] = 0.3 * tanh_distortion(work_buf[i], 15.0f);
    //     work_buf[i] = fast_tanh(work_buf[i]);
    // }

    // arm_biquad_cascade_stereo_df2T_f32(&active_config->iir_out_instance, work_buf, work_buf, frames);

    for (uint32_t i = 0; i < block_size; i++)
        out_buf[i] = (int16_t) (work_buf[i] * 32768.0f);
}