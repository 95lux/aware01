#pragma once

#include "audioengine.h"
#include "envelope.h"
#include "param_cache.h"
#include "project_config.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// TODO: change every use of int16_t to tape_sample_t for better readability and easier bit depth changes in the future
// --- Derived Constants ---
#if (APP_BIT_DEPTH == 8)
typedef int8_t tape_sample_t;
#else
typedef int16_t tape_sample_t;
#endif

typedef struct {
    int16_t* ch[2];         // ch[0]=L, ch[1]=R
    uint32_t size;          // samples per channel
    uint32_t valid_samples; // number of valid recorded samples in the buffer (for playback), updated when recording is done
    uint8_t decimation;     // decimation factor for recording and playback
} tape_buffer_t;

// holds changeable parameters in the tape player engine. should actually not be accessed from outside
struct parameters {
    float pitch_factor;
    float starting_position; // currently not used
    float env_attack;
    float env_decay;

    // playback modes
    bool reverse;
    bool cyclic_mode;

    uint8_t decimation; // decimation factor for recording and playback.
    // more params will be added according to DSP/feature requirements

    float grit; // calculated from decimation factor, used for excite effect amount in audio processing task. 0..1 depending on decimation.
};

// FSM logic
typedef enum { TAPE_EVT_NONE = 0, TAPE_EVT_PLAY, TAPE_EVT_STOP, TAPE_EVT_RECORD, TAPE_EVT_RECORD_DONE, TAPE_EVT_SWAP_DONE } tape_event_t;

typedef enum { PLAY_STOPPED = 0, PLAY_PLAYING } play_state_t;

typedef enum { REC_IDLE = 0, REC_RECORDING, REC_SWAP_PENDING, REC_DONE } rec_state_t;

typedef struct {
    bool active;
    uint32_t len; // TODO make xfade length configurable. Need to implement fade lookup interpolation then (or use bigger LUT?).
    // TODO: this has to be max xfade length. Cant be super long unfortunately, since we dont have much SRAM available anymore :(
    bool
        crossfade; // if crossfade is enabled, we are using temp_buf. Otherwise this struct just holds the position and length for simple in/out fades.

    // tracking the second playhead for crossfade
    uint64_t pos_q48_16; // 48.16 fixed point: upper 48 bits are integer index, lower 16 bits are fractional part

    // 16.16 fixed point fade accumulator for fade curve interpolation, independent of main playhead position and increment, so that we can have smooth fades even with low pitch factors (where main playhead moves very slowly and thus would cause stepping in the fade curve)
    // this is just the phase accumulator for the lut access.
    uint32_t fade_acc_q16;
    uint16_t fade_step_q16; // how much to increment the fade_acc every sample, in Q16.16 format. Calculated from fade length and LUT size.
    uint32_t
        base_ratio_q16; // for crossfades, this is the ratio between the xfade buffer and the fade LUT, in Q16.16 format. Calculated from xfade length and LUT size.

    int16_t* buf_b_ptr_l;
    int16_t* buf_b_ptr_r;
    uint32_t
        temp_buf_valid_samples; // number of valid samples in the xfade temp buffer (for retrigger xfade at end of recording, when there might be less than xfade.len samples available)
} crossfade_t;

// main tape player structure
struct tape_player {
    size_t dma_buf_size;         // buffer size RX/TX
    tape_buffer_t* playback_buf; // holds tape audio - play source and recording
                                 // target of the tape
    tape_buffer_t* record_buf;   // holds tape audio - play source and recording
                                 // target of the tape

    // playheads, use 2 for crossfading with same buffer on retrigger/cycling - Q16.16 (int16_t integer part, uint16_t frac part)
    // playhead_t ph_a;  // main playhead for playback
    uint64_t pos_q48_16; // main playhead position in Q48.16 format. Fractional part is 16bits for performance reasons
    // playhead_t ph_b;

    // TODO: make wrap mode configurable: cyclic or oneshot
    bool cyclic_mode;

    crossfade_t xfade_retrig; // crossfade that is used on cyclic mode repeat or retriggering sample playback.
    crossfade_t xfade_cyclic; // crossfade that is used on cyclic mode repeat or retriggering sample playback.
    crossfade_t fade_in;      // simple fade in when starting playback
    crossfade_t fade_out;     // simple fadeout when approaching end of playback buffer

    uint32_t tape_recordhead;

    bool switch_bufs_pending;
    bool switch_bufs_done;

    uint32_t curr_phase_inc_q16_16;   // The increment actually being used
    uint32_t target_phase_inc_q16_16; // The increment we want to reach, used for smooth pitch transitions

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
void tape_player_set_pitch(float pitch_factor);
void tape_player_set_params(struct param_cache param_cache);
float tape_player_get_grit(struct tape_player* tape);

float tape_player_get_pitch();