#include "tape_player.h"

#include "project_config.h"
#include <arm_math.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

static int16_t tape_buffer_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_buffer_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static struct tape_player* active_tape_player;

#define FADE_IN_LENGTH 128
float envelope[FADE_IN_LENGTH]; // simple fade in envelope to prevent clicks

int init_tape_player(struct tape_player* tape_player, size_t dma_buf_size, QueueHandle_t cmd_queue) {
    if (tape_player == NULL || dma_buf_size <= 0)
        return -1;

    // clear tape buffers on init
    memset(tape_buffer_l, 0, sizeof(tape_buffer_l));
    memset(tape_buffer_r, 0, sizeof(tape_buffer_r));

    // precompute fade-in envelope
    for (int i = 0; i < FADE_IN_LENGTH; i++) {
        envelope[i] = sinf((float) i / FADE_IN_LENGTH * (M_PI / 2)); // fade-in
    }

    tape_player->dma_buf_size = dma_buf_size;
    tape_player->tape_buf.ch[0] = tape_buffer_l;
    tape_player->tape_buf.ch[1] = tape_buffer_r;
    tape_player->tape_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->tape_playphase = 1 << 16; // start at sample 1 for interpolation
    tape_player->tape_recordhead = 0;

    // parameters
    tape_player->is_playing = false;
    tape_player->is_recording = false;
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

// TODO: prevent clicks on retrigger / loop end by crossfading playback.
// Approach: on retrigger or near loop end, briefly run two playheads
// (old + new) and overlap them using complementary fade-out / fade-in
// envelopes (equal-power). After fade completes, discard old playhead.
// This avoids waveform discontinuities without copying tape data.

// worker function to process tape player state
void tape_player_process(struct tape_player* tape, int16_t* dma_in_buf, int16_t* dma_out_buf) {
    // TODO: implement circular tape buffer (?) - for now, just stop at the end of the buffer
    if (!tape || !tape->tape_buf.ch[0] || !tape->tape_buf.ch[1])
        return;

    // process half of the block
    for (uint32_t n = 0; n < (tape->dma_buf_size / 2) - 1; n += 2) {
        if (tape->is_playing) {
            // if tape has played all the way, stop playback for now.
            uint32_t idx = tape->tape_playphase >> 16;
            if (idx < 1 || idx + 2 >= tape->tape_buf.size) {
                // stop playback or clamp
                tape->is_playing = false;
                tape->tape_playphase = 1 << 16; // safe start
                continue;
            }

            dma_out_buf[n] = hermite_interpolate(tape->tape_playphase, tape->tape_buf.ch[0]);
            dma_out_buf[n + 1] = hermite_interpolate(tape->tape_playphase, tape->tape_buf.ch[1]);
            // dma_out_buf[n] = tape->tape_buf.ch[0][idx];
            // dma_out_buf[n + 1] = tape->tape_buf.ch[1][idx];

            if (idx < FADE_IN_LENGTH) {
                // apply fade-in envelope to prevent clicks
                float env = envelope[idx];
                dma_out_buf[n] = (int16_t) (dma_out_buf[n] * env);
                dma_out_buf[n + 1] = (int16_t) (dma_out_buf[n + 1] * env);
            }

            uint32_t phase_inc = tape->params.pitch_factor * 65536.0f;
            tape->tape_playphase += phase_inc;
        } else {
            // idle -> output silence
            dma_out_buf[n] = 0;
            dma_out_buf[n + 1] = 0;
        }

        // while recording, pitch factor does not matter at all.
        // EVAL: maybe allow recording at pitch factor? could lead to interesting artifacts.
        if (tape->is_recording) {
            // record tape at current recordhead position
            // if tape has recorded all the way, stop recording for now.
            if (tape->tape_recordhead >= tape->tape_buf.size) {
                tape->is_recording = false;
                tape->tape_recordhead = 0;
            }
            // deinterleaves input buffer into tape buffer
            tape->tape_buf.ch[0][tape->tape_recordhead] = dma_in_buf[n];
            tape->tape_buf.ch[1][tape->tape_recordhead] = dma_in_buf[n + 1];
            tape->tape_recordhead++;
        }
    }
}

void tape_player_play() {
    if (active_tape_player) {
        active_tape_player->tape_playphase = 1 << 16;
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
    if (active_tape_player) {
        active_tape_player->tape_recordhead = 0;
        // clear tape buffers on init
        memset(tape_buffer_l, 0, sizeof(tape_buffer_l));
        memset(tape_buffer_r, 0, sizeof(tape_buffer_r));

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