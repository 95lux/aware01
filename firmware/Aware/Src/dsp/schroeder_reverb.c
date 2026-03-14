/**
 * @file schroeder_reverb.c
 * @brief Stereo Schroeder reverb: 4 parallel lowpass-comb filters into 2 series allpass filters per channel.
 */
#include "dsp/schroeder_reverb.h"
#include "arm_math.h"
#include <string.h>
/* ---- Prime delays (~48kHz) ---- */
// Base comb & allpass lengths (primes for stereo)
static const uint16_t comb_base[2][4] = {
    {1613, 1553, 1499, 1427}, // left
    {1601, 1567, 1487, 1433}  // right
};

static const uint16_t allpass_base[2][2] = {
    {223, 557}, // left
    {229, 563}  // right
};

/* ---- Static buffers ---- */

// TODO: MAX_COMB_LEN on biggest value in comb_base? this way a scale of 1.0 would mean max delay line lengths.
// Currently some headroom.
#define MAX_COMB_LEN 1620 // max delay for combs
#define MAX_AP_LEN 600    // max delay for allpasses

// ensure feedback stability
// feedback scaling macros: feedback increases as size decreases, to maintain perceptual consistency across different sizes. The exact curve can be tweaked for desired response.
// Base feedback values (for size = 1.0f)
#define COMB_FEEDBACK_BASE 0.972f
#define ALLPASS_FEEDBACK_BASE 0.68f // Allpasses usually need less feedback
// Scaling factor (adjust as needed)
#define FEEDBACK_SCALE_FACTOR 0.005f // Adjust this to control how much feedback increases as size decreases

#define COMB_FEEDBACK(size) (COMB_FEEDBACK_BASE + FEEDBACK_SCALE_FACTOR * (1.0f - size))
#define ALLPASS_FEEDBACK(size) (ALLPASS_FEEDBACK_BASE + FEEDBACK_SCALE_FACTOR * (1.0f - size))

#define MIN_ROOM_SIZE 0.3f // minimum room size to prevent instability at very low sizes

/* Left comb buffers */
static float l_c1[MAX_COMB_LEN];
static float l_c2[MAX_COMB_LEN];
static float l_c3[MAX_COMB_LEN];
static float l_c4[MAX_COMB_LEN];

/* Right comb buffers */
static float r_c1[MAX_COMB_LEN];
static float r_c2[MAX_COMB_LEN];
static float r_c3[MAX_COMB_LEN];
static float r_c4[MAX_COMB_LEN];

/* Left allpass buffers */
static float l_a1[MAX_AP_LEN];
static float l_a2[MAX_AP_LEN];

/* Right allpass buffers */
static float r_a1[MAX_AP_LEN];
static float r_a2[MAX_AP_LEN];

/* ---- Processing ---- */

/** @brief Lowpass-comb filter: feedback with one-pole LP damping. */
static float comb_process(sr_delay_t* d, float in) {
    // calculate read index with wraparound depending on set .length parameter
    uint32_t read_idx = (d->idx + d->size - d->length) % d->size;
    float y = d->buf[read_idx];

    float feedback_signal = y * d->feedback;

    // one-pole lowpass on each combs fb path.
    d->lp_state = (1.0f - d->lp_alpha) * feedback_signal + d->lp_alpha * d->lp_state;
    d->buf[d->idx] = in + d->lp_state;

    d->idx++;
    if (d->idx >= d->size)
        d->idx = 0;

    return y;
}

/** @brief Schroeder allpass filter: unity gain, disperses phase. */
static float allpass_process(sr_delay_t* d, float in) {
    // calculate read index with wraparound depending on set .length parameter
    uint32_t read_idx = (d->idx + d->size - d->length) % d->size;
    float buf = d->buf[read_idx];

    float y = -in + buf;
    d->buf[d->idx] = in + buf * d->feedback;

    d->idx++;
    if (d->idx >= d->size)
        d->idx = 0;

    return y;
}

void schroeder_rev_init(schroeder_stereo_t* rev) {
    memset(l_c1, 0, sizeof(l_c1));
    memset(l_c2, 0, sizeof(l_c2));
    memset(l_c3, 0, sizeof(l_c3));
    memset(l_c4, 0, sizeof(l_c4));
    memset(r_c1, 0, sizeof(r_c1));
    memset(r_c2, 0, sizeof(r_c2));
    memset(r_c3, 0, sizeof(r_c3));
    memset(r_c4, 0, sizeof(r_c4));
    memset(l_a1, 0, sizeof(l_a1));
    memset(l_a2, 0, sizeof(l_a2));
    memset(r_a1, 0, sizeof(r_a1));
    memset(r_a2, 0, sizeof(r_a2));

    /* Comb feedback ~ RT60 control */
    float comb_fb = 0.80f;
    float ap_fb = 0.7f;

    /* Left */
    rev->left.combs[0] = (sr_delay_t) {comb_fb, l_c1, comb_base[0][0], 0};
    rev->left.combs[1] = (sr_delay_t) {comb_fb, l_c2, comb_base[0][1], 0};
    rev->left.combs[2] = (sr_delay_t) {comb_fb, l_c3, comb_base[0][2], 0};
    rev->left.combs[3] = (sr_delay_t) {comb_fb, l_c4, comb_base[0][3], 0};

    rev->left.allpasses[0] = (sr_delay_t) {ap_fb, l_a1, allpass_base[0][0], 0};
    rev->left.allpasses[1] = (sr_delay_t) {ap_fb, l_a2, allpass_base[0][1], 0};

    /* Right */
    rev->right.combs[0] = (sr_delay_t) {comb_fb, r_c1, comb_base[1][0], 0};
    rev->right.combs[1] = (sr_delay_t) {comb_fb, r_c2, comb_base[1][1], 0};
    rev->right.combs[2] = (sr_delay_t) {comb_fb, r_c3, comb_base[1][2], 0};
    rev->right.combs[3] = (sr_delay_t) {comb_fb, r_c4, comb_base[1][3], 0};

    rev->right.allpasses[0] = (sr_delay_t) {ap_fb, r_a1, allpass_base[1][0], 0};
    rev->right.allpasses[1] = (sr_delay_t) {ap_fb, r_a2, allpass_base[1][1], 0};

    rev->wet = 0.3f;
    rev->dry = 0.7f;

    // arm_biquad_cascade_df2T_init_f32(&rev->left.iir_lp_instance, lp_fc2k_but_NUM_STAGES, lp_fc2k_but_coeffs, rev->left.iir_lp_state);
    // arm_biquad_cascade_df2T_init_f32(&rev->right.iir_lp_instance, lp_fc2k_but_NUM_STAGES, lp_fc2k_but_coeffs, rev->right.iir_lp_state);
}

/** @brief Run one sample through the parallel combs then series allpasses. */
static float process_channel(sr_channel_t* ch, float in) {
    float sum = 0.0f;

    for (int i = 0; i < SR_COMBS; i++) {
        sum += comb_process(&ch->combs[i], in);
    }

    sum *= 0.25f;

    float y = sum;
    for (int i = 0; i < SR_ALLPASSES; i++)
        y = allpass_process(&ch->allpasses[i], y);

    return y;
}

void schroeder_rev_process(schroeder_stereo_t* rev, float in_l, float in_r, float* out_l, float* out_r) {
    float wet_l = process_channel(&rev->left, in_l);
    float wet_r = process_channel(&rev->right, in_r);

    *out_l = rev->dry * in_l + rev->wet * wet_l;
    *out_r = rev->dry * in_r + rev->wet * wet_r;
}

/* ----- PUBLIC API ----- */

void schroeder_rev_set_feedback(schroeder_stereo_t* rev, float feedback) {
    if (feedback < 0.f)
        feedback = 0.f;
    if (feedback > 0.999f)
        feedback = 0.999f;

    // float comb_fb = feedback * COMB_FEEDBACK(rev->size);
    // float ap_fb = feedback * ALLPASS_FEEDBACK(rev->size);
    float comb_fb = feedback * COMB_FEEDBACK(rev->size);
    float ap_fb = feedback * ALLPASS_FEEDBACK(rev->size);

    for (int i = 0; i < 4; i++) {
        rev->left.combs[i].feedback = comb_fb;
        rev->right.combs[i].feedback = comb_fb;
    }

    for (int i = 0; i < 2; i++) {
        rev->left.allpasses[i].feedback = ap_fb;
        rev->right.allpasses[i].feedback = ap_fb;
    }
}

void schroeder_rev_set_wet(schroeder_stereo_t* rev, float wet) {
    if (wet < 0.f)
        wet = 0.f;
    if (wet > 1.f)
        wet = 1.f;

    rev->wet = wet;
    rev->dry = 1.f - wet;
}

// size = 1.0 -> maximum RT60 / room size based on base delay lengths
// size < 1.0 -> shorter RT60 / smaller room
void schroeder_rev_set_size(schroeder_stereo_t* rev, float size) {
    if (size < MIN_ROOM_SIZE)
        size = MIN_ROOM_SIZE;
    if (size > 1.0f)
        size = 1.0f;

    rev->size = size; // Store the clamped size

    // Use size directly as the scaling factor
    for (int i = 0; i < 4; i++) {
        rev->left.combs[i].length = (uint16_t) (comb_base[0][i] * size);
        rev->right.combs[i].length = (uint16_t) (comb_base[1][i] * size);
    }

    for (int i = 0; i < 2; i++) {
        rev->left.allpasses[i].length = (uint16_t) (allpass_base[0][i] * size);
        rev->right.allpasses[i].length = (uint16_t) (allpass_base[1][i] * size);
    }
}

void schroeder_rev_set_lp_alpha(schroeder_stereo_t* rev, float alpha) {
    if (alpha < 0.f)
        alpha = 0.f;
    if (alpha > 1.f)
        alpha = 1.f;

    for (int i = 0; i < SR_COMBS; i++) {
        rev->left.combs[i].lp_alpha = alpha;
        rev->right.combs[i].lp_alpha = alpha;
    }
}