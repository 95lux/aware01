/**
 * @file tape_player_dsp.c
 * @brief Per-sample DSP core — interpolation, playhead advance, fades, crossfades.
 */
#include "tape_player.h"

#include <arm_math.h>
#include <stdbool.h>
#include <stdint.h>

#include "envelope.h"
#include "project_config.h"
#include "ressources.h"

#define Q32_UNITY (4294967296.0f)
#define Q16_UNITY (65536.0f)

// Shared tape player state, defined and owned by tape_player.c.
extern struct tape_player tape_player;

// Catmull-Rom (Hermite) cubic interpolation from a Q48.16 phase position.
// Reads four consecutive samples around pos and evaluates a cubic Hermite polynomial,
// giving band-limited sample-rate conversion.
// ref: https://www.musicdsp.org/en/latest/Other/93-hermite-interpollation.html
static inline float hermite_interpolate(uint64_t pos, int16_t* buffer, bool reverse) {
    uint32_t idx = (uint32_t) (pos >> 16);
    uint32_t frac = (uint32_t) (pos & 0xFFFF);

    float xm1, x0, x1, x2, t;

    if (reverse) {
        // interpolate between idx and idx-1, t runs 1->0 as pos decreases
        t = 1.0f - (frac * (1.0f / Q16_UNITY));
        xm1 = buffer[idx + 2];
        x0 = buffer[idx + 1];
        x1 = buffer[idx];
        x2 = buffer[idx - 1];
    } else {
        // interpolate between idx and idx+1, t runs 0->1 as pos increases
        t = frac * (1.0f / Q16_UNITY);
        int n = (int) idx - 1;
        xm1 = buffer[n];
        x0 = buffer[n + 1];
        x1 = buffer[n + 2];
        x2 = buffer[n + 3];
    }

    // estimate derivatives by finite differences
    // Catmull-Rom splines with Tension = 0
    float m0 = 0.5f * (x1 - xm1); // derivative at x0
    float m1 = 0.5f * (x2 - x0);  // derivative at x1

    // Rearranged cubic Hermite polynomial in Horner form: ((a*t - b)*t + c)*t + d
    // which evaluates a*t^3 - b*t^2 + c*t + d
    // Standard coefficients:
    //   d =  x0
    //   c =  m0
    //   a =  2*x0 - 2*x1 + m0 + m1   (t^3 term)
    //   -b = -3*x0 + 3*x1 - 2*m0 - m1  (t^2 term, sign absorbed into the a*t-b form)
    // Factored to minimise multiplications via intermediate variables v, w:
    float c = m0;
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + m1;
    float b = w + a;
    float d = x0;

    return (((a * t - b) * t + c) * t + d);
}

// Fetch one stereo sample at pos_q48_16 using Hermite interpolation,
// blended with a zero-order hold sample according to the current grit value.
// High grit (heavy decimation) -> more hold -> lo-fi texture.
static inline void tape_fetch_sample(uint64_t pos_q48_16, int16_t* buf_l, int16_t* buf_r, int16_t* out_l, int16_t* out_r) {
    uint32_t idx = (uint32_t) (pos_q48_16 >> 16);

    // --- Hold ---
    float hold_l = buf_l[idx];
    float hold_r = buf_r[idx];

#ifdef CONFIG_TAPE_PLAYER_ENABLE_HERMITE

    // --- Hermite ---
    float herm_l = hermite_interpolate(pos_q48_16, buf_l, tape_player.params.reverse);
    float herm_r = hermite_interpolate(pos_q48_16, buf_r, tape_player.params.reverse);

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

// Advance the Q48.16 playhead by one phase_inc_q16 step.
// Forward: wraps (cyclic) or clamps + stops (one-shot) at the buffer end.
// Reverse: wraps or clamps + stops at the buffer start (index 1, Hermite lower bound).
// Uses a subtraction loop instead of 64-bit modulo for cyclic wrap — avoids slow division on M7.
static inline void advance_playhead_q48(uint64_t* pos_q48, uint32_t phase_inc_q16, bool reverse, bool cyclic) {
    uint32_t valid_samples = tape_player.playback_buf->valid_samples;
    uint64_t wrap_point = (uint64_t) valid_samples << 16;

    // TODO: Reverse logic is buggy atm.
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

                // if we land on n=0 of the buffer, we force the first sample because hermite needs n-1 sample.
                if ((*pos_q48 >> 16) == 0) {
                    uint16_t frac = *pos_q48 & 0xFFFF; // keep fractional part
                    *pos_q48 = (1 << 16) | frac;       // integer part = 1, fractional part unchanged
                }
            } else {
                *pos_q48 = (uint64_t) (valid_samples - 4) << 16;
                tape_player_stop_play();
            }
        }
    }
}

// Returns true if the playhead will reach the buffer boundary within the next
// fade_len_samples output samples at the current phase increment.
// Uses cross-multiplication to avoid division:
//   (samples_left << 16) <= (fade_len_samples * active_phase_inc_q16)
// Keeps a 4-sample Hermite guard margin at each end.
static inline bool playhead_near_end(uint64_t pos_q48_16, uint32_t fade_len_samples, uint32_t active_phase_inc_q16) {
    uint32_t buf_size = tape_player.playback_buf->valid_samples;

    // Safety margin for Hermite (n+3)
    if (buf_size < 4)
        return false;
    uint32_t limit = buf_size - 4;

    if (tape_player.params.reverse) {
        // near the start of the buffer (index 1 = Hermite lower bound)
        uint32_t idx = (uint32_t) (pos_q48_16 >> 16);
        uint32_t lower_bound = 1;

        if (idx <= lower_bound)
            return true;

        uint32_t samples_left = idx - lower_bound;
        uint64_t distance_to_end_q16 = (uint64_t) samples_left << 16;
        uint64_t distance_during_fade_q16 = (uint64_t) fade_len_samples * active_phase_inc_q16;

        return distance_to_end_q16 <= distance_during_fade_q16;
    } else {
        uint32_t idx = (uint32_t) (pos_q48_16 >> 16);
        if (idx >= limit)
            return true;

        uint32_t samples_left = limit - idx;

        // We want to know if: samples_left <= (fade_len_samples * (active_phase_inc / 65536))
        // To avoid division, we cross-multiply:
        // (samples_left << 16) <= (fade_len_samples * active_phase_inc)
        uint64_t physical_distance_q16 = (uint64_t) samples_left << 16;
        uint64_t distance_during_fade_q16 = (uint64_t) fade_len_samples * active_phase_inc_q16;

        return physical_distance_q16 <= distance_during_fade_q16;
    }
}

// Apply fade-in to one output sample pair via a fixed-step Q16.16 accumulator into fade_in_lut.
// Skipped if a retrigger crossfade is active — that crossfade already handles the fade-in.
static inline void tape_handle_fade_in(int16_t* out_l, int16_t* out_r) {
    // --- Q16 FIXED POINT FADE IN ---
    // dont fade when retrigger crossfade is active, since that means we are already fading in.
    if (!tape_player.fade_in.active || tape_player.xfade_retrig.active)
        return;

    uint32_t lut_idx = tape_player.fade_in.fade_acc_q16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        tape_player.fade_in.active = false;
    } else {
        int16_t f = fade_in_lut[lut_idx];
        // LUT values are Q0.15 (0..32767). Multiply: int16 * Q0.15 -> Q1.15 (32-bit product).
        // >>15 brings result back to Q0.15 range, then SSAT clamps to 16-bit signed.
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // fixed step, not pitch-aware, since we want a consistent fade in time regardless of pitch. If we wanted a pitch-aware fade in, we would need to calculate the step based on the current phase increment, similar to the fade out below.
        tape_player.fade_in.fade_acc_q16 += FADE_IN_OUT_STEP_Q16;
    }
}

// Apply fade-out to one output sample pair. Skipped in cyclic mode — the loop
// crossfade handles boundary transitions there. Stops playback when LUT is exhausted.
static inline void tape_handle_fade_out(uint32_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    if (!tape_player.fade_out.active || tape_player.params.cyclic_mode)
        return;

    // LUT index = top 16 bits
    uint32_t lut_idx = tape_player.fade_out.fade_acc_q16 >> 16;

    if (lut_idx >= FADE_LUT_LEN - 1) {
        *out_l = 0;
        *out_r = 0;
        tape_player.fade_out.active = false;
        tape_player_stop_play();
    } else {
        int16_t f = fade_in_lut[FADE_LUT_LEN - 1 - lut_idx];
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // advance Q16.16 position
        tape_player.fade_out.fade_acc_q16 += FADE_IN_OUT_STEP_Q16;
    }
}

// Retrigger crossfade: buffer A (main playhead) is the new audio fading IN,
// buffer B (xfade->buf_b) is the old audio fading OUT
// main playhead already points to the new content.
static inline void tape_handle_crossfade(crossfade_t* xfade, uint32_t active_phase_inc, int16_t* new_l, int16_t* new_r) {
    int16_t old_l, old_r;
    tape_fetch_sample(xfade->pos_q48_16, xfade->buf_b_ptr_l, xfade->buf_b_ptr_r, &old_l, &old_r);

    uint32_t lut_i = xfade->fade_acc_q16 >> 16;
    if (lut_i >= FADE_LUT_LEN)
        lut_i = FADE_LUT_LEN - 1;

    int16_t mix_new = fade_in_lut[lut_i];
    int16_t mix_old = INT16_MAX - mix_new;

    *new_l = (int16_t) ((((int32_t) (*new_l) * mix_new) + ((int32_t) old_l * mix_old)) >> 15);
    *new_r = (int16_t) ((((int32_t) (*new_r) * mix_new) + ((int32_t) old_r * mix_old)) >> 15);

    xfade->pos_q48_16 += active_phase_inc;
    xfade->fade_acc_q16 += xfade->step_q16;

    if (lut_i >= FADE_LUT_LEN - 1 || (xfade->pos_q48_16 >> 16) >= xfade->temp_buf_valid_samples) {
        xfade->active = false;
        xfade->fade_acc_q16 = 0;
    }
}

// Convert pitch_factor to a Q16.16 phase increment, divided by the decimation factor
// so that playback speed is correct relative to the decimated sample rate.
static inline uint32_t tape_compute_phase_increment() {
#ifdef CONFIG_TAPE_PITCH_OVERRIDE
    float target_inc = CONFIG_TAPE_PITCH_OVERRIDE * 65536.0f;
#else
    float target_inc = tape_player.params.pitch_factor * 65536.0f;
#endif

    tape_player.curr_phase_inc_q16_16 = target_inc;

    uint32_t dec = tape_player.playback_buf->decimation > 0 ? tape_player.playback_buf->decimation : 1;
    // This result is now a Q16.16 increment
    // TODO: since decimation is always a power of 2, we could do this division via bit shift instead of actual division, which should be faster.
    // Compiler probably does NOT optimize this, since decimation is a runtime variable.
    // To change this from division to bit shift and gain performance, on param_cache needs to hold the shift value instead of the decimation factor.
    return (tape_player.curr_phase_inc_q16_16 / dec);
}

// Process one stereo output frame: fetch sample, apply fade-in, trigger/process
// fade-out (one-shot), trigger/process cyclic loop crossfade, process retrigger
// crossfade, advance main playhead.
static inline void tape_process_playback_frame(uint32_t active_phase_inc, int16_t* out_l, int16_t* out_r) {
    switch (tape_player.play_state) {
    case PLAY_STOPPED:
        // output silence
        break;
    case PLAY_PLAYING: {
        if (tape_player.pos_q48_16 < (1 << 16)) {
            // safety check to prevent out of bounds access in tape_fetch_sample
            *out_l = 0;
            *out_r = 0;
            tape_player.pos_q48_16 = 1 << 16; // move playhead to n=1 to ensure valid interpolation
            return;
        }

        tape_fetch_sample(tape_player.pos_q48_16, tape_player.playback_buf->ch[0], tape_player.playback_buf->ch[1], out_l, out_r);

#ifdef CONFIG_TAPE_PLAYER_ENABLE_FADE_IN_OUT
        tape_handle_fade_in(out_l, out_r);

        // --- Q16 FIXED POINT FADE OUT TRIGGER ---
        if (!tape_player.fade_out.active && playhead_near_end(tape_player.pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc) &&
            !tape_player.params.cyclic_mode) {
            tape_player.fade_out.active = true;
            tape_player.fade_out.pos_q48_16 = 0; // Reset accumulator
        }
        // --- Q16 FIXED POINT FADE OUT PROCESS ---
        tape_handle_fade_out(active_phase_inc, out_l, out_r);
#endif

        // --- Cyclic Loop Trigger Logic ---
        if (!tape_player.xfade_cyclic.active && tape_player.params.cyclic_mode &&
            playhead_near_end(tape_player.pos_q48_16, FADE_XFADE_CYCLIC_LEN, active_phase_inc)) {
            // --- INITIALIZE CROSSFADE STATE (DO ONCE) ---
            if (active_phase_inc < tape_player.xfade_cyclic.len << 16) {
                tape_player.xfade_cyclic.active = true;
                tape_player.xfade_cyclic.fade_acc_q16 = 0;

                // save current tail as outgoing (buf_b fades out)
                tape_player.xfade_cyclic.buf_b_ptr_l = &tape_player.playback_buf->ch[0][0];
                tape_player.xfade_cyclic.buf_b_ptr_r = &tape_player.playback_buf->ch[1][0];
                tape_player.xfade_cyclic.pos_q48_16 = tape_player.pos_q48_16;
                // jump main playhead to loop start immediately
                tape_player.pos_q48_16 = 1ULL << 16;
            } else {
                tape_player.xfade_cyclic.active = false;
            }
        }

        // handle cyclic crossfade
        if (tape_player.xfade_cyclic.active)
            tape_handle_crossfade(&tape_player.xfade_cyclic, active_phase_inc, out_l, out_r);

        // handle retrig crossfade
        if (tape_player.xfade_retrig.active)
            tape_handle_crossfade(&tape_player.xfade_retrig, active_phase_inc, out_l, out_r);

        // advance main playhead
        advance_playhead_q48(&tape_player.pos_q48_16, active_phase_inc, tape_player.params.reverse, tape_player.params.cyclic_mode);
    }
    }
}

// Write one stereo frame to the record buffer, applying decimation (record every Nth frame).
// Stops recording automatically when the buffer is full.
static inline void tape_process_recording_frame(int16_t* in_buf, uint32_t n) {
    if (tape_player.rec_state != REC_RECORDING)
        return;

    uint8_t rec_decimation = tape_player.record_buf->decimation > 0 ? tape_player.record_buf->decimation : 1;
    uint32_t frame_idx = n / 2;            // index of the current stereo frame (0, 1, 2, ...)
    if ((frame_idx % rec_decimation) != 0) // record only each Nth frame, where N = decimation factor
        return;

    // deinterleaves input buffer into tape buffer
    tape_player.record_buf->ch[0][tape_player.tape_recordhead] = in_buf[n];
    tape_player.record_buf->ch[1][tape_player.tape_recordhead] = in_buf[n + 1];
    tape_player.tape_recordhead++;

    // if tape has recorded all the way, stop recording for now.
    if (tape_player.tape_recordhead >= tape_player.record_buf->size) {
        tape_player_stop_record();
    }
}

// Main per-block entry point. Called from the audio engine on every DMA half-transfer.
void tape_player_process(int16_t* in_buf, int16_t* out_buf) {
    if (!tape_player.playback_buf->ch[0] || !tape_player.playback_buf->ch[1])
        return;

    uint32_t active_phase_inc = tape_compute_phase_increment();

    // n represents the sample index within the current DMA buffer (interleaved stereo, so step by 2)
    for (uint32_t n = 0; n < AUDIO_HALF_BLOCK_SIZE; n += 2) {
        int16_t out_l = 0;
        int16_t out_r = 0;

        switch (tape_player.play_state) {
        case PLAY_STOPPED:
            // output silence, but still process recording if active
            break;
        case PLAY_PLAYING:
            tape_process_playback_frame(active_phase_inc, &out_l, &out_r);
            break;
        }

#ifdef CONFIG_ENABLE_ENVELOPE
        float env_val = envelope_process(&tape_player.env);
        out_buf[n] = (int16_t) (out_l * env_val);
        out_buf[n + 1] = (int16_t) (out_r * env_val);
#else
        out_buf[n] = (int16_t) (out_l);
        out_buf[n + 1] = (int16_t) (out_r);
#endif

        switch (tape_player.rec_state) {
        case REC_IDLE:
            // do nothing
            break;
        case REC_RECORDING: {
            // record tape at current recordhead position
            // only record every second sample. Simple decimation.
            tape_process_recording_frame(in_buf, n);
            break;
        }
        case REC_REREC:
            break;
        case REC_DONE:
            break;
        }
    }
}
