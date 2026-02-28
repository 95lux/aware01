#pragma once

#include "arm_math.h"

// Coefficients for IIR filter in CMSIS-DSP format (b0, b1, b2, -a1, -a2)

#define BIQUAD_CASCADE_NUM_STAGES 3

extern float32_t iir_coeffs[BIQUAD_CASCADE_NUM_STAGES * 5];
