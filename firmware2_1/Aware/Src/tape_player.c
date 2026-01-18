#include "tape_player.h"

#include <stddef.h>
#include <stdbool.h>
#include <arm_math.h>
#include <stdint.h>
#include "project_config.h"

static int16_t tape_buffer_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1")));
static int16_t tape_buffer_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1")));

static struct audioengine_tape* active_tape_player;

int init_tape_player(struct audioengine_tape* tape_player,
                     volatile int16_t* dma_in_buf,
                     volatile int16_t* dma_out_buf,
                     size_t dma_buf_size,
                     QueueHandle_t cmd_queue) {
    if (tape_player == NULL || dma_in_buf == NULL || dma_out_buf == NULL || dma_buf_size <= 0)
        return -1;

    tape_player->dma_in_buf = dma_in_buf;
    tape_player->dma_out_buf = dma_out_buf;
    tape_player->dma_buf_size = dma_buf_size;
    tape_player->tape_buf.ch[0] = tape_buffer_l;
    tape_player->tape_buf.ch[1] = tape_buffer_r;
    tape_player->tape_buf.size = TAPE_SIZE_CHANNEL;
    tape_player->tape_playphase = 1 << 16; // start at sample 1 for interpolation
    tape_player->tape_recordhead = 0;
    tape_player->is_playing = false;
    tape_player->is_recording = false;
    tape_player->pitch_factor = 1.0f; // TODO: read out pitch fader on init?

    // init cmd
    tape_player->tape_cmd_q = cmd_queue;

    active_tape_player = tape_player;

    return 0;
}

// from https://www.musicdsp.org/en/latest/Other/93-hermite-interpollation.html?utm_source=chatgpt.com
// explained here https://ldesoras.fr/doc/articles/resampler-en.pdf
// uses phase accumulator
float hermite_interpolate(uint32_t phase, int16_t* buffer) {
    uint32_t idx = phase >> 16; // integer part
    float frac = (phase & 0xFFFF) * (1.0f / 65536.0f);

    int n = (int) idx - 1;

    float xm1 = buffer[n];
    float x0 = buffer[n + 1];
    float x1 = buffer[n + 2];
    float x2 = buffer[n + 3];

    float c = 0.5f * (x1 - xm1);
    float v = x0 - x1;
    float w = c + v;
    float a = w + v + 0.5f * (x2 - x0);
    float b = w + a;

    return (((a * frac - b) * frac + c) * frac + x0);
}

// worker function to process tape player state
void tape_player_process(struct audioengine_tape* tape) {
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

            tape->dma_out_buf[n] = hermite_interpolate(tape->tape_playphase, tape->tape_buf.ch[0]);
            tape->dma_out_buf[n + 1] = hermite_interpolate(tape->tape_playphase, tape->tape_buf.ch[1]);

            uint32_t phase_inc = tape->pitch_factor * 65536.0f;
            tape->tape_playphase += phase_inc;
        } else {
            // idle -> output silence
            tape->dma_out_buf[n] = 0;
            tape->dma_out_buf[n + 1] = 0;
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
            tape->tape_buf.ch[0][tape->tape_recordhead] = tape->dma_in_buf[n];
            tape->tape_buf.ch[1][tape->tape_recordhead] = tape->dma_in_buf[n + 1];
            tape->tape_recordhead++;
        }
    }
}

BaseType_t tape_player_send_cmd_from_isr(const tape_cmd_msg_t* msg, BaseType_t* pxHigherPriorityTaskWoken) {
    if (active_tape_player->tape_cmd_q == NULL)
        return pdFALSE;
    return xQueueSendFromISR(active_tape_player->tape_cmd_q, msg, pxHigherPriorityTaskWoken);
}

void tape_player_play(struct audioengine_tape* tape_player) {
    if (tape_player) {
        tape_player->is_playing = true;
    }
}
void tape_player_record(struct audioengine_tape* tape_player) {
    if (tape_player) {
        tape_player->is_recording = true;
    }
}

void tape_player_change_pitch(struct audioengine_tape* tape_player, float pitch_factor) {
    if (tape_player) {
        tape_player->pitch_factor = pitch_factor;
    }
}