#include "tape_player.h"

#include "audioengine.h"
#include "envelope.h"
#include "project_config.h"
#include "string.h"
#include <arm_math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/swo_log.h"
#include "param_cache.h"
#include "ressources.h"
#include "util.h"

// #define DECIMATION_FIXED 16 // fixed decimation factor for testing, will be set from params in record state machine once working.
#define Q32_UNITY (4294967296.0f)
#define Q16_UNITY (65536.0f)

static tape_buffer_t playback_buffer_struct;
static tape_buffer_t record_buffer_struct;

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

// +3 for hermite interpolation safety
// TODO: make sure this is long enough once configurable!
int16_t xfade_retrig_temp_buf_l[FADE_RETRIG_XFADE_LEN + 3];
int16_t xfade_retrig_temp_buf_r[FADE_RETRIG_XFADE_LEN + 3]; // +3 for hermite interpolation safety

int16_t xfade_cyclic_temp_buf_l[FADE_RETRIG_XFADE_LEN + 3];
int16_t xfade_cyclic_temp_buf_r[FADE_RETRIG_XFADE_LEN + 3]; // +3 for hermite interpolation safety

static struct tape_player* active_tape_player;

int init_tape_player(struct tape_player* tape_player, size_t dma_buf_size, QueueHandle_t cmd_queue) {
    if (tape_player == NULL || dma_buf_size <= 0)
        return -1;

    // clear tape buffers on init
    memset(tape_play_buf_l, 0, sizeof(tape_play_buf_l));
    memset(tape_play_buf_r, 0, sizeof(tape_play_buf_r));

    // debug addresses for reading out tape buffer via SWD
    volatile uintptr_t tape_l_addr_dbg = (uintptr_t) tape_play_buf_l;
    volatile uintptr_t tape_r_addr_dbg = (uintptr_t) tape_play_buf_r;

    // buffer assignments
    tape_player->playback_buf = &playback_buffer_struct;
    tape_player->record_buf = &record_buffer_struct;
    tape_player->dma_buf_size = dma_buf_size;
    tape_player->playback_buf->ch[0] = tape_play_buf_l;
    tape_player->playback_buf->ch[1] = tape_play_buf_r;
    tape_player->playback_buf->size = TAPE_SIZE_CHANNEL;
    tape_player->playback_buf->valid_samples = 0;
    tape_player->record_buf->ch[0] = tape_rec_buf_l;
    tape_player->record_buf->ch[1] = tape_rec_buf_r;
    tape_player->record_buf->size = TAPE_SIZE_CHANNEL;
    tape_player->record_buf->valid_samples = 0;

    // playhead and reacordhead init
    tape_player->pos_q48_16 = 1 << 16; // start at sample 1 for interpolation

    tape_player->xfade_retrig.buf_b_ptr_l = xfade_retrig_temp_buf_l;
    tape_player->xfade_retrig.buf_b_ptr_r = xfade_retrig_temp_buf_r;
    tape_player->xfade_retrig.len = FADE_RETRIG_XFADE_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->xfade_retrig.active = false;
    tape_player->xfade_retrig.temp_buf_valid_samples = 0;
    tape_player->xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation

    tape_player->xfade_cyclic.buf_b_ptr_l = xfade_cyclic_temp_buf_l;
    tape_player->xfade_cyclic.buf_b_ptr_r = xfade_cyclic_temp_buf_r;
    tape_player->xfade_cyclic.len = FADE_RETRIG_XFADE_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->xfade_cyclic.active = false;
    tape_player->xfade_cyclic.temp_buf_valid_samples = 0;
    tape_player->xfade_cyclic.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation

    tape_player->fade_in.buf_b_ptr_l = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player->fade_in.buf_b_ptr_r = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player->fade_in.len = FADE_IN_OUT_LEN; // fade length in samples TODO: make configurable via MACRO
    tape_player->fade_in.pos_q48_16 = 1 << 16;  // start at sample 1 for interpolation

    tape_player->fade_out.buf_b_ptr_l = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player->fade_out.buf_b_ptr_r = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player->fade_out.len = FADE_IN_OUT_LEN; // fade length in samples
    tape_player->fade_out.pos_q48_16 = 1 << 16;  // start at sample 1 for interpolation

    tape_player->tape_recordhead = 0;

    // state logic
    tape_player->play_state = PLAY_STOPPED;
    tape_player->rec_state = REC_IDLE;

    // envelope init
    // TODO: maybe init in own function
    tape_player->env.state = ENV_IDLE;
    tape_player->env.value = 0.0f;
    tape_player->env.attack_inc = 1 / (0.001f * AUDIO_SAMPLE_RATE);
    tape_player->env.decay_inc = 1 / (0.5f * AUDIO_SAMPLE_RATE);
    tape_player->env.sustain = 0.0f;

    // parameters
    // TODO: switch to state events
    tape_player->cyclic_mode = false; // default to oneshot mode

    tape_player->switch_bufs_pending = false;
    tape_player->params.pitch_factor = 1.0f;
    tape_player->params.env_attack = 0.0f; // normalized env values
    tape_player->params.env_decay = 0.2f;  // normalized env values

    tape_player->params.reverse = false;    // default to forward playback
    tape_player->params.cyclic_mode = true; // default to oneshot mode

    // init cmd
    tape_player->tape_cmd_q = cmd_queue;

    active_tape_player = tape_player;

    return 0;
}

// from https://www.musicdsp.org/en/latest/Other/93-hermite-interpollation.html
// explained here https://ldesoras.fr/doc/articles/resampler-en.pdf
// uses phase accumulator
// phase: integer index + 16-bit fractional part
static inline float hermite_interpolate(uint64_t pos, int16_t* buffer) {
    uint32_t idx = (uint32_t) (pos >> 16);
    uint32_t frac = (uint32_t) (pos & 0xFFFF);

    // 2. Fractional part as Q32 -> normalized t in [0,1)
    float t = frac * (1.0f / Q16_UNITY);
    int n = (int) idx - 1;

    // xm1 and x2 are only used for derivative estimation
    // we interpolate between x0 and x1
    float xm1 = buffer[n];
    float x0 = buffer[n + 1];
    float x1 = buffer[n + 2];
    float x2 = buffer[n + 3];

    // estimate derivatives by finite differences
    // Catmull-Rom splines with Tension = 0
    float m0 = 0.5f * (x1 - xm1); // derivative at x0
    float m1 = 0.5f * (x2 - x0);  // derivative at x1

    // calculate coefficients
    // Cubic Hermite interpolation (Catmull–Rom spline)
    float c = m0;
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + m1;
    float b = w + a;
    float d = x0;

    // cubic polynomial in horner form.
    // actually a*t^3 + b*t^2 + c*t + d
    return (((a * t - b) * t + c) * t + d);
}

static inline void
tape_fetch_sample(uint64_t pos_q48_16, int16_t* buf_l, int16_t* buf_r, struct tape_player* tape, int16_t* out_l, int16_t* out_r) {
    uint32_t idx = (uint32_t) (pos_q48_16 >> 16);

    // --- Hold ---
    float hold_l = buf_l[idx];
    float hold_r = buf_r[idx];

#ifdef CONFIG_TAPE_PLAYER_ENABLE_HERMITE

    // --- Hermite ---
    float herm_l = hermite_interpolate(pos_q48_16, buf_l);
    float herm_r = hermite_interpolate(pos_q48_16, buf_r);

    float grit = tape_player_get_grit(tape);

    grit = grit * MAX_GRIT_ON_MAX_DECIMATION;

    // --- Blend (difference method) ---
    float out_l_f = herm_l + grit * (hold_l - herm_l);
    float out_r_f = herm_r + grit * (hold_r - herm_r);

#else
    float out_l_f = hold_l;
    float out_r_f = hold_r;
#endif

    *out_l = __SSAT((int32_t) out_l_f, 16);
    *out_r = __SSAT((int32_t) out_r_f, 16);
}

// advance Q48.16 playhead with bounds checking and wrap/clamp logic for different modes (reverse, cyclic)
// use q16 increment for phase inc, since it is more than enough resolution for pitch control, and much faster to do the addition and bounds checking with 64-bit integers, instead of 128-bit which would be needed for Q48.16 increments.
static inline void advance_playhead_q48(uint64_t* pos_q48, uint32_t phase_inc_q16, bool reverse, bool cyclic) {
    uint32_t valid_samples = active_tape_player->playback_buf->valid_samples;
    uint64_t wrap_point = (uint64_t) valid_samples << 16;

    if (reverse) {
        if (*pos_q48 < phase_inc_q16) {
            *pos_q48 = cyclic ? (wrap_point + *pos_q48 - phase_inc_q16) : 0;
            if (!cyclic)
                tape_player_stop_play();
        } else {
            *pos_q48 -= phase_inc_q16;
        }
    } else {
        *pos_q48 += phase_inc_q16;
        if (*pos_q48 >= wrap_point) {
            if (cyclic) {
                // 64bit division might be slow on M7, this is why whe use a subtraction loop for wrapping instead of mod.
                // *pos_q48 %= wrap_point;
                while (*pos_q48 >= wrap_point)
                    *pos_q48 -= wrap_point;
            } else {
                *pos_q48 = (uint64_t) (valid_samples - 4) << 16;
                tape_player_stop_play();
            }
        }
    }
}

// Advance Q16.16 fade accumulator with simple bounds check
// TODO: hopefully no cyclic, retrig, stop handling is needed.
static inline void advance_fade_acc_q48(uint64_t* pos_q48, uint32_t step_q16, uint16_t lut_len) {
    // simple advance stepq16 is already at Q48.16 scale
    *pos_q48 += (uint64_t) step_q16;

    // clamp to last LUT index
    uint64_t max_pos = ((uint64_t) (lut_len - 1)) << 16;
    if (*pos_q48 > max_pos) {
        *pos_q48 = max_pos;
    }
}

static inline bool playhead_near_end(uint64_t pos_q48_16, uint32_t fade_len_samples, uint32_t active_phase_inc_q16) {
    uint32_t buf_size = active_tape_player->playback_buf->valid_samples;

    // Safety margin for Hermite (n+3)
    if (buf_size < 4)
        return false;
    uint32_t limit = buf_size - 4;

    uint32_t idx = (uint32_t) (pos_q48_16 >> 16);
    // How many steps (at current pitch) are left until we hit the limit?
    uint32_t samples_left = (idx < limit) ? (limit - idx) : 0;

    // Convert fade_len_samples to the same "pitch scale" as the playhead.
    // If active_phase_inc is large (pitch up), we need to trigger much earlier
    // in terms of buffer index to ensure the fade has enough real-time cycles to finish.

    // But multiplication is safer/faster:
    uint32_t distance_left_q8 = samples_left << 8;
    uint32_t fade_reach_q8 = (fade_len_samples * (active_phase_inc_q16 >> 8));

    return distance_left_q8 <= fade_reach_q8;
}

// handle fade when starting playback. requires fade_in_active to be set when sample playback is started.
// currently uses Q16 fixed point for LUT indexing.
// TODO: 128 steps in LUT might be very low, maybe increase resolution of LUT
static inline void tape_handle_fade_in(struct tape_player* tape, int16_t* out_l, int16_t* out_r) {
    // --- Q16 FIXED POINT FADE IN ---
    if (!tape->fade_in.active && !tape->xfade_retrig.active)
        return;

    uint32_t lut_idx = tape->fade_in.pos_q48_16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        tape->fade_in.active = false;
    } else {
        int16_t f = fade_in_lut[lut_idx];
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // fixed step, not pitch-aware, since we want a consistent fade in time regardless of pitch. If we wanted a pitch-aware fade in, we would need to calculate the step based on the current phase increment, similar to the fade out below.
        tape->fade_in.pos_q48_16 += (uint64_t) FADE_IN_OUT_STEP_Q16;
    }
}

// this might not be needed, if we clamp the envelope release to the buffer length, when not in cyclic mode.
// TODO: clamp envelope release to buffer length in non-cyclic mode, to prevent clicks. This also means that fade out is not really needed in non-cyclic mode, since we will just do a hard stop at the end of the buffer, which should be fine since there is no looping happening. In cyclic mode however, we do need the fade out to prevent clicks when crossing the loop point.
// if in cyclic not needed aswell, since crossfading between cycles happens indefinitely.
static inline void tape_handle_fade_out(struct tape_player* tape, uint32_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    if (!tape->fade_out.active || tape->params.cyclic_mode)
        return;

    // --- Calculate pitch-aware Q32.32 step ---
    // FADE_LUT_LEN in samples, FADE_IN_OUT_LEN in tape samples
    // Scale step to Q32.32 directly
    uint64_t step_q32 = ((uint64_t) FADE_LUT_LEN << 16) / FADE_IN_OUT_LEN;
    // multiply by playhead increment to make it pitch-aware
    uint64_t pitch_aware_step = (step_q32 * (uint64_t) active_phase_inc) >> 16;

    // LUT index = top 32 bits
    uint32_t lut_idx = tape->fade_out.pos_q48_16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        *out_l = 0;
        *out_r = 0;
        tape->fade_out.active = false;
        tape_player_stop_play();
    } else {
        int16_t f = fade_in_lut[FADE_LUT_LEN - 1 - lut_idx];
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // advance Q48.16 position
        tape->fade_out.pos_q48_16 += pitch_aware_step;
    }
}

static inline void tape_handle_cyclic_crossfade(struct tape_player* tape, uint64_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    if (!tape->fade_out.active || !tape->xfade_cyclic.active)
        return;

    int16_t b_l, b_r;
    tape_fetch_sample(tape->xfade_cyclic.pos_q48_16, tape->xfade_cyclic.buf_b_ptr_l, tape->xfade_cyclic.buf_b_ptr_r, tape, &b_l, &b_r);

    uint32_t lut_idx = tape->xfade_cyclic.pos_q48_16 >> 16;
    int16_t fb = fade_in_lut[lut_idx];
    int16_t fa = fade_in_lut[FADE_LUT_LEN - 1 - lut_idx];

    *out_l = __SSAT(((int32_t) (*out_l) * fa + (int32_t) b_l * fb) >> 15, 16);
    *out_r = __SSAT(((int32_t) (*out_r) * fa + (int32_t) b_r * fb) >> 15, 16);

    advance_playhead_q48(&tape->xfade_cyclic.pos_q48_16, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);

    if (lut_idx >= FADE_LUT_LEN - 1) {
        // crossfade finished, switch playhead to the new position
        tape->pos_q48_16 = tape->xfade_cyclic.pos_q48_16;
        tape->xfade_retrig.active = false;
    }
}

static inline void tape_handle_retrigger_crossfade(struct tape_player* tape, uint64_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    if (!tape->xfade_retrig.active)
        return;

    int16_t a_l = *out_l;
    int16_t a_r = *out_r;
    int16_t b_l, b_r;

    tape_fetch_sample(tape->xfade_retrig.pos_q48_16, tape->xfade_retrig.buf_b_ptr_l, tape->xfade_retrig.buf_b_ptr_r, tape, &b_l, &b_r);

    int16_t fa, fb;

    if (tape->xfade_retrig.temp_buf_valid_samples < 2) {
        fa = 32767;
        fb = 0;
    } else {
        uint32_t lut_i = (tape->xfade_retrig.pos_q48_16 >> 16) * (FADE_LUT_LEN - 1) / (tape->xfade_retrig.temp_buf_valid_samples - 1);
        fa = fade_in_lut[FADE_LUT_LEN - 1 - lut_i];
        fb = fade_in_lut[lut_i];
    }

    int32_t acc_l = (int32_t) fa * a_l + (int32_t) fb * b_l;
    int32_t acc_r = (int32_t) fa * a_r + (int32_t) fb * b_r;

    *out_l = __SSAT(acc_l >> 15, 16);
    *out_r = __SSAT(acc_r >> 15, 16);

    if ((tape->xfade_retrig.pos_q48_16 >> 16) >= tape->xfade_retrig.temp_buf_valid_samples) {
        tape->xfade_retrig.active = false;
    }

    advance_playhead_q48(&tape->xfade_retrig.pos_q48_16, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);
}

static inline uint32_t tape_compute_phase_increment(struct tape_player* tape) {
    float target_inc = tape->params.pitch_factor * 65536.0f;

    //TODO: is filtering pitch per block necessary? Evaluate if zipper noise is occuring without it. Was heavier before, seems gone now.
    // // 0.01f = 1% move per sample.
    // // Increase to 0.005f for smoother/slower, 0.05f for snappier.
    // const float alpha = 0.01f;
    // //--- The "Default" Stable IIR ---
    // // current = current + alpha * (target - current)
    // // This form is much more stable than the (target * coeff) version
    // tape->current_phase_inc += alpha * (target_inc - tape->current_phase_inc);

    tape->curr_phase_inc_q16_16 = target_inc;

    uint32_t dec = tape->playback_buf->decimation > 0 ? tape->playback_buf->decimation : 1;
    // This result is now a Q16.16 increment
    return (tape->curr_phase_inc_q16_16 / dec);
}

static inline void tape_process_playback_frame(struct tape_player* tape, uint32_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    switch (tape->play_state) {
    case PLAY_STOPPED:
        // output silence
        break;
    case PLAY_PLAYING: {
        tape_fetch_sample(tape->pos_q48_16, tape->playback_buf->ch[0], tape->playback_buf->ch[1], tape, out_l, out_r);

        tape_handle_fade_in(tape, out_l, out_r);

        // --- Q16 FIXED POINT FADE OUT TRIGGER ---
        if (!tape->fade_out.active && playhead_near_end(tape->pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc) && !tape->params.cyclic_mode) {
            tape->fade_out.active = true;
            tape->fade_out.pos_q48_16 = 0; // Reset accumulator
        }

        // --- Q16 FIXED POINT FADE OUT PROCESS ---
        tape_handle_fade_out(tape, active_phase_inc, out_l, out_r);

        // --- Cyclic Loop Trigger Logic ---
        if (playhead_near_end(tape->pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc) && tape->params.cyclic_mode) {
            tape->xfade_cyclic.active = true;
            tape->xfade_cyclic.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
            // set buffer pointers for cyclic crossfade to start of playback buffer.
            tape->xfade_cyclic.buf_b_ptr_l = tape->playback_buf->ch[0];
            tape->xfade_cyclic.buf_b_ptr_r = tape->playback_buf->ch[1];

            // TODO: this is not needed anymore. we track fade progress and active state in xfade struct
            tape->xfade_cyclic.active = true;
        }

        tape_handle_cyclic_crossfade(tape, active_phase_inc, out_l, out_r);

        tape_handle_retrigger_crossfade(tape, active_phase_inc, out_l, out_r);

        // advance main playhead
        advance_playhead_q48(&tape->pos_q48_16, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);

        // stop only when reaching the end of the buffer in oneshot mode. In cyclic mode, we will just wrap around and never stop.
        // if (tape->ph_a.idx < tape->playback_buf->valid_samples - 3) {
        // } else {
        // tape_player_stop_play();
        // }
    }
    }
}

static inline void tape_process_recording_frame(struct tape_player* tape, int16_t* in_buf, uint32_t n) {
    if (tape->rec_state != REC_RECORDING)
        return;

    uint8_t rec_decimation = tape->record_buf->decimation > 0 ? tape->record_buf->decimation : 1;
    uint32_t frame_idx = n / 2;            // index of the current stereo frame (0, 1, 2, ...)
    if ((frame_idx % rec_decimation) != 0) // record only each Nth frame, where N = decimation factor
        return;

    // deinterleaves input buffer into tape buffer
    tape->record_buf->ch[0][tape->tape_recordhead] = in_buf[n];
    tape->record_buf->ch[1][tape->tape_recordhead] = in_buf[n + 1];
    tape->tape_recordhead++;

    // if tape has recorded all the way, stop recording for now.
    if (tape->tape_recordhead >= tape->record_buf->size) {
        tape_player_stop_record();
    }
}

// worker function to process tape player state
void tape_player_process(struct tape_player* tape, int16_t* in_buf, int16_t* out_buf) {
    // TODO: implement circular tape buffer (?) - for now, just stop at the end of the buffer
    if (!tape || !tape->playback_buf->ch[0] || !tape->playback_buf->ch[1])
        return;

    // TODO: BIG TODO!!! OPTIMIZE FADE ALGS. currently with 128 sample buffer, the cpu is not fast enough. Maybe switch to q16.16 altogeher
    // n represents the sample index within the current DMA buffer (interleaved stereo, so step by 2)
    for (uint32_t n = 0; n < (AUDIO_BLOCK_SIZE / 2); n += 2) {
        uint64_t active_phase_inc = tape_compute_phase_increment(tape);

        int16_t out_l = 0;
        int16_t out_r = 0;

        switch (tape->play_state) {
        case PLAY_STOPPED:
            // output silence, but still process recording if active
            break;
        case PLAY_PLAYING:
            tape_process_playback_frame(tape, active_phase_inc, &out_l, &out_r);
            break;
        }

        float env_val = envelope_process(&tape->env);
        out_buf[n] = (int16_t) (out_l * env_val);
        out_buf[n + 1] = (int16_t) (out_r * env_val);

        switch (tape->rec_state) {
        case REC_IDLE:
            // do nothing
            break;
        case REC_RECORDING: {
            // record tape at current recordhead position
            // only record every second sample. Simple decimation.
            tape_process_recording_frame(tape, in_buf, n);
            break;
        }
        case REC_SWAP_PENDING:
            break;
        case REC_DONE:
            break;
        }
    }
}

static void compute_grit(struct tape_player* t) {
    uint8_t dec = t->playback_buf->decimation;
    if (dec < 1)
        dec = 1;

#define MAX_DECIMATION 16

    float g = (float) (dec - 1) / (float) (MAX_DECIMATION - 1); // 0..1

    // power curve - mixes linear and quardratic
    // t->params.grit = g * 0.6f + g * g * 0.4f;
    // logarithmic curve: fast start, slow rise at the end
    t->params.grit = logf(1.0f + 9.0f * g) / logf(10.0f); // maps 0..1 -> 0..1
}

// swap record and playback buffers before playing newly recorded buffer.
static void swap_tape_buffers(struct tape_player* t) {
    // switch recorded buffer to playback buffer
    tape_buffer_t* temp = t->playback_buf;
    t->playback_buf = t->record_buf;
    t->record_buf = temp;

    t->tape_recordhead = 0;
    t->switch_bufs_pending = false;

    // now that the new playback buffer holds the recorded audio at the respective decimation factor,
    // compute grit factor from this and save as parameter to be used in other dsp related functions.
    compute_grit(t);
}

// prototypes for FSM event handling
static void play_fsm_event(struct tape_player* t, tape_event_t evt);
static void rec_fsm_event(struct tape_player* t, tape_event_t evt);

static void play_fsm_event(struct tape_player* t, tape_event_t evt) {
    switch (t->play_state) {
    case PLAY_STOPPED:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- PLAY FROM IDLE ----- */

            // each time a play event is triggered, switch buffers if pending.
            if (t->switch_bufs_pending) {
                swap_tape_buffers(t);
                rec_fsm_event(t, TAPE_EVT_SWAP_DONE);
            }

            // TODO: evaluate minimum number of samples
            // 4 samples hermite, maybe fadein + fadeout?
            if (t->playback_buf->valid_samples < 4) {
                // not enough samples to play
                return;
            }

            envelope_set_attack_norm(&t->env, t->params.env_attack);
            envelope_set_decay_norm(&t->env, t->params.env_decay);
            envelope_note_on(&t->env);

            t->pos_q48_16 = 1 << 16; // start at sample 1 for interpolation

            // Init Fade In
            t->fade_in.pos_q48_16 = 0;
            t->fade_in.active = true;

            // Ensure Fade Out is clean
            t->fade_out.active = false;
            t->fade_out.pos_q48_16 = 0;

            t->play_state = PLAY_PLAYING;
        }
        break;

    case PLAY_PLAYING:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- RETRIGGER PLAY ----- */

            // copy upcoming xfade.len samples starting at current phase
            // we need to start from one sample before the current phase for hermite interpolation
            uint32_t start_idx = (uint32_t) (t->pos_q48_16 >> 16) - 1;
            uint32_t max_needed = t->xfade_retrig.len + 3; // +3 for hermite interpolation safety (idx-1..idx+2)

            // number of samples available for crossfade starting from start_idx
            uint32_t available = t->playback_buf->valid_samples - start_idx;

            // if available samples are less than max_needed, we have to adjust crossfade length and fade LUT access accordingly, otherwise we might read out of bounds of the playback buffer (if start_idx + max_needed exceeds valid_samples), or the fade LUT (if we use max_needed as xfade.len for LUT access, but there are not enough valid samples in temp buffer).
            t->xfade_retrig.temp_buf_valid_samples = min_u32(available, max_needed);

            // switch buffers if pending. This has to happen after after copying the xfade buffer and calculating the available samples for xfade
            if (t->switch_bufs_pending) {
                // use static temp buffer for fade, because playback buffer has now switched
                // fill up only until temp_buf_valid, but ramp down, so on i = temp_buf_valid, the sample reaches 0
                t->xfade_retrig.buf_b_ptr_l = xfade_retrig_temp_buf_l;
                t->xfade_retrig.buf_b_ptr_r = xfade_retrig_temp_buf_r;
                for (uint32_t i = 0; i < t->xfade_retrig.temp_buf_valid_samples; i++) {
                    t->xfade_retrig.buf_b_ptr_l[i] = t->playback_buf->ch[0][start_idx + i];
                    t->xfade_retrig.buf_b_ptr_r[i] = t->playback_buf->ch[1][start_idx + i];
                }
                swap_tape_buffers(t);
                rec_fsm_event(t, TAPE_EVT_SWAP_DONE);
            } else {
                // if no buffer switch pending, we can also just point the xfade buffer to the playback buffer, starting at the current phase. This saves us from copying the xfade buffer every time.
                t->xfade_retrig.buf_b_ptr_l = &t->playback_buf->ch[0][start_idx];
                t->xfade_retrig.buf_b_ptr_r = &t->playback_buf->ch[1][start_idx];
            }

            t->pos_q48_16 = 1 << 16; // reset main play phase phase

            // ph_b holdes the phase for crossfade buffer
            t->xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1
            t->xfade_retrig.active = true;
            t->xfade_retrig.len = FADE_RETRIG_XFADE_LEN;

            envelope_set_attack_norm(&t->env, t->params.env_attack);
            envelope_set_decay_norm(&t->env, t->params.env_decay);
            envelope_note_on(&t->env);

        } else if (evt == TAPE_EVT_STOP) {
            // envelope_note_off(&t->env);
            t->play_state = PLAY_STOPPED;
        }
        break;
    }
}

static void rec_fsm_event(struct tape_player* t, tape_event_t evt) {
    switch (t->rec_state) {
    case REC_IDLE:
        if (evt == TAPE_EVT_RECORD) {
            t->tape_recordhead = 0;
            // apply decimation to recording buffer. This will be reach over to playback buffer by buffer swapping
#ifdef DECIMATION_FIXED
            t->record_buf->decimation = DECIMATION_FIXED;
#else
            t->record_buf->decimation = t->params.decimation;
#endif
            t->rec_state = REC_RECORDING;
        }
        break;

    case REC_RECORDING:
        if (evt == TAPE_EVT_RECORD) {
            // new recording while already recording -> just switch buffers and start recording on the other one
            t->record_buf->valid_samples = t->tape_recordhead;
            t->tape_recordhead = 0;
            t->switch_bufs_pending = true;

            t->rec_state = REC_DONE;

            // stay in recording state
        } else if (evt == TAPE_EVT_RECORD_DONE || evt == TAPE_EVT_STOP) {
            t->record_buf->valid_samples = t->tape_recordhead;
            t->switch_bufs_pending = true;

            t->rec_state = REC_DONE;
        }
        break;
    case REC_DONE:
        //swap is done, jump to.
        if (evt == TAPE_EVT_SWAP_DONE) {
#ifdef DECIMATION_FIXED
            t->record_buf->decimation = DECIMATION_FIXED;
#else
            t->record_buf->decimation = t->params.decimation;
            t->switch_bufs_pending = false;
#endif

            t->rec_state = REC_IDLE;
        }
        break;
    case REC_SWAP_PENDING:
        //swap is done, prepare for next record.
        if (evt == TAPE_EVT_SWAP_DONE) {
#ifdef DECIMATION_FIXED
            t->record_buf->decimation = DECIMATION_FIXED;
#else
            t->record_buf->decimation = t->params.decimation;
#endif

            t->rec_state = REC_RECORDING;
        }
        break;
    }
}

/* ----- PUBLIC API ----- */

void tape_player_play(void) {
    if (!active_tape_player)
        return;
    play_fsm_event(active_tape_player, TAPE_EVT_PLAY);
}

void tape_player_stop_play(void) {
    if (!active_tape_player)
        return;
    play_fsm_event(active_tape_player, TAPE_EVT_STOP);
}

void tape_player_record(void) {
    if (!active_tape_player)
        return;
    rec_fsm_event(active_tape_player, TAPE_EVT_RECORD);
}

void tape_player_stop_record(void) {
    if (!active_tape_player)
        return;
    rec_fsm_event(active_tape_player, TAPE_EVT_RECORD_DONE);
}

// change pitch factor (playback speed)
// TODO: evaluate where musical pitch is calculated. Probably in control interface task, and converted to simple pitch factor here.
void tape_player_set_pitch(float pitch_factor) {
    if (active_tape_player) {
        active_tape_player->params.pitch_factor = pitch_factor;
    }
}

float tape_player_get_pitch() {
    if (active_tape_player) {
        return active_tape_player->params.pitch_factor;
    }
    return 0;
}

void tape_player_set_params(struct param_cache param_cache) {
    if (active_tape_player) {
        active_tape_player->params.pitch_factor = param_cache.pitch_ui * param_cache.pitch_cv;
        active_tape_player->params.env_attack = param_cache.env_attack;
        active_tape_player->params.env_decay = param_cache.env_decay;
        active_tape_player->params.reverse = param_cache.reverse_mode;
        active_tape_player->params.cyclic_mode = param_cache.cyclic_mode;
        active_tape_player->params.decimation = param_cache.decimation;
    }
}

// returns value from 0..1 depending on the decimation factor
// this is used to blend in the hold sample for a grittier sound when decimation is high. At low decimation, we rely more on the hermite interpolation, which gives a smoother sound. At high decimation, the hermite interpolation can become less accurate and introduce more artifacts, so we blend in more of the hold sample to mask these artifacts and create a more "lo-fi" sound.
// possibility to use a power curve to have a stronger effect at higher decimation factors
float tape_player_get_grit(struct tape_player* tape) {
    return tape->params.grit;
}