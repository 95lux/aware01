#include "arm_math.h"

arm_biquad_cascade_stereo_df2T_instance_f32* S;
float32_t iirState[4 * NUM_STAGE_IIR];  // 4 state variables per stage
float32_t iirCoeffs[5 * NUM_STAGE_IIR]; // 5 coefficients per stage (b0, b1, b2, a1, a2)

// b1 = -1.3856, b2 = 0.6, a0 = 0.74641, a1 = -1.4928, a2 = 0.74641

iirCoefs = {0.80744, -1.6149, 0.80744, -1.551, 0.67878};

arm_biquad_cascade_stereo_df2T_init_f32(arm_biquad_cascade_stereo_df2T_instance_f32* S,
                                        uint8_t numStages,
                                        const float32_t* pCoeffs,
                                        float32_t* pState);

void excite_block(int16_t* out_buf, uint32_t block_size, float freq) {
    arm_biquad_cascade_stereo_df2T_f32(S, const float32_t* pSrc, float32_t* pDst, uint32_t blockSize);
}