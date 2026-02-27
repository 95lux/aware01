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

static tape_buffer_t tape_buf_a;
static tape_buffer_t tape_buf_b;

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

// +3 for hermite interpolation safety
// TODO: make sure this is long enough once configurable!
int16_t xfade_retrig_temp_buf_l[FADE_XFADE_RETRIG_LEN + 3];
int16_t xfade_retrig_temp_buf_r[FADE_XFADE_RETRIG_LEN + 3]; // +3 for hermite interpolation safety

int16_t xfade_cyclic_temp_buf_l[FADE_XFADE_CYCLIC_LEN + 3];
int16_t xfade_cyclic_temp_buf_r[FADE_XFADE_CYCLIC_LEN + 3]; // +3 for hermite interpolation safety

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
    tape_player->playback_buf = &tape_buf_a;
    tape_player->record_buf = &tape_buf_b;
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
    tape_player->xfade_retrig.len = FADE_XFADE_RETRIG_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->xfade_retrig.active = false;
    tape_player->xfade_retrig.temp_buf_valid_samples = 0;
    tape_player->xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
    tape_player->xfade_retrig.step_q16 = FADE_XFADE_RETRIG_STEP_Q16;

    tape_player->xfade_cyclic.buf_b_ptr_l = xfade_cyclic_temp_buf_l;
    tape_player->xfade_cyclic.buf_b_ptr_r = xfade_cyclic_temp_buf_r;
    tape_player->xfade_cyclic.len = FADE_XFADE_CYCLIC_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->xfade_cyclic.active = false;
    tape_player->xfade_cyclic.temp_buf_valid_samples = 0;
    tape_player->xfade_cyclic.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
    tape_player->xfade_cyclic.step_q16 = FADE_XFADE_CYCLIC_STEP_Q16;

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

    tape_player->swap_bufs_pending = false;
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

static inline void tape_fetch_sample(uint64_t pos_q48_16, int16_t* buf_l, int16_t* buf_r, int16_t* out_l, int16_t* out_r) {
    uint32_t idx = (uint32_t) (pos_q48_16 >> 16);

    // --- Hold ---
    float hold_l = buf_l[idx];
    float hold_r = buf_r[idx];

#ifdef CONFIG_TAPE_PLAYER_ENABLE_HERMITE

    // --- Hermite ---
    float herm_l = hermite_interpolate(pos_q48_16, buf_l);
    float herm_r = hermite_interpolate(pos_q48_16, buf_r);

    float grit = tape_player_get_grit();

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
    if (idx >= limit)
        return true;

    uint32_t samples_left = limit - idx;

    // We want to know if: samples_left <= (fade_len_samples * (active_phase_inc / 65536))
    // To avoid division, we cross-multiply:
    // (samples_left << 16) <= (fade_len_samples * active_phase_inc)
    uint64_t physical_distance_q16 = (uint64_t) samples_left << 16;
    uint64_t fade_travel_distance_q16 = (uint64_t) fade_len_samples * active_phase_inc_q16;

    return physical_distance_q16 <= fade_travel_distance_q16;
}

// handle fade when starting playback. requires fade_in_active to be set when sample playback is started.
// currently uses Q16 fixed point for LUT indexing.
// TODO: 128 steps in LUT might be very low, maybe increase resolution of LUT
static inline void tape_handle_fade_in(struct tape_player* tape, int16_t* out_l, int16_t* out_r) {
    // --- Q16 FIXED POINT FADE IN ---
    // dont fade when retrigger crossfade is active, since that means we are already fading in.
    if (!tape->fade_in.active || tape->xfade_retrig.active)
        return;

    uint32_t lut_idx = tape->fade_in.fade_acc_q16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        tape->fade_in.active = false;
    } else {
        int16_t f = fade_in_lut[lut_idx];
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // fixed step, not pitch-aware, since we want a consistent fade in time regardless of pitch. If we wanted a pitch-aware fade in, we would need to calculate the step based on the current phase increment, similar to the fade out below.
        tape->fade_in.fade_acc_q16 += FADE_IN_OUT_STEP_Q16;
    }
}

// this might not be needed, if we clamp the envelope release to the buffer length, when not in cyclic mode.
// TODO: clamp envelope release to buffer length in non-cyclic mode, to prevent clicks. This also means that fade out is not really needed in non-cyclic mode, since we will just do a hard stop at the end of the buffer, which should be fine since there is no looping happening. In cyclic mode however, we do need the fade out to prevent clicks when crossing the loop point.
// if in cyclic not needed aswell, since crossfading between cycles happens indefinitely.
static inline void tape_handle_fade_out(struct tape_player* tape, uint32_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    if (!tape->fade_out.active || tape->params.cyclic_mode)
        return;

    // LUT index = top 16 bits
    uint32_t lut_idx = tape->fade_out.fade_acc_q16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        *out_l = 0;
        *out_r = 0;
        tape->fade_out.active = false;
        tape_player_stop_play();
    } else {
        int16_t f = fade_in_lut[FADE_LUT_LEN - 1 - lut_idx];
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // advance Q16.16 position
        tape->fade_out.fade_acc_q16 += FADE_IN_OUT_STEP_Q16;
    }
}

// buf_b = OLD, a = NEW. No promotion needed.
static inline void tape_handle_crossfade_a_is_new(crossfade_t* xfade, uint32_t active_phase_inc, int16_t* new_l, int16_t* new_r) {
    if (!xfade->active)
        return;

    int16_t old_l, old_r;
    tape_fetch_sample(xfade->pos_q48_16, xfade->buf_b_ptr_l, xfade->buf_b_ptr_r, &old_l, &old_r);

    uint32_t lut_i = xfade->fade_acc_q16 >> 16;
    if (lut_i >= FADE_LUT_LEN)
        lut_i = FADE_LUT_LEN - 1;

    int16_t mix_new = fade_in_lut[lut_i];
    int16_t mix_old = 32767 - mix_new;

    *new_l = (int16_t) ((((int32_t) (*new_l) * mix_new) + ((int32_t) old_l * mix_old)) >> 15);
    *new_r = (int16_t) ((((int32_t) (*new_r) * mix_new) + ((int32_t) old_r * mix_old)) >> 15);

    xfade->pos_q48_16 += active_phase_inc;
    xfade->fade_acc_q16 += xfade->step_q16;

    if (lut_i >= FADE_LUT_LEN - 1 || (xfade->pos_q48_16 >> 16) >= xfade->temp_buf_valid_samples) {
        xfade->active = false;
        xfade->fade_acc_q16 = 0;
    }
}

// buf_b = NEW, a = OLD. Returns true on termination — caller must promote pos_q48_16.
static inline bool tape_handle_crossfade_b_is_new(crossfade_t* xfade, uint32_t active_phase_inc, int16_t* a_l, int16_t* a_r) {
    if (!xfade->active)
        return false;

    int16_t b_l, b_r;
    tape_fetch_sample(xfade->pos_q48_16, xfade->buf_b_ptr_l, xfade->buf_b_ptr_r, &b_l, &b_r);

    uint32_t lut_i = xfade->fade_acc_q16 >> 16;
    if (lut_i >= FADE_LUT_LEN)
        lut_i = FADE_LUT_LEN - 1;

    int16_t mix_new = fade_in_lut[lut_i];
    int16_t mix_old = 32767 - mix_new;

    *a_l = (int16_t) ((((int32_t) (*a_l) * mix_old) + ((int32_t) b_l * mix_new)) >> 15);
    *a_r = (int16_t) ((((int32_t) (*a_r) * mix_old) + ((int32_t) b_r * mix_new)) >> 15);

    xfade->pos_q48_16 += active_phase_inc;
    xfade->fade_acc_q16 += xfade->step_q16;

    if (lut_i >= FADE_LUT_LEN - 1) {
        xfade->active = false;
        xfade->fade_acc_q16 = 0;
        return true;
    }

    return false;
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
        tape_fetch_sample(tape->pos_q48_16, tape->playback_buf->ch[0], tape->playback_buf->ch[1], out_l, out_r);

        tape_handle_fade_in(tape, out_l, out_r);

        // --- Q16 FIXED POINT FADE OUT TRIGGER ---
        if (!tape->fade_out.active && playhead_near_end(tape->pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc) && !tape->params.cyclic_mode) {
            tape->fade_out.active = true;
            tape->fade_out.pos_q48_16 = 0; // Reset accumulator
        }

        // --- Q16 FIXED POINT FADE OUT PROCESS ---
        tape_handle_fade_out(tape, active_phase_inc, out_l, out_r);

        // --- Cyclic Loop Trigger Logic ---
        if (!tape->xfade_cyclic.active && tape->params.cyclic_mode &&
            playhead_near_end(tape->pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc)) {
            // --- INITIALIZE CROSSFADE STATE (DO ONCE) ---

            tape->xfade_cyclic.active = true;
            tape->xfade_cyclic.pos_q48_16 = 1ULL << 16; // playhead of temp buffer is the new play buffer
            tape->xfade_cyclic.fade_acc_q16 = 0;        // Start LUT index at 0

            // Set temp buffer to beginning of playback buf.
            tape->xfade_cyclic.buf_b_ptr_l = tape->playback_buf->ch[0];
            tape->xfade_cyclic.buf_b_ptr_r = tape->playback_buf->ch[1];
        }

        // handle cyclic crossfade
        if (tape_handle_crossfade_b_is_new(&tape->xfade_cyclic, active_phase_inc, out_l, out_r)) {
            // cyclic crossfade finished. buf_b is now fully faded in.
            // promote temp buffer position to new playback buffer position.
            tape->pos_q48_16 = tape->xfade_cyclic.pos_q48_16;
        }

        // handle retrig crossfade
        tape_handle_crossfade_a_is_new(&tape->xfade_retrig, active_phase_inc, out_l, out_r);

        // advance main playhead
        advance_playhead_q48(&tape->pos_q48_16, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);
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

    uint32_t active_phase_inc = tape_compute_phase_increment(tape);

    // TODO: BIG TODO!!! OPTIMIZE FADE ALGS. currently with 128 sample buffer, the cpu is not fast enough. Maybe switch to q16.16 altogeher
    // n represents the sample index within the current DMA buffer (interleaved stereo, so step by 2)
    for (uint32_t n = 0; n < (AUDIO_BLOCK_SIZE / 2); n += 2) {
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
        case REC_REREC:
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
    t->swap_bufs_pending = false;

    // now that the new playback buffer holds the recorded audio at the respective decimation factor,
    // compute grit factor from this and save as parameter to be used in other dsp related functions.
    compute_grit(t);
}

static void tape_clear_slices(tape_buffer_t* buf) {
    // clear slice positions and reset slice index
    for (int i = 0; i < MAX_NUM_SLICES; i++) {
        buf->slice_positions[i] = 0;
    }
    buf->num_slices = 0;
}

static int tape_buf_get_slice_start_pos_q48_16(struct tape_player* t, uint64_t* out_pos) {
    uint32_t slice_idx = (uint32_t) (t->params.slice_pos * (t->playback_buf->num_slices - 1));
    uint32_t slice_pos = t->playback_buf->slice_positions[slice_idx];

    if (slice_pos < 1) {
        slice_pos = 1;
    }

    if (slice_pos >= t->playback_buf->valid_samples) {
        return -1;
    }

    uint32_t available = t->playback_buf->valid_samples - slice_pos;
    if (available < 4) {
        return -1;
    }

    *out_pos = (uint64_t) slice_pos << 16;
    return 0;
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
            if (t->swap_bufs_pending) {
                swap_tape_buffers(t);
                // only play can really prepare for next record.
                rec_fsm_event(t, TAPE_EVT_SWAP_DONE);
            }

            if (t->playback_buf->valid_samples < 4) {
                // not enough audio recorded to play, ignore play command
                return;
            }

            // aquire playback starting position depending on current set slice
            tape_buf_get_slice_start_pos_q48_16(t, &t->pos_q48_16);

            // Init Fade In
            t->fade_in.pos_q48_16 = 0;
            t->fade_in.active = true;

            // Ensure Fade Out is clean
            t->fade_out.active = false;
            t->fade_out.pos_q48_16 = 0; // this is not needed. Set anyway.
            t->fade_out.fade_acc_q16 = 0;

            envelope_set_attack_norm(&t->env, t->params.env_attack);
            envelope_set_decay_norm(&t->env, t->params.env_decay);
            envelope_note_on(&t->env);

            t->play_state = PLAY_PLAYING;
        }
        break;

    case PLAY_PLAYING:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- RETRIGGER PLAY ----- */

            // copy upcoming xfade.len samples starting at current phase
            // we need to start from one sample before the current phase for hermite interpolation
            uint32_t start_idx_xfade = (uint32_t) (t->pos_q48_16 >> 16) - 1;
            uint32_t max_needed_xfade = t->xfade_retrig.len + 3; // +3 for hermite interpolation safety (idx-1..idx+2)

            // number of samples available for crossfade starting from start_idx
            uint32_t available = t->playback_buf->valid_samples - start_idx_xfade;

            // if available samples are less than max_needed, we have to adjust crossfade length and fade LUT access accordingly, otherwise we might read out of bounds of the playback buffer (if start_idx + max_needed exceeds valid_samples), or the fade LUT (if we use max_needed as xfade.len for LUT access, but there are not enough valid samples in temp buffer).
            t->xfade_retrig.temp_buf_valid_samples = min_u32(available, max_needed_xfade);

            // switch buffers if pending. This has to happen after after copying the xfade buffer and calculating the available samples for xfade
            // buf_b_ptr holds the current playback buffer. On xfade it has to start at full volume and fade out.
            if (t->swap_bufs_pending) { // new record buffer waiting to be switched to playback buffer
                // use static temp buffer for fade, because playback buffer has now switched
                // fill up only until temp_buf_valid, but ramp down, so on i = temp_buf_valid, the sample reaches 0
                t->xfade_retrig.buf_b_ptr_l = xfade_retrig_temp_buf_l;
                t->xfade_retrig.buf_b_ptr_r = xfade_retrig_temp_buf_r;
                for (uint32_t i = 0; i < t->xfade_retrig.temp_buf_valid_samples; i++) {
                    t->xfade_retrig.buf_b_ptr_l[i] = t->playback_buf->ch[0][start_idx_xfade + i];
                    t->xfade_retrig.buf_b_ptr_r[i] = t->playback_buf->ch[1][start_idx_xfade + i];
                }
                swap_tape_buffers(t);
                rec_fsm_event(t, TAPE_EVT_SWAP_DONE);
            } else {
                // if no buffer switch pending, we can also just point the xfade buffer to the playback buffer, starting at the current phase. This saves us from copying the xfade buffer every time.
                t->xfade_retrig.buf_b_ptr_l = &t->playback_buf->ch[0][start_idx_xfade];
                t->xfade_retrig.buf_b_ptr_r = &t->playback_buf->ch[1][start_idx_xfade];
            }

            // aquire playback starting position depending on current set slice
            tape_buf_get_slice_start_pos_q48_16(t, &t->pos_q48_16);

            // ph_b holdes the phase for crossfade buffer
            t->xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1
            t->xfade_retrig.fade_acc_q16 = 0;
            t->xfade_retrig.active = true;
            t->xfade_retrig.len = FADE_XFADE_RETRIG_LEN;

            t->fade_in.active = true;

            // uint32_t base_ratio_q16 = ((uint32_t) (FADE_LUT_LEN - 1) << 16) / (t->xfade_retrig.temp_buf_valid_samples - 1);
            // t->xfade_retrig.base_ratio_q16 = base_ratio_q16;

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

// BEFORE SWAP, right after record stop
static inline void finalize_rec_buf(struct tape_player* t) {
    t->record_buf->valid_samples = t->tape_recordhead;
    t->tape_recordhead = 0;
    t->swap_bufs_pending = true;
}

// AFTER SWAP, right before record start
static inline void prepare_next_rec_buf(struct tape_player* t) {
    // apply decimation to recording buffer. This will be reach over to playback buffer by buffer swapping
    // prepare next rec buffer
#ifdef DECIMATION_FIXED
    t->record_buf->decimation = DECIMATION_FIXED;
#else
    t->record_buf->decimation = t->params.decimation;
#endif

    tape_clear_slices(t->record_buf);
    t->record_buf->slice_positions[0] = 1; // always start at 1 for Hermite
    t->record_buf->num_slices = 1;
}

static void rec_fsm_event(struct tape_player* t, tape_event_t evt) {
    switch (t->rec_state) {
    case REC_IDLE:
        if (evt == TAPE_EVT_RECORD) {
            prepare_next_rec_buf(t);

            t->rec_state = REC_RECORDING;
        }
        break;

    case REC_RECORDING:
        if (evt == TAPE_EVT_RECORD) {
            // new recording while already recording -> just switch buffers and start recording on the other one
            finalize_rec_buf(t);

            // pend wait for buffer swap
            t->rec_state = REC_REREC;
        } else if (evt == TAPE_EVT_RECORD_DONE || evt == TAPE_EVT_STOP) {
            finalize_rec_buf(t);

            t->rec_state = REC_DONE;
        }
        break;
    case REC_DONE:
        // wait for buffer so be swapped before allowing to record again.
        if (evt == TAPE_EVT_SWAP_DONE) {
            t->rec_state = REC_IDLE;
        }
        break;
    case REC_REREC:
        // wait for swap, than jump into next record
        if (evt == TAPE_EVT_SWAP_DONE) {
            prepare_next_rec_buf(t);

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

// set slice position marker to current recordhead position.
// TODO: slicing doesnt work correctly.
void tape_player_set_slice() {
    if (active_tape_player && active_tape_player->rec_state == REC_RECORDING) {
        uint32_t current_rec_pos = active_tape_player->tape_recordhead;
        uint32_t num_slices = active_tape_player->record_buf->num_slices;
        if (num_slices < MAX_NUM_SLICES) {
            active_tape_player->record_buf->slice_positions[num_slices] = current_rec_pos;
            active_tape_player->record_buf->num_slices++;
        }
    }
}

// sets params once per block, based on current UI and CV values.
void tape_player_set_params(struct param_cache param_cache) {
    if (active_tape_player) {
        active_tape_player->params.pitch_factor = param_cache.pitch_ui * param_cache.pitch_cv;
        active_tape_player->params.env_attack = param_cache.env_attack;
        active_tape_player->params.env_decay = param_cache.env_decay;
        active_tape_player->params.reverse = param_cache.reverse_mode;
        active_tape_player->params.cyclic_mode = param_cache.cyclic_mode;
        active_tape_player->params.decimation = param_cache.decimation;
        active_tape_player->params.slice_pos = param_cache.slice_pos;
    }
}

float tape_player_get_pitch() {
    if (active_tape_player) {
        return active_tape_player->params.pitch_factor;
    }
    return 0;
}

// returns value from 0..1 depending on the decimation factor
// this is used to blend in the hold sample for a grittier sound when decimation is high. At low decimation, we rely more on the hermite interpolation, which gives a smoother sound. At high decimation, the hermite interpolation can become less accurate and introduce more artifacts, so we blend in more of the hold sample to mask these artifacts and create a more "lo-fi" sound.
// possibility to use a power curve to have a stronger effect at higher decimation factors
float tape_player_get_grit() {
    if (active_tape_player) {
        return active_tape_player->params.grit;
    }
    return 0;
}