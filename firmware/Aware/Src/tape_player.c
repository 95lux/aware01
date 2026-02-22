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

static tape_buffer_t playback_buffer_struct;
static tape_buffer_t record_buffer_struct;

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

int16_t temp_buf_l[FADE_RETRIG_XFADE_LEN + 3]; // +3 for hermite interpolation safety TODO: make sure this is long enough once configurable!
int16_t temp_buf_r[FADE_RETRIG_XFADE_LEN + 3]; // +3 for hermite interpolation safety

// TODO: this has to be max xfade length. Cant be super long unfortunately, since we dont have much SRAM available anymore :()
int16_t xfade_buf_l[128];
int16_t xfade_buf_r[128];

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
    tape_player->ph_a.idx = 1;  // start at sample 1 for interpolation
    tape_player->ph_a.frac = 0; // start at sample 1 for interpolation
    tape_player->ph_a.active = true;

    // playhead b for crossfading on retrigger/cycling, starts inactive
    tape_player->ph_b.idx = 1;  // start at sample 1 for interpolation
    tape_player->ph_b.frac = 0; // start at sample 1 for interpolation
    tape_player->ph_b.active = false;

    tape_player->fade_out_idx = 0;

    tape_player->xfade_retrig.temp_buf_l = temp_buf_l;
    tape_player->xfade_retrig.temp_buf_r = temp_buf_r;
    tape_player->xfade_retrig.len = FADE_RETRIG_XFADE_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->xfade_retrig.active = false;

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
static inline float hermite_interpolate(uint32_t idx, uint16_t frac, int16_t* buffer) {
    // fractional part as Q16 -> normalized t in [0,1)
    float t = frac * (1.0f / 65536.0f);

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

void advance_playhead(playhead_t* ph, uint32_t phase_inc, bool reverse, bool cyclic) {
    uint32_t valid_samples = active_tape_player->playback_buf->valid_samples;

    if (reverse) {
        // subtract phase_inc from frac
        if (ph->frac < phase_inc) {
            // need to borrow 1 from idx
            if (ph->idx == 0) {
                if (cyclic) {
                    ph->idx = valid_samples - 1;
                    ph->frac = 0x10000 + ph->frac - phase_inc; // wrap frac
                } else {
                    ph->idx = 0;
                    ph->frac = 0;
                }
            } else {
                ph->idx -= 1;
                ph->frac = 0x10000 + ph->frac - phase_inc; // borrow
            }
        } else {
            ph->frac -= phase_inc;
        }
    } else {
        // forward
        // add fractional increment
        uint32_t new_frac = ph->frac + phase_inc;
        ph->idx += new_frac >> 16; // carry into integer idx
        ph->frac = new_frac & 0xFFFF;

        if (cyclic && ph->idx >= valid_samples) {
            /* ----- cyclic mode ----- */
            ph->idx %= valid_samples; // integer wrap only, frac is already correct
        } else if (!cyclic && ph->idx >= valid_samples) {
            /* ----- oneshot mode ----- */
            ph->idx = valid_samples - 1;
            ph->frac = 0;
            tape_player_stop_play();
        }
    }
}

static inline bool playhead_near_end(playhead_t* ph, uint32_t fade_len_samples, uint32_t active_phase_inc) {
    uint32_t buf_size = active_tape_player->playback_buf->valid_samples;

    // Safety margin for Hermite (n+3)
    if (buf_size < 4)
        return false;
    uint32_t limit = buf_size - 4;

    // How many steps (at current pitch) are left until we hit the limit?
    uint32_t samples_left = (ph->idx < limit) ? (limit - ph->idx) : 0;

    // Convert fade_len_samples to the same "pitch scale" as the playhead.
    // If active_phase_inc is large (pitch up), we need to trigger much earlier
    // in terms of buffer index to ensure the fade has enough real-time cycles to finish.

    // Equivalent to: (samples_left << 16) / active_phase_inc <= fade_len_samples
    // But multiplication is safer/faster:
    return ((uint64_t) samples_left << 16) <= (uint64_t) fade_len_samples * active_phase_inc;
}

// worker function to process tape player state
void tape_player_process(struct tape_player* tape, int16_t* in_buf, int16_t* out_buf) {
    // TODO: implement circular tape buffer (?) - for now, just stop at the end of the buffer
    if (!tape || !tape->playback_buf->ch[0] || !tape->playback_buf->ch[1])
        return;

    // 1. Calculate Target
    float target_inc = tape->params.pitch_factor * 65536.0f;

    // 2. Fixed Alpha (No complex math/division)
    // 0.01f = 1% move per sample.
    // Increase to 0.005f for smoother/slower, 0.05f for snappier.
    const float alpha = 0.01f;

    // n represents the sample index within the current DMA buffer (interleaved stereo, so step by 2)
    for (uint32_t n = 0; n < (AUDIO_BLOCK_SIZE / 2); n += 2) {
        // --- The "Default" Stable IIR ---
        // current = current + alpha * (target - current)
        // This form is much more stable than the (target * coeff) version
        tape->current_phase_inc += alpha * (target_inc - tape->current_phase_inc);

        // apply decimation
        // Ensure decimation is at least 1 to avoid division by zero or weirdness
        uint8_t pb_decimation = tape->playback_buf->decimation > 0 ? tape->playback_buf->decimation : 1;
        float inv_decimation = 1.0f / (float) pb_decimation;

        // 3. Cast to uint32 for the playhead
        uint32_t active_phase_inc = (uint32_t) (tape->current_phase_inc * inv_decimation);

        int16_t out_l = 0;
        int16_t out_r = 0;

        switch (tape->play_state) {
        case PLAY_STOPPED:
            // output silence
            break;
        case PLAY_PLAYING:
            // Fetch raw samples with Hermite
            out_l = hermite_interpolate(tape->ph_a.idx, tape->ph_a.frac, tape->playback_buf->ch[0]);
            out_r = hermite_interpolate(tape->ph_a.idx, tape->ph_a.frac, tape->playback_buf->ch[1]);

            // 1. Determine Pitch-Aware Step
            // We use uint64_t for the intermediate multiplication to prevent overflow
            // This scales the fade speed perfectly with the playhead speed
            uint32_t base_ratio = (uint32_t) (((uint64_t) FADE_LUT_LEN << 16) / FADE_IN_OUT_LEN);
            uint32_t pitch_aware_step = (uint32_t) (((uint64_t) base_ratio * active_phase_inc) >> 16);

            // --- Q16 FIXED POINT FADE IN ---
            if (tape->fade_in_active && !tape->params.cyclic_mode) {
                uint32_t lut_idx = tape->fade_in_idx_q16 >> 16;

                if (lut_idx >= FADE_LUT_LEN - 1) {
                    tape->fade_in_active = false;
                } else {
                    int16_t f = fade_in_lut[lut_idx];
                    out_l = __SSAT(((int32_t) out_l * f) >> 15, 16);
                    out_r = __SSAT(((int32_t) out_r * f) >> 15, 16);
                    tape->fade_in_idx_q16 += FADE_IN_OUT_STEP_Q16;
                }
            }

            // --- Q16 FIXED POINT FADE OUT TRIGGER ---
            if (!tape->fade_out_active && playhead_near_end(&tape->ph_a, FADE_IN_OUT_LEN, active_phase_inc) && !tape->params.cyclic_mode) {
                tape->fade_out_active = true;
                tape->fade_out_idx_q16 = 0; // Reset accumulator
            }

            // --- Q16 FIXED POINT FADE OUT PROCESS ---
            if (tape->fade_out_active && !tape->params.cyclic_mode) {
                uint32_t lut_idx = tape->fade_out_idx_q16 >> 16;

                if (lut_idx >= FADE_LUT_LEN - 1) {
                    // Final sample safety: force silence and kill play state
                    out_l = 0;
                    out_r = 0;
                    tape->fade_out_active = false;
                    tape_player_stop_play();
                } else {
                    // Reverse index for fade out: (MaxIndex - Current)
                    int16_t f = fade_in_lut[(FADE_LUT_LEN - 1) - lut_idx];
                    out_l = __SSAT(((int32_t) out_l * f) >> 15, 16);
                    out_r = __SSAT(((int32_t) out_r * f) >> 15, 16);
                    tape->fade_out_idx_q16 += pitch_aware_step;
                }
            }

            // --- Cyclic Loop Trigger Logic ---
            if (!tape->fade_out_active && playhead_near_end(&tape->ph_a, FADE_IN_OUT_LEN, active_phase_inc) && tape->params.cyclic_mode) {
                tape->fade_out_active = true;
                tape->xfade_retrig.active = true;
                tape->ph_b.idx = 1; // start at sample 1 for interpolation
                tape->ph_b.frac = 0;
                tape->ph_b.active = true;
                tape->fade_out_idx_q16 = 0; // Reset lut accumulator for cyclic crossfade
            }
            // process cyclic loop crossfade
            if (tape->fade_out_active && tape->ph_b.active) {
                // Fetch samples for the Secondary Playhead (B) starting at the beginning
                int16_t b_l = hermite_interpolate(tape->ph_b.idx, tape->ph_b.frac, tape->playback_buf->ch[0]);
                int16_t b_r = hermite_interpolate(tape->ph_b.idx, tape->ph_b.frac, tape->playback_buf->ch[1]);

                // Calculate LUT index based on ph_b's progress from the start
                // This creates the Fade IN for B and Fade OUT for A
                uint32_t lut_idx = tape->fade_out_idx_q16 >> 16;

                int16_t fb = fade_in_lut[lut_idx];                    // B fades in
                int16_t fa = fade_in_lut[FADE_LUT_LEN - 1 - lut_idx]; // A fades out

                // Apply mix
                out_l = __SSAT(((int32_t) out_l * fa + (int32_t) b_l * fb) >> 15, 16);
                out_r = __SSAT(((int32_t) out_r * fa + (int32_t) b_r * fb) >> 15, 16);

                // Advance ph_b
                advance_playhead(&tape->ph_b, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);

                // Completion Check: Once B has covered the FADE_IN_OUT_LEN, B becomes A
                if (lut_idx >= FADE_LUT_LEN - 1) {
                    tape->ph_a = tape->ph_b; // Snap A to B's position
                    tape->ph_b.active = false;
                    tape->xfade_retrig.active = false;
                }
            }

            if (tape->xfade_retrig.active) {
                // --- dual playhead crossfade ---

                // these are the "new" playback samples. Recently recorded
                int16_t a_l = out_l;
                int16_t a_r = out_r;

                // ph_b.phase is starting from the beginning.
                int16_t b_l = hermite_interpolate(tape->ph_b.idx, tape->ph_b.frac, tape->xfade_retrig.temp_buf_l);
                int16_t b_r = hermite_interpolate(tape->ph_b.idx, tape->ph_b.frac, tape->xfade_retrig.temp_buf_r);

                int16_t fa;
                int16_t fb;

                if (tape->xfade_retrig.temp_buf_valid_samples < 2) {
                    // TODO: this seems odd. if not enough samples for crossfade, we should actually fade to silence, not jump to 0.
                    fa = 32767; // full a
                    fb = 0;     // b silent
                } else {
                    // Fade LUT access has to be scaled to crossfade length of available samples
                    uint32_t lut_i = (tape->ph_b.idx * (FADE_LUT_LEN - 1)) / (tape->xfade_retrig.temp_buf_valid_samples - 1);

                    fa = fade_in_lut[FADE_LUT_LEN - 1 - lut_i];
                    fb = fade_in_lut[lut_i];
                }
                // Q15 * Q15 -> Q30, then sum, then >>15 back to Q15
                int32_t acc_l = (int32_t) fa * (int32_t) a_l + (int32_t) fb * (int32_t) b_l;
                int32_t acc_r = (int32_t) fa * (int32_t) a_r + (int32_t) fb * (int32_t) b_r;

                // back to int16 Q15
                out_l = __SSAT(acc_l >> 15, 16);
                out_r = __SSAT(acc_r >> 15, 16);

                if (tape->ph_b.idx >= tape->xfade_retrig.temp_buf_valid_samples) {
                    // crossfade done -> promote B to A
                    // tape->ph_a = tape->ph_b; // promoting not needed anymore. A is already at the right position.
                    tape->ph_b.active = false;
                    tape->xfade_retrig.active = false;
                }
                advance_playhead(&tape->ph_b, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);
            }

            // advance ph_a
            if (tape->ph_a.active) {
                advance_playhead(&tape->ph_a, active_phase_inc, tape->params.reverse, tape->params.cyclic_mode);

                // stop only when reaching the end of the buffer in oneshot mode. In cyclic mode, we will just wrap around and never stop.
                // if (tape->ph_a.idx < tape->playback_buf->valid_samples - 3) {
                // } else {
                // tape_player_stop_play();
                // }
            }
        }

        float env_val = envelope_process(&tape->env);
        // float env_val = 1.0f;

        out_buf[n] = (int16_t) out_l * env_val;
        out_buf[n + 1] = (int16_t) out_r * env_val;

        switch (tape->rec_state) {
        case REC_IDLE:
            // do nothing
            break;
        case REC_RECORDING: {
            // record tape at current recordhead position
            // only record every second sample. Simple decimation.
            uint8_t rec_decimation = tape->record_buf->decimation > 0 ? tape->record_buf->decimation : 1;
            uint32_t frame_idx = n / 2;              // index of the current stereo frame (0, 1, 2, ...)
            if ((frame_idx % rec_decimation) == 0) { // record only each Nth frame, where N = decimation factor

                // deinterleaves input buffer into tape buffer
                tape->record_buf->ch[0][tape->tape_recordhead] = in_buf[n];
                tape->record_buf->ch[1][tape->tape_recordhead] = in_buf[n + 1];
                tape->tape_recordhead++;

                // if tape has recorded all the way, stop recording for now.
                if (tape->tape_recordhead >= tape->record_buf->size) {
                    tape_player_stop_record();
                }
            }
            break;
        }
        case REC_SWAP_PENDING:
            break;
        case REC_DONE:
            break;
        }
    }
}

static void swap_tape_buffers(struct tape_player* t) {
    // switch recorded buffer to playback buffer
    tape_buffer_t* temp = t->playback_buf;
    t->playback_buf = t->record_buf;
    t->record_buf = temp;
    t->switch_bufs_pending = false;
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

            t->ph_a.idx = 1;
            t->ph_a.frac = 0;

            // Init Fade In
            t->fade_in_idx_q16 = 0;
            t->fade_in_active = true;

            // Ensure Fade Out is clean
            t->fade_out_active = false;
            t->fade_out_idx_q16 = 0;

            t->play_state = PLAY_PLAYING;
        }
        break;

    case PLAY_PLAYING:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- RETRIGGER PLAY ----- */

            // copy upcoming xfade.len samples starting at current phase
            // we need to start from one sample before the current phase for hermite interpolation
            uint32_t start_idx = t->ph_a.idx - 1;
            uint32_t max_needed = t->xfade_retrig.len + 3; // +3 for hermite interpolation safety (idx-1..idx+2)

            // number of samples available for crossfade starting from start_idx
            uint32_t available = t->playback_buf->valid_samples - start_idx;

            // if available samples are less than max_needed, we have to adjust crossfade length and fade LUT access accordingly, otherwise we might read out of bounds of the playback buffer (if start_idx + max_needed exceeds valid_samples), or the fade LUT (if we use max_needed as xfade.len for LUT access, but there are not enough valid samples in temp buffer).
            t->xfade_retrig.temp_buf_valid_samples = min_u32(available, max_needed);

            // switch buffers if pending. This has to happen after after copying the xfade buffer and calculating the available samples for xfade
            if (t->switch_bufs_pending) {
                // use static temp buffer for fade, because playback buffer has now switched
                // fill up only until temp_buf_valid, but ramp down, so on i = temp_buf_valid, the sample reaches 0
                t->xfade_retrig.temp_buf_l = temp_buf_l;
                t->xfade_retrig.temp_buf_r = temp_buf_r;
                for (uint32_t i = 0; i < t->xfade_retrig.temp_buf_valid_samples; i++) {
                    t->xfade_retrig.temp_buf_l[i] = t->playback_buf->ch[0][start_idx + i];
                    t->xfade_retrig.temp_buf_r[i] = t->playback_buf->ch[1][start_idx + i];
                }
                swap_tape_buffers(t);
                rec_fsm_event(t, TAPE_EVT_SWAP_DONE);
            } else {
                // if no buffer switch pending, we can also just point the xfade buffer to the playback buffer, starting at the current phase. This saves us from copying the xfade buffer every time.
                t->xfade_retrig.temp_buf_l = &t->playback_buf->ch[0][start_idx];
                t->xfade_retrig.temp_buf_r = &t->playback_buf->ch[1][start_idx];
            }

            t->ph_a.idx = 1;
            t->ph_a.frac = 0;

            // ph_b holdes the phase for crossfade buffer
            t->ph_b.idx = 1;
            t->ph_b.frac = 0;
            t->ph_b.active = true;
            t->xfade_retrig.len = FADE_RETRIG_XFADE_LEN;
            t->xfade_retrig.active = true;

            t->fade_out_idx = 0;

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
            t->record_buf->decimation = t->params.decimation;
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

            t->rec_state = REC_SWAP_PENDING;
        }
        break;
    case REC_DONE:
        //swap is done, jump to.
        if (evt == TAPE_EVT_SWAP_DONE) {
            t->record_buf->decimation = t->params.decimation;

            t->rec_state = REC_IDLE;
        }
        break;
    case REC_SWAP_PENDING:
        //swap is done, prepare for next record.
        if (evt == TAPE_EVT_SWAP_DONE) {
            t->record_buf->decimation = t->params.decimation;

            t->rec_state = REC_RECORDING;
        }
        break;
    }
}

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