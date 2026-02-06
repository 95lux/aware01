#include "tape_player.h"

#include "project_config.h"
#include "string.h"
#include <arm_math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/swo_log.h"
#include "ressources.h"
#include "util.h"

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

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
    tape_player->dma_buf_size = dma_buf_size;
    tape_player->playback_buf.ch[0] = tape_play_buf_l;
    tape_player->playback_buf.ch[1] = tape_play_buf_r;
    tape_player->playback_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->playback_buf.valid_samples = 0;
    tape_player->record_buf.ch[0] = tape_rec_buf_l;
    tape_player->record_buf.ch[1] = tape_rec_buf_r;
    tape_player->record_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->record_buf.valid_samples = 0;

    // playhead and reacordhead init
    tape_player->ph_a.phase = 1 << 16; // start at sample 1 for interpolation
    tape_player->ph_b.phase = 1 << 16; // start at sample 1 for interpolation

    tape_player->buffer_end_fade_pos = 0;
    tape_player->xfade_retrig.len = 128; // crossfade length in samples TODO: make configurable via MACRO
    tape_player->tape_recordhead = 0;

    // state logic
    tape_player->play_state = PLAY_STOPPED;
    tape_player->rec_state = REC_IDLE;

    // envelope init
    tape_player->env.state = ENV_IDLE;
    tape_player->env.value = 0.0f;
    tape_player->env.attack_inc = 1 / (0.001f * AUDIO_SAMPLE_RATE);
    tape_player->env.decay_inc = 1 / (0.5f * AUDIO_SAMPLE_RATE);
    tape_player->env.sustain = 0.0f;

    // parameters
    // TODO: switch to state events
    tape_player->cyclic_mode = false; // default to oneshot mode

    tape_player->switch_bufs_pending = false;
    tape_player->params.pitch_factor = 1.0f; // TODO: read out pitch fader on init?

    // init cmd
    tape_player->tape_cmd_q = cmd_queue;

    active_tape_player = tape_player;

    return 0;
}

// from https://www.musicdsp.org/en/latest/Other/93-hermite-interpollation.html
// explained here https://ldesoras.fr/doc/articles/resampler-en.pdf
// uses phase accumulator
float hermite_interpolate(uint32_t phase, int16_t* buffer) {
    uint32_t idx = phase >> 16; // integer part
    float t = (phase & 0xFFFF) * (1.0f / 65536.0f);

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

static inline void advance_playhead(playhead_t* ph, uint32_t phase_inc) {
    if (!ph->active)
        return;

    ph->phase += phase_inc;

    // Wraparound the integer part if it exceeds buffer size
    // TODO: where to implement this wrapping logic?
    uint32_t idx = ph->phase >> 16; // integer part
    if (idx >= active_tape_player->playback_buf.size) {
        idx %= active_tape_player->playback_buf.size;   // wrap
        ph->phase = (idx << 16) | (ph->phase & 0xFFFF); // keep fractional
    }
}

// checks if playhead is near the end of the valid samples of the buffer and has to start fading into next buffer/silence
static inline bool playhead_near_end(playhead_t* ph, uint32_t fade_len) {
    uint32_t buf_size = active_tape_player->playback_buf.valid_samples;

    // samples → phase
    uint32_t end_phase = ((buf_size - 2) << 16); // hermite safety (idx+2)
    uint32_t fade_phase = fade_len << 16;

    volatile uint16_t fade_phase_int = fade_phase >> 16;
    volatile uint16_t end_phase_int = end_phase >> 16;
    volatile uint16_t ph_phase_int = ph->phase >> 16;

    return ph->phase + fade_phase >= end_phase;
}

// worker function to process tape player state
void tape_player_process(struct tape_player* tape, int16_t* dma_in_buf, int16_t* dma_out_buf) {
    // TODO: implement circular tape buffer (?) - for now, just stop at the end of the buffer
    if (!tape || !tape->playback_buf.ch[0] || !tape->playback_buf.ch[1])
        return;

    // process half of the block
    for (uint32_t n = 0; n < (tape->dma_buf_size / 2) - 1; n += 2) {
        int16_t out_l = 0;
        int16_t out_r = 0;

        uint32_t phase_inc = tape->params.pitch_factor * 65536.0f;

        switch (tape->play_state) {
        case PLAY_STOPPED:
            // output silence
            break;
        case PLAY_PLAYING:
            // fetch samples with Hermite interpolation
            out_l += hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[0]);
            out_r += hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[1]);

            // if approaching end of valid samples in buffer, start fading out to prevent clicks.
            if (playhead_near_end(&tape->ph_a, FADE_OUT_LENGTH)) {
                int16_t f = fade_in_lut[FADE_LUT_LEN - 1 - tape->buffer_end_fade_pos];

                // intermediate 32 bit accumulator to prevent overflow when multiplying two Q15 values.
                int32_t acc_l = (int32_t) out_l * (int32_t) f;
                int32_t acc_r = (int32_t) out_r * (int32_t) f;

                out_l = __SSAT(acc_l >> 15, 16);
                out_r = __SSAT(acc_r >> 15, 16);

                tape->buffer_end_fade_pos++;

                if (tape->buffer_end_fade_pos >= FADE_OUT_LENGTH) {
                    tape_player_stop_play(); // stop playback at end
                }
            }

            if (tape->xfade_retrig.active) {
                // --- dual playhead crossfade ---

                // these are the "new" playback samples. Recently recorded
                int16_t a_l = out_l;
                int16_t a_r = out_r;

                // ph_b.phase is starting from the beginning
                uint32_t b_idx = tape->ph_b.phase >> 16;
                // check upper bound for Hermite (needs idx-1..idx+2)
                if (b_idx < 1 || b_idx + 2 >= tape->xfade_retrig.len + 3) {
                    // breakpoint here in debugger
                    b_idx = tape->xfade_retrig.len - 3; // clamp to safe max
                    tape->ph_b.phase = (b_idx << 16) | (tape->ph_b.phase & 0xFFFF);
                }

                // ph_b.phase is starting from the beginning.
                int16_t b_l = hermite_interpolate(tape->ph_b.phase, tape->xfade_retrig.temp_buf_l);
                int16_t b_r = hermite_interpolate(tape->ph_b.phase, tape->xfade_retrig.temp_buf_r);

                int16_t fa;
                int16_t fb;

                if (tape->xfade_retrig.temp_buf_valid_samples < 2) {
                    // TODO: this seems odd. if not enough samples for crossfade, we should actually fade to silence, not jump to 0.
                    fa = 32767; // full a
                    fb = 0;     // b silent
                } else {
                    // Fade LUT access has to be scaled to crossfade length of available samples
                    uint32_t lut_i = (tape->xfade_retrig.pos * (FADE_LUT_LEN - 1)) / (tape->xfade_retrig.temp_buf_valid_samples - 1);

                    fa = fade_in_lut[FADE_LUT_LEN - 1 - lut_i];
                    fb = fade_in_lut[lut_i];
                }
                // Q15 * Q15 -> Q30, then sum, then >>15 back to Q15
                int32_t acc_l = (int32_t) fa * (int32_t) a_l + (int32_t) fb * (int32_t) b_l;
                int32_t acc_r = (int32_t) fa * (int32_t) a_r + (int32_t) fb * (int32_t) b_r;

                // back to int16 Q15
                out_l = __SSAT(acc_l >> 15, 16);
                out_r = __SSAT(acc_r >> 15, 16);

                tape->xfade_retrig.pos++;

                if (tape->xfade_retrig.pos >= tape->xfade_retrig.temp_buf_valid_samples) {
                    // crossfade done -> promote B to A
                    // tape->ph_a = tape->ph_b; // promoting not needed anymore. A is already at the right position.
                    tape->ph_b.active = false;
                    tape->xfade_retrig.active = false;
                }
            }

            // advance ph_a
            uint32_t idx_a = tape->ph_a.phase >> 16;
            if (tape->ph_a.active) {
                if (tape->cyclic_mode || (!tape->cyclic_mode && idx_a + 2 < tape->playback_buf.valid_samples)) {
                    tape->ph_a.phase += phase_inc;
                }
            }
            // advance ph_b (xfade buffer)
            if (tape->xfade_retrig.active && tape->ph_b.active) {
                if (tape->cyclic_mode || (!tape->cyclic_mode && idx_a + 2 < tape->xfade_retrig.len + 3)) {
                    tape->ph_b.phase += phase_inc;
                }
            }
            break;
        }

        float env_val = envelope_process(&tape->env);
        dma_out_buf[n] = (int16_t) out_l * env_val;
        dma_out_buf[n + 1] = (int16_t) out_r * env_val;

        // while recording, pitch factor does not matter at all.
        // EVAL: maybe allow recording at pitch factor? could lead to interesting artifacts.
        if (tape->rec_state == REC_RECORDING) {
            // record tape at current recordhead position
            // if tape has recorded all the way, stop recording for now.
            if (tape->tape_recordhead >= tape->playback_buf.size) {
                tape->rec_state = REC_IDLE;
                tape->switch_bufs_pending = true;
                tape_player_stop_record();
            }
            // deinterleaves input buffer into tape buffer
            tape->record_buf.ch[0][tape->tape_recordhead] = dma_in_buf[n];
            tape->record_buf.ch[1][tape->tape_recordhead] = dma_in_buf[n + 1];
            tape->tape_recordhead++;
        }
    }
}

static void swap_tape_buffers(struct tape_player* t) {
    // switch recorded buffer to playback buffer
    tape_buffer_t temp = t->playback_buf;
    t->playback_buf = t->record_buf;
    t->record_buf = temp;

    // avoid clearing by now. Could be expensive for large tape buffers
    // // clear rec buffers after switching pointers
    // memset(t->record_buf.ch[0], 0, sizeof(int16_t) * t->record_buf.size);
    // memset(t->record_buf.ch[1], 0, sizeof(int16_t) * t->record_buf.size);
}

static void play_fsm_event(struct tape_player* t, tape_event_t evt) {
    switch (t->play_state) {
    case PLAY_STOPPED:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- PLAY FROM IDLE ----- */

            // each time a play event is triggered, switch buffers if pending.
            if (t->switch_bufs_pending) {
                t->playback_buf.valid_samples = t->record_buf.valid_samples;
                swap_tape_buffers(t);
                t->switch_bufs_pending = false;
            }

            if (t->playback_buf.valid_samples < 4) {
                // not enough samples to play
                return;
            }

            envelope_note_on(&t->env);

            t->ph_a.phase = 1 << 16;
            t->ph_a.active = true;
            t->buffer_end_fade_pos = 0;

            t->play_state = PLAY_PLAYING;
        }
        break;

    case PLAY_PLAYING:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- RETRIGGER PLAY ----- */

            // copy upcoming xfade.len samples starting at current phase
            // we need to start from one sample before the current phase for hermite interpolation
            uint32_t start_idx = (t->ph_a.phase >> 16) - 1;
            uint32_t max_needed = t->xfade_retrig.len + 3; // +3 for hermite interpolation safety (idx-1..idx+2)

            // number of samples available for crossfade starting from start_idx
            uint32_t available = t->playback_buf.valid_samples - (start_idx + 1);

            // if available samples are less than max_needed, we have to adjust crossfade length and fade LUT access accordingly, otherwise we might read out of bounds of the playback buffer (if start_idx + max_needed exceeds valid_samples), or the fade LUT (if we use max_needed as xfade.len for LUT access, but there are not enough valid samples in temp buffer).
            t->xfade_retrig.temp_buf_valid_samples = min_u32(available, max_needed);

            // then fill up only until temp_buf_valid, but ramp down, so on i = temp_buf_valid, the sample reaches 0
            for (uint32_t i = 0; i < t->xfade_retrig.temp_buf_valid_samples; i++) {
                t->xfade_retrig.temp_buf_l[i] = t->playback_buf.ch[0][start_idx + i];
                t->xfade_retrig.temp_buf_r[i] = t->playback_buf.ch[1][start_idx + i];
            }

            // switch buffers if pending. This has to happen after after copying the xfade buffer and calculating the available samples for xfade
            if (t->switch_bufs_pending) {
                t->playback_buf.valid_samples = t->record_buf.valid_samples;
                swap_tape_buffers(t);
                t->switch_bufs_pending = false;
            }

            t->ph_a.phase = 1 << 16; // reset main play phase

            // ph_b holdes the phase for crossfade buffer
            t->ph_b.phase = 1 << 16; // restart b from beginning of temp_buffer, which now holds the next samples to be crossfaded with
            t->ph_b.active = true;
            t->xfade_retrig.pos = 0;
            t->xfade_retrig.len = FADE_LUT_LEN;
            t->xfade_retrig.active = true;

            t->buffer_end_fade_pos = 0;

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

            t->rec_state = REC_RECORDING;
        }
        break;

    case REC_RECORDING:
        if (evt == TAPE_EVT_RECORD) {
            // new recording while already recording -> just switch buffers and start recording on the other one
            t->record_buf.valid_samples = t->tape_recordhead;
            t->tape_recordhead = 0;
            t->switch_bufs_pending = true;

            // stay in recording state
        } else if (evt == TAPE_EVT_RECORD_DONE || evt == TAPE_EVT_STOP) {
            t->record_buf.valid_samples = t->tape_recordhead;
            t->switch_bufs_pending = true;

            t->rec_state = REC_IDLE;
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
    rec_fsm_event(active_tape_player, TAPE_EVT_STOP);
}

// change pitch factor (playback speed)
// TODO: evaluate where musical pitch is calculated. Probably in control interface task, and converted to simple pitch factor here.
void tape_player_change_pitch(float pitch_factor) {
    if (active_tape_player) {
        active_tape_player->params.pitch_factor = pitch_factor;
    }
}

int tape_player_copy_params(struct parameters* params_out) {
    if (active_tape_player == NULL || params_out == NULL)
        return -1;

    params_out->pitch_factor = active_tape_player->params.pitch_factor;
    params_out->pitch_factor_dirty = false;
    params_out->starting_position = active_tape_player->params.starting_position; // currently not used

    return 0;
}