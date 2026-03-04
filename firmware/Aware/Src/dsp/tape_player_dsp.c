/**
 * @file tape_player_dsp.c
 * @brief Per-sample DSP core for the tape player — interpolation, playhead
 *        advancement, fades, crossfades, and the main audio processing loop.
 *
 * All functions here are called from the audio task on every DMA half-transfer
 * and must complete within one audio block. The tape player state is owned by
 * tape_player.c; this file accesses it via the shared @c tape_player instance.
 *
 * @see tape_player.c  for FSM, init, and public control API.
 */
#include "tape_player.h"

#include "audioengine.h"
#include "envelope.h"
#include "project_config.h"
#include <arm_math.h>
#include <stdbool.h>
#include <stdint.h>

#include "ressources.h"
#include "util.h"

#define Q32_UNITY (4294967296.0f)
#define Q16_UNITY (65536.0f)

/** Shared tape player state, defined and owned by tape_player.c. */
extern struct tape_player tape_player;

/**
 * @brief Catmull-Rom (Hermite) interpolation from a Q48.16 phase position.
 *
 * Reads four consecutive samples around @p pos and evaluates a cubic Hermite
 * polynomial, giving band-limited sample-rate conversion. The @p reverse flag
 * mirrors the neighbourhood so that the same LUT works during reverse playback.
 *
 * @see https://www.musicdsp.org/en/latest/Other/93-hermite-interpollation.html
 * @see https://ldesoras.fr/doc/articles/resampler-en.pdf
 *
 * @param pos     Q48.16 playhead position (upper 48 bits = integer index,
 *                lower 16 bits = fractional part).
 * @param buffer  Source sample buffer (must have ≥ 3 guard samples on both sides).
 * @param reverse If true, reads neighbours in the reverse direction.
 * @return        Interpolated floating-point sample value.
 */
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

/**
 * @brief Fetch one stereo sample from a tape buffer at a given Q48.16 position.
 *
 * Selects between Hermite interpolation and zero-order hold based on
 * @c CONFIG_TAPE_PLAYER_ENABLE_HERMITE. When Hermite is enabled the two outputs
 * are blended with the hold sample according to the current grit value, giving
 * a lo-fi texture at high decimation factors.
 *
 * @param pos_q48_16  Playhead position in Q48.16 fixed-point format.
 * @param buf_l       Left channel sample buffer.
 * @param buf_r       Right channel sample buffer.
 * @param[out] out_l  Left channel output sample (saturated to 16 bits).
 * @param[out] out_r  Right channel output sample (saturated to 16 bits).
 */
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

/**
 * @brief Advance the Q48.16 playhead with bounds checking, wrap, and clamp logic.
 *
 * The increment is a Q16.16 value (not Q48.16) for performance: 64-bit addition
 * is fast on Cortex-M7, whereas 128-bit arithmetic would be required for a full
 * Q48.16 increment.
 *
 * **Forward mode**: playhead is incremented each call. On overflow:
 * - Cyclic: wraps back to the beginning using a subtraction loop (avoids 64-bit
 *   division), and forces index ≥ 1 to satisfy Hermite's n−1 requirement.
 * - One-shot: clamps to the last safe sample and calls @c tape_player_stop_play().
 *
 * **Reverse mode**: playhead is decremented each call. On underflow:
 * - Cyclic: wraps to the end of the valid buffer.
 * - One-shot: clamps to 0 and calls @c tape_player_stop_play().
 *
 * @param pos_q48        Pointer to the Q48.16 playhead position (modified in place).
 * @param phase_inc_q16  Q16.16 phase increment per sample (pitch × 65536 / decimation).
 * @param reverse        If true, decrement the playhead (reverse playback).
 * @param cyclic         If true, wrap around at buffer boundaries instead of stopping.
 */
static inline void advance_playhead_q48(uint64_t* pos_q48, uint32_t phase_inc_q16, bool reverse, bool cyclic) {
    uint32_t valid_samples = tape_player.playback_buf->valid_samples;
    uint64_t wrap_point = (uint64_t) valid_samples << 16;

    // TODO: Reverse logic is super buggy atm.
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

/**
 * @brief Check whether the playhead will reach the buffer boundary within the
 *        upcoming fade window.
 *
 * Uses a cross-multiplication trick to avoid division:
 * @code
 *   physical_distance_q16 <= fade_len_samples * active_phase_inc_q16
 * @endcode
 * Accounts for both forward and reverse playback directions, and keeps a 4-sample
 * Hermite guard margin at each end of the buffer.
 *
 * @param pos_q48_16           Current Q48.16 playhead position.
 * @param fade_len_samples     Length of the fade window in output samples.
 * @param active_phase_inc_q16 Current Q16.16 phase increment (pitch + decimation).
 * @return                     @c true if the playhead will reach the boundary within
 *                             the fade window, @c false otherwise.
 */
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
        uint64_t physical_distance_q16 = (uint64_t) samples_left << 16;
        uint64_t fade_travel_distance_q16 = (uint64_t) fade_len_samples * active_phase_inc_q16;

        return physical_distance_q16 <= fade_travel_distance_q16;
    } else {
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
}

/**
 * @brief Apply the fade-in envelope to one output sample pair.
 *
 * Reads the fade LUT via a Q16.16 accumulator so the fade speed is independent
 * of the current pitch factor. No-ops if the fade is inactive or if a retrigger
 * crossfade is already active (which handles the fade-in itself).
 *
 * @param[in,out] out_l  Left channel sample, attenuated in place.
 * @param[in,out] out_r  Right channel sample, attenuated in place.
 */
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
        *out_l = __SSAT(((int32_t) (*out_l) * f) >> 15, 16);
        *out_r = __SSAT(((int32_t) (*out_r) * f) >> 15, 16);

        // fixed step, not pitch-aware, since we want a consistent fade in time regardless of pitch. If we wanted a pitch-aware fade in, we would need to calculate the step based on the current phase increment, similar to the fade out below.
        tape_player.fade_in.fade_acc_q16 += FADE_IN_OUT_STEP_Q16;
    }
}

/**
 * @brief Apply the fade-out envelope to one output sample pair.
 *
 * Reads the fade LUT in reverse via a Q16.16 accumulator. Skips in cyclic mode
 * because loop crossfading handles boundary transitions there. When the LUT is
 * exhausted the output is forced to zero and @c tape_player_stop_play() is called.
 *
 * @param active_phase_inc  Current Q16.16 phase increment (unused here, kept for
 *                          API symmetry with other fade helpers).
 * @param[in,out] out_l     Left channel sample, attenuated in place.
 * @param[in,out] out_r     Right channel sample, attenuated in place.
 */
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

/**
 * @brief Process one sample of a crossfade where buffer A is the incoming (new) audio.
 *
 * Used for the retrigger crossfade: the current playback buffer (A) fades in while
 * the old audio stored in @c xfade->buf_b fades out. No position promotion is needed
 * on completion because the main playhead already points to the new buffer.
 *
 * @param xfade            Crossfade state (buf_b holds the OLD audio fading out).
 * @param active_phase_inc Current Q16.16 phase increment used to advance buf_b's position.
 * @param[in,out] new_l    Left channel from the new buffer; blended in place with old audio.
 * @param[in,out] new_r    Right channel from the new buffer; blended in place with old audio.
 */
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

/**
 * @brief Process one sample of a crossfade where buffer B is the incoming (new) audio.
 *
 * Used for the cyclic loop crossfade: audio from @c xfade->buf_b (the loop start) fades
 * in while the current playback position (A) fades out. On completion the caller
 * must promote @c xfade->pos_q48_16 to the main playhead so playback continues
 * seamlessly from the new position.
 *
 * @param xfade            Crossfade state (buf_b holds the NEW audio fading in).
 * @param active_phase_inc Current Q16.16 phase increment used to advance buf_b's position.
 * @param[in,out] a_l      Left channel from the old playback position; blended in place.
 * @param[in,out] a_r      Right channel from the old playback position; blended in place.
 * @return                 @c true when the crossfade is complete (caller must promote position).
 */
static inline bool tape_handle_crossfade_b_is_new(crossfade_t* xfade, uint32_t active_phase_inc, int16_t* a_l, int16_t* a_r) {
    if (!xfade->active)
        return false;

    int16_t b_l, b_r;
    // TODO: on very high pitch ratios in cyclic mode (pitch fader turned to 100% and V/Oct input is cranked up), some miscalculation happens, and we try to fetch a sample out of bounds.
    // probably has to do with wrapping logic in advance playhead.
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

/**
 * @brief Compute the Q16.16 phase increment for the current block.
 *
 * Converts the floating-point pitch factor to a Q16.16 integer and divides by
 * the active decimation factor, so that pitch-shifted playback stays at the
 * correct speed relative to the decimated sample rate.
 *
 * @return Q16.16 phase increment to pass to @c advance_playhead_q48().
 */
static inline uint32_t tape_compute_phase_increment() {
    float target_inc = tape_player.params.pitch_factor * 65536.0f;

    //TODO: is filtering pitch per block necessary? Evaluate if zipper noise is occuring without it. Was heavier before, seems gone now.
    // // 0.01f = 1% move per sample.
    // // Increase to 0.005f for smoother/slower, 0.05f for snappier.
    // const float alpha = 0.01f;
    // //--- The "Default" Stable IIR ---
    // // current = current + alpha * (target - current)
    // // This form is much more stable than the (target * coeff) version
    // tape_player.current_phase_inc += alpha * (target_inc - tape_player.current_phase_inc);

    tape_player.curr_phase_inc_q16_16 = target_inc;

    uint32_t dec = tape_player.playback_buf->decimation > 0 ? tape_player.playback_buf->decimation : 1;
    // This result is now a Q16.16 increment
    return (tape_player.curr_phase_inc_q16_16 / dec);
}

/**
 * @brief Process one stereo output frame of tape playback.
 *
 * Called once per sample inside the main DMA loop. Handles, in order:
 * -# Sample fetch with Hermite/hold interpolation
 * -# Fade-in at playback start
 * -# Fade-out trigger and processing near the buffer end (one-shot mode)
 * -# Cyclic crossfade trigger and processing at the loop boundary
 * -# Retrigger crossfade processing
 * -# Main playhead advance
 *
 * @param active_phase_inc  Q16.16 phase increment for this block.
 * @param[out] out_l        Left channel output sample.
 * @param[out] out_r        Right channel output sample.
 */
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

        tape_handle_fade_in(out_l, out_r);

        // --- Q16 FIXED POINT FADE OUT TRIGGER ---
        if (!tape_player.fade_out.active && playhead_near_end(tape_player.pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc) &&
            !tape_player.params.cyclic_mode) {
            tape_player.fade_out.active = true;
            tape_player.fade_out.pos_q48_16 = 0; // Reset accumulator
        }

        // --- Q16 FIXED POINT FADE OUT PROCESS ---
        tape_handle_fade_out(active_phase_inc, out_l, out_r);

        // --- Cyclic Loop Trigger Logic ---
        if (!tape_player.xfade_cyclic.active && tape_player.params.cyclic_mode &&
            playhead_near_end(tape_player.pos_q48_16, FADE_IN_OUT_LEN, active_phase_inc)) {
            // --- INITIALIZE CROSSFADE STATE (DO ONCE) ---
            if (active_phase_inc > tape_player.xfade_cyclic.len << 16) {
                tape_player.xfade_cyclic.active = true;
                tape_player.xfade_cyclic.pos_q48_16 = 1ULL << 16; // playhead of temp buffer is the new play buffer
                tape_player.xfade_cyclic.fade_acc_q16 = 0;        // Start LUT index at 0

                // Set temp buffer to beginning of playback buf.
                tape_player.xfade_cyclic.buf_b_ptr_l = tape_player.playback_buf->ch[0];
                tape_player.xfade_cyclic.buf_b_ptr_r = tape_player.playback_buf->ch[1];
            } else {
                // if phase inc is larger than crossfade length, just do hard switch without crossfade to avoid illegal buffer access
                tape_player.xfade_cyclic.active = false;
            }
        }

        // handle cyclic crossfade
        if (tape_handle_crossfade_b_is_new(&tape_player.xfade_cyclic, active_phase_inc, out_l, out_r)) {
            // cyclic crossfade finished. buf_b is now fully faded in.
            // promote temp buffer position to new playback buffer position.
            tape_player.pos_q48_16 = tape_player.xfade_cyclic.pos_q48_16;
        }

        // handle retrig crossfade
        tape_handle_crossfade_a_is_new(&tape_player.xfade_retrig, active_phase_inc, out_l, out_r);

        // advance main playhead
        advance_playhead_q48(&tape_player.pos_q48_16, active_phase_inc, tape_player.params.reverse, tape_player.params.cyclic_mode);
    }
    }
}

/**
 * @brief Record one stereo frame from the DMA input buffer into the record tape buffer.
 *
 * Applies decimation by only writing every Nth stereo frame, where N is the
 * configured decimation factor. Stops recording automatically when the tape buffer
 * is full.
 *
 * @param in_buf  Pointer to the interleaved stereo DMA input buffer.
 * @param n       Current frame byte-offset within @p in_buf (step of 2 for stereo).
 */
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

/**
 * @brief Main per-block audio processing entry point.
 *
 * Called from the audio engine ISR/task for every DMA half-transfer. Iterates
 * over all stereo frames in the block, dispatching to @c tape_process_playback_frame()
 * and @c tape_process_recording_frame(), then applies the amplitude envelope.
 *
 * @param in_buf   Interleaved stereo input buffer (from ADC/codec DMA).
 * @param out_buf  Interleaved stereo output buffer (to DAC/codec DMA).
 */
void tape_player_process(int16_t* in_buf, int16_t* out_buf) {
    if (!tape_player.playback_buf->ch[0] || !tape_player.playback_buf->ch[1])
        return;

    uint32_t active_phase_inc = tape_compute_phase_increment();

    // n represents the sample index within the current DMA buffer (interleaved stereo, so step by 2)
    for (uint32_t n = 0; n < (AUDIO_BLOCK_SIZE / 2); n += 2) {
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

        float env_val = envelope_process(&tape_player.env);
        out_buf[n] = (int16_t) (out_l * env_val);
        out_buf[n + 1] = (int16_t) (out_r * env_val);

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
