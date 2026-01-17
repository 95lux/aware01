#include "tape_player.h"

#include <stddef.h>
#include <stdbool.h>
#include <arm_math.h>
#include <stdint.h>
#include "project_config.h"

static int16_t tape_buffer_l[TAPE_SIZE_ALIGNED] __attribute__((section(".sram1")));
static int16_t tape_buffer_r[TAPE_SIZE_ALIGNED] __attribute__((section(".sram1")));

int init_tape_player(struct audioengine_tape* tape_player,
                     volatile int16_t* dma_in_buf,
                     volatile int16_t* dma_out_buf,
                     size_t dma_buf_size) {
    if (tape_player == NULL || dma_in_buf == NULL || dma_out_buf == NULL || dma_buf_size <= 0)
        return -1;

    tape_player->dma_in_buf = dma_in_buf;
    tape_player->dma_out_buf = dma_out_buf;
    tape_player->dma_buf_size = dma_buf_size;
    tape_player->tape_buf.ch[0] = tape_buffer_l;
    tape_player->tape_buf.ch[1] = tape_buffer_r;
    tape_player->tape_buf.size = TAPE_SIZE_ALIGNED;
    tape_player->tape_playhead = 0;
    tape_player->tape_recordhead = 0;
    tape_player->is_playing = false;
    tape_player->is_recording = false;
    tape_player->pitch_factor = 1.0f; // TODO: read out pitch fader on init?

    return 0;
}

// worker function to process tape player state
void tape_player_process(struct audioengine_tape* tape) {
    // TODO: implement circular tape buffer (?) - for now, just stop at the end of the buffer
    if (!tape || !tape->tape_buf.ch[0] || !tape->tape_buf.ch[1])
        return;

    // process half of the block
    for (uint32_t n = 0; n < (tape->dma_buf_size / 2) - 1; n += 2) {
        if (tape->is_playing) {
            // copy 2 samples at once (left and right)
            // interpolation may give even better results
            // round samples without interpolation
            // if tape has played all the way, stop playback for now.
            if ((uint32_t) tape->tape_playhead >= tape->tape_buf.size) {
                tape->is_playing = false;
                tape->tape_playhead = 0;
            }

            tape->dma_out_buf[n] = tape->tape_buf.ch[0][(uint32_t) tape->tape_playhead];
            tape->dma_out_buf[n + 1] = tape->tape_buf.ch[1][(uint32_t) tape->tape_playhead];

            tape->tape_playhead += tape->pitch_factor;
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