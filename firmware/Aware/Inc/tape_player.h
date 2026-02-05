#pragma once

#include "audioengine.h"
#include "envelope.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define TAPE_PLAYER_PITCH_RANGE 2.0f // pitch factor range from 0.0 (stop) to 2.0 (double speed)

typedef struct {
    int16_t* ch[2];          // ch[0]=L, ch[1]=R
    uint32_t size;           // samples per channel
    uint32_t filled_samples; // number of valid recorded samples in the buffer (for playback), updated when recording is done
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

// FSM logic
typedef enum {
    TAPE_EVT_NONE = 0,
    TAPE_EVT_PLAY,
    TAPE_EVT_STOP,
    TAPE_EVT_RECORD,
    TAPE_EVT_RECORD_DONE,
} tape_event_t;

typedef enum {
    PLAY_STOPPED = 0,
    PLAY_PLAYING,
} play_state_t;

typedef enum {
    REC_IDLE = 0,
    REC_RECORDING,
} rec_state_t;

typedef enum {
    XFADE_NONE = 0,
    XFADE_RETRIGGER,
    XFADE_CYCLIC_WRAP,
    XFADE_BUFFER_END,
} xfade_reason_t;

typedef struct {
    bool active;
    xfade_reason_t reason;
    uint32_t pos;
    uint32_t len; // TODO make xfade length configurable. Need to implement fade lookup interpolation then (or use bigger LUT?).
    // TODO: this has to be max xfade length. Cant be super long unfortunately, since we dont have much SRAM available anymore :(
    int16_t temp_buf_l[128 + 3]; // +3 for hermite interpolation safety TODO: make sure this is long enough once configurable!
    int16_t temp_buf_r[128 + 3]; // +3 for hermite interpolation safety
} crossfade_t;

// main tape player structure
struct tape_player {
    size_t dma_buf_size;        // buffer size RX/TX
    tape_buffer_t playback_buf; // holds tape audio - play source and recording
                                // target of the tape
    tape_buffer_t record_buf;   // holds tape audio - play source and recording
                                // target of the tape

    // playheads, use 2 for crossfading with same buffer on retrigger/cycling - Q16.16 (int16_t integer part, uint16_t frac part)
    playhead_t ph_a;
    playhead_t ph_b;

    // TODO: make wrap mode configurable: cyclic or oneshot
    bool cyclic_mode;

    crossfade_t xfade;

    uint32_t tape_recordhead;

    bool switch_bufs_pending;

    // states
    play_state_t play_state;
    rec_state_t rec_state;

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
void tape_player_stop_play();
void tape_player_record();
void tape_player_stop_record(void);
void tape_player_change_pitch(float pitch_factor);

int tape_player_copy_params(struct parameters* params_out);
