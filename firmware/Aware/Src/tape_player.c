#include "tape_player.h"

#include "project_config.h"
#include "string.h"
#include <arm_math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/swo_log.h"
#include "ressources.h"

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static struct tape_player* active_tape_player;

#define FADE_IN_LENGTH 128
float envelope[FADE_IN_LENGTH]; // simple fade in envelope to prevent clicks

int init_tape_player(struct tape_player* tape_player, size_t dma_buf_size, QueueHandle_t cmd_queue) {
    if (tape_player == NULL || dma_buf_size <= 0)
        return -1;

    // clear tape buffers on init
    memset(tape_play_buf_l, 0, sizeof(tape_play_buf_l));
    memset(tape_play_buf_r, 0, sizeof(tape_play_buf_r));

    // debug addresses for reading out tape buffer via SWD
    volatile uintptr_t tape_l_addr_dbg = (uintptr_t) tape_play_buf_l;
    volatile uintptr_t tape_r_addr_dbg = (uintptr_t) tape_play_buf_r;

    // precompute fade-in envelope
    for (int i = 0; i < FADE_IN_LENGTH; i++) {
        envelope[i] = sinf((float) i / FADE_IN_LENGTH * (M_PI / 2)); // fade-in
    }

    tape_player->dma_buf_size = dma_buf_size;
    tape_player->playback_buf.ch[0] = tape_play_buf_l;
    tape_player->playback_buf.ch[1] = tape_play_buf_r;
    tape_player->playback_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->record_buf.ch[0] = tape_rec_buf_l;
    tape_player->record_buf.ch[1] = tape_rec_buf_r;
    tape_player->record_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->ph_a.phase = 1 << 16; // start at sample 1 for interpolation
    tape_player->ph_b.phase = 1 << 16; // start at sample 1 for interpolation
    tape_player->fade_len = 128;       // crossfade length in samples TODO: make configurable via MACRO

    tape_player->tape_recordhead = 0;

    tape_player->env.state = ENV_IDLE;
    tape_player->env.value = 0.0f;
    tape_player->env.attack_inc = 1 / (0.01f * AUDIO_SAMPLE_RATE);
    tape_player->env.decay_inc = 1 / (0.2f * AUDIO_SAMPLE_RATE);
    tape_player->env.sustain = 0.0f;

    // parameters
    tape_player->is_playing = false;
    tape_player->is_recording = false;
    tape_player->copy_pending = false;
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
    uint32_t idx = ph->phase >> 16; // integer part
    if (idx >= active_tape_player->playback_buf.size) {
        idx %= active_tape_player->playback_buf.size;   // wrap
        ph->phase = (idx << 16) | (ph->phase & 0xFFFF); // keep fractional
    }
}

// TODO: prevent clicks on retrigger / loop end by crossfading playback.
// Approach: on retrigger or near loop end, briefly run two playheads
// (old + new) and overlap them using complementary fade-out / fade-in
// envelopes (equal-power). After fade completes, discard old playhead.
// This avoids waveform discontinuities without copying tape data.

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

        if (tape->is_playing && tape->ph_a.active) {
            if (!tape->crossfading) {
                out_l += hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[0]);
                out_r += hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[1]);
            } else {
                // --- dual playhead crossfade ---
                int16_t a_l = hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[0]);
                int16_t a_r = hermite_interpolate(tape->ph_a.phase, tape->playback_buf.ch[1]);

                int16_t b_l = hermite_interpolate(tape->ph_b.phase, tape->playback_buf.ch[0]);
                int16_t b_r = hermite_interpolate(tape->ph_b.phase, tape->playback_buf.ch[1]);

                uint32_t i = tape->fade_pos;
                int16_t fa = fade_in_lut[tape->fade_len - 1 - i];
                int16_t fb = fade_in_lut[i];
                // Q15 * Q15 -> Q30, then sum, then >>15 back to Q15
                int32_t acc_l = (int32_t) fa * (int32_t) a_l + (int32_t) fb * (int32_t) b_l;
                int32_t acc_r = (int32_t) fa * (int32_t) a_r + (int32_t) fb * (int32_t) b_r;

                // back to int16 Q15
                out_l = __SSAT(acc_l >> 15, 16);
                out_r = __SSAT(acc_r >> 15, 16);

                tape->fade_pos++;

                if (tape->fade_pos >= tape->fade_len) {
                    // crossfade done → promote B to A
                    tape->ph_a = tape->ph_b;
                    tape->ph_b.active = false;
                    tape->crossfading = false;
                }
            }
            advance_playhead(&tape->ph_a, phase_inc);
            if (tape->crossfading)
                advance_playhead(&tape->ph_b, phase_inc);
        }

        float env_val = envelope_process(&tape->env);
        dma_out_buf[n] = (int16_t) out_l * env_val;
        dma_out_buf[n + 1] = (int16_t) out_r * env_val;

        // while recording, pitch factor does not matter at all.
        // EVAL: maybe allow recording at pitch factor? could lead to interesting artifacts.
        if (tape->is_recording) {
            // record tape at current recordhead position
            // if tape has recorded all the way, stop recording for now.
            if (tape->tape_recordhead >= tape->playback_buf.size) {
                tape->is_recording = false;
                tape->copy_pending = true;
                tape->tape_recordhead = 0;
            }
            // deinterleaves input buffer into tape buffer
            tape->record_buf.ch[0][tape->tape_recordhead] = dma_in_buf[n];
            tape->record_buf.ch[1][tape->tape_recordhead] = dma_in_buf[n + 1];
            tape->tape_recordhead++;
        }
    }
}

void tape_player_play() {
    if (active_tape_player) {
        if (active_tape_player->copy_pending) {
            // switch recorded buffer to playback buffer
            tape_buffer_t temp = active_tape_player->playback_buf;
            active_tape_player->playback_buf = active_tape_player->record_buf;
            active_tape_player->record_buf = temp;

            // avoid clearing by now. Could be expensive for large tape buffers
            // // clear rec buffers after switching pointers
            // memset(active_tape_player->record_buf.ch[0], 0, sizeof(int16_t) * active_tape_player->record_buf.size);
            // memset(active_tape_player->record_buf.ch[1], 0, sizeof(int16_t) * active_tape_player->record_buf.size);
            active_tape_player->copy_pending = false;
        }

        envelope_note_on(&active_tape_player->env);

        // reset 2nd playhead for crossfading
        active_tape_player->ph_b.active = false;
        active_tape_player->crossfading = false;
        active_tape_player->fade_pos = 0;

        // if retrigger was detected, start crossfade
        if (active_tape_player->is_playing) {
            active_tape_player->ph_b.phase = 1 << 16; // new playhead start
            active_tape_player->ph_b.active = true;

            active_tape_player->fade_pos = 0;
            active_tape_player->fade_len = 128; // ~2.6 ms @ 48 kHz
            active_tape_player->crossfading = true;
        }

        active_tape_player->is_playing = true;
    }
}
void tape_player_stop() {
    if (active_tape_player) {
        active_tape_player->tape_playphase = 1 << 16;
        active_tape_player->is_playing = false;
    }
}

void tape_player_record() {
    // TODO: there is a high pitched noise when starting recording.
    // check why. maybe add in recording fade-in
    if (active_tape_player) {
        active_tape_player->tape_recordhead = 0;

        // handle retrigger recording
        if (active_tape_player->is_recording) {
            // memcpy(tape_play_buf_l, tape_rec_buf_l, TAPE_SIZE_CHANNEL * sizeof(int16_t));
            // memcpy(tape_play_buf_r, tape_rec_buf_r, TAPE_SIZE_CHANNEL * sizeof(int16_t));
            active_tape_player->copy_pending = true;
        }

        active_tape_player->is_recording = true;
    }
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