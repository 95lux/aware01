#include "dsp/schroeder_reverb.h"
#include "arm_math.h"
#include <string.h>
/* ---- Prime delays (~48kHz) ---- */
// Base comb & allpass lengths (primes for stereo)
static const uint16_t comb_base[2][4] = {
    {1553, 1613, 1499, 1427}, // left
    {1567, 1601, 1487, 1433}  // right
};

static const uint16_t allpass_base[2][2] = {
    {223, 557}, // left
    {229, 563}  // right
};

/* ---- Static buffers ---- */

#define MAX_COMB_LEN 1600 // max delay for combs
#define MAX_AP_LEN 600    // max delay for allpasses

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

static float comb_process(sr_delay_t* d, float in) {
    float y = d->buf[d->idx];
    d->buf[d->idx] = in + y * d->feedback;

    d->idx++;
    if (d->idx >= d->size)
        d->idx = 0;

    return y;
}

static float allpass_process(sr_delay_t* d, float in) {
    float buf = d->buf[d->idx];
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
}

static float process_channel(sr_channel_t* ch, float in) {
    float sum = 0.0f;

    for (int i = 0; i < SR_COMBS; i++)
        sum += comb_process(&ch->combs[i], in);

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

void schroeder_rev_set_feedback(schroeder_stereo_t* rev, float feedback) {
    if (feedback < 0.f)
        feedback = 0.f;
    if (feedback > 0.999f)
        feedback = 0.999f;

    for (int i = 0; i < SR_COMBS; i++) {
        rev->left.combs[i].feedback = feedback;
        rev->right.combs[i].feedback = feedback;
    }

    // ensure allpass feedback stability
    float ALLPASS_MAX_FEEDBACK = 0.7f;

    for (int i = 0; i < SR_ALLPASSES; i++) {
        rev->left.allpasses[i].feedback = fminf(feedback, ALLPASS_MAX_FEEDBACK);
        rev->right.allpasses[i].feedback = fminf(feedback, ALLPASS_MAX_FEEDBACK);
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

// scale = 1.0 -> original length
// scale < 1.0 -> shorter RT60 / smaller room
// scale > 1.0 -> longer RT60 / larger room
void schroeder_rev_set_scale(schroeder_stereo_t* rev, float scale) {
    if (scale < 0.001f)
        scale = 0.001f;
    if (scale > 1.0f)
        scale = 1.0f;

    for (int i = 0; i < 4; i++) {
        rev->left.combs[i].length = (uint16_t) (comb_base[0][i] * scale);
        rev->right.combs[i].length = (uint16_t) (comb_base[1][i] * scale);
    }

    for (int i = 0; i < 2; i++) {
        rev->left.allpasses[i].length = (uint16_t) (allpass_base[0][i] * scale);
        rev->right.allpasses[i].length = (uint16_t) (allpass_base[1][i] * scale);
    }
}