#pragma once

#include "audioengine.h"
#include "envelope.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAPE_PLAYER_PITCH_RANGE 2.0f // pitch factor range from 0.0 (stop) to 2.0 (double speed)

typedef struct {
    int16_t* ch[2]; // ch[0]=L, ch[1]=R
    uint32_t size;  // samples per channel
} tape_buffer_t;

struct parameters {
    float pitch_factor;
    bool pitch_factor_dirty;
    float starting_position; // currently not used
    // more params will be added according to DSP/feature requirements
};

typedef struct {
    uint32_t phase;
    bool active;
} playhead_t;

struct tape_player {
    size_t dma_buf_size;        // buffer size RX/TX
    tape_buffer_t playback_buf; // holds tape audio - play source and recording
                                // target of the tape
    tape_buffer_t record_buf;   // holds tape audio - play source and recording
                                // target of the tape
    uint32_t tape_playphase;    // Q16.16 (int16_t integer part, uint16_t frac part)
    uint32_t tape_recordhead;

    // corssfading logic
    playhead_t ph_a;
    playhead_t ph_b;
    bool crossfading;
    uint32_t fade_pos; // samples into crossfade
    uint32_t fade_len; // total fade length

    bool is_playing;
    bool is_recording;
    bool copy_pending;

    envelope_t env;

    QueueHandle_t tape_cmd_q; // command queue handle

    struct parameters params;
};

// FREERTOS queue message structure
typedef enum { TAPE_CMD_PLAY, TAPE_CMD_STOP, TAPE_CMD_RECORD } tape_cmd_t;
typedef struct {
    tape_cmd_t cmd;
    float pitch;
} tape_cmd_msg_t;

int init_tape_player(struct tape_player* tape_player, size_t dma_buf_size, QueueHandle_t cmd_queue);

void tape_player_process(struct tape_player* tape, int16_t* dma_in_buf, int16_t* dma_out_buf);

void tape_player_play();
void tape_player_stop();
void tape_player_record();
void tape_player_change_pitch(float pitch_factor);

int tape_player_copy_params(struct parameters* params_out);
