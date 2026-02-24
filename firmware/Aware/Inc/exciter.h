#pragma once

#include "coefficients.h"
#include "project_config.h"

#include "arm_math.h"
#include <stdint.h>

#define ALPHA 0.4f

struct excite_config {
    arm_biquad_cascade_stereo_df2T_instance_f32 iir_in_instance;
    float32_t iir_in_state[BIQUAD_CASCADE_NUM_STAGES * 4]; // 4 state variables per stage
    float32_t* iir_in_coeffs;                              // 5 coefficients per stage (b0, b1, b2, a1, a2)

    arm_biquad_cascade_stereo_df2T_instance_f32 iir_out_instance;
    float32_t iir_out_state[BIQUAD_CASCADE_NUM_STAGES * 4];  // 4 state variables per stage
    float32_t iir_out_coeffs[BIQUAD_CASCADE_NUM_STAGES * 5]; // 5 coefficients per stage (b0, b1, b2, a1, a2)
};

void excite_init(struct excite_config* config);
void excite_block(const int16_t* in_buf, int16_t* out_buf, uint32_t block_size, float freq);