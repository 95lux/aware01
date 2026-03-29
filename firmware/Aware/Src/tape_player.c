/**
 * @file tape_player.c
 * @brief Tape player — buffer management, FSMs, init, and public control API.
 */
#include "tape_player.h"

#include "envelope.h"
#include "project_config.h"
#include "string.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "drivers/swo_log.h"
#include "param_cache.h"
#include "ressources.h"
#include "util.h"

// #define DECIMATION_FIXED 16 // fixed decimation factor for testing, will be set from params in record state machine once working.

static tape_buffer_t tape_buf_a;
static tape_buffer_t tape_buf_b;

static int16_t tape_play_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_play_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

static int16_t tape_rec_buf_l[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};
static int16_t tape_rec_buf_r[TAPE_SIZE_CHANNEL] __attribute__((section(".sram1"))) = {0};

// +3 for hermite interpolation safety
// TODO: make sure this is long enough once configurable!
int16_t xfade_retrig_temp_buf_l[FADE_XFADE_RETRIG_LEN + 3];
int16_t xfade_retrig_temp_buf_r[FADE_XFADE_RETRIG_LEN + 3]; // +3 for hermite interpolation safety

int16_t xfade_cyclic_temp_buf_l[FADE_XFADE_CYCLIC_LEN + 3];
int16_t xfade_cyclic_temp_buf_r[FADE_XFADE_CYCLIC_LEN + 3]; // +3 for hermite interpolation safety

// Shared tape player state — also accessed by tape_player_dsp.c via extern.
struct tape_player tape_player;

int init_tape_player(size_t dma_buf_size, QueueHandle_t cmd_queue) {
    if (dma_buf_size <= 0)
        return -1;

    // clear tape buffers on init
    memset(tape_play_buf_l, 0, sizeof(tape_play_buf_l));
    memset(tape_play_buf_r, 0, sizeof(tape_play_buf_r));

    // debug addresses for reading out tape buffer via SWD
    volatile uintptr_t tape_l_addr_dbg = (uintptr_t) tape_play_buf_l;
    volatile uintptr_t tape_r_addr_dbg = (uintptr_t) tape_play_buf_r;

    // buffer assignments
    tape_player.playback_buf = &tape_buf_a;
    tape_player.record_buf = &tape_buf_b;
    tape_player.dma_buf_size = dma_buf_size;
    tape_player.playback_buf->ch[0] = tape_play_buf_l;
    tape_player.playback_buf->ch[1] = tape_play_buf_r;
    tape_player.playback_buf->size = TAPE_SIZE_CHANNEL;
    tape_player.playback_buf->valid_samples = 0;
    tape_player.record_buf->ch[0] = tape_rec_buf_l;
    tape_player.record_buf->ch[1] = tape_rec_buf_r;
    tape_player.record_buf->size = TAPE_SIZE_CHANNEL;
    tape_player.record_buf->valid_samples = 0;

    // playhead and reacordhead init
    tape_player.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
    tape_player.swap_bufs_pending = false;

    tape_player.xfade_retrig.buf_b_ptr_l = xfade_retrig_temp_buf_l;
    tape_player.xfade_retrig.buf_b_ptr_r = xfade_retrig_temp_buf_r;
    tape_player.xfade_retrig.len = FADE_XFADE_RETRIG_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player.xfade_retrig.active = false;
    tape_player.xfade_retrig.temp_buf_valid_samples = 0;
    tape_player.xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
    tape_player.xfade_retrig.step_q16 = FADE_XFADE_RETRIG_STEP_Q16;

    tape_player.xfade_cyclic.buf_b_ptr_l = xfade_cyclic_temp_buf_l;
    tape_player.xfade_cyclic.buf_b_ptr_r = xfade_cyclic_temp_buf_r;
    tape_player.xfade_cyclic.len = FADE_XFADE_CYCLIC_LEN; // crossfade length in samples TODO: make configurable via MACRO
    tape_player.xfade_cyclic.active = false;
    tape_player.xfade_cyclic.temp_buf_valid_samples = 0;
    tape_player.xfade_cyclic.pos_q48_16 = 1 << 16; // start at sample 1 for interpolation
    tape_player.xfade_cyclic.step_q16 = FADE_XFADE_CYCLIC_STEP_Q16;

    tape_player.fade_in.buf_b_ptr_l = NULL;     // not used for simple fade in/out, only for crossfades
    tape_player.fade_in.buf_b_ptr_r = NULL;     // not used for simple fade in/out, only for crossfades
    tape_player.fade_in.len = FADE_IN_OUT_LEN;  // fade length in samples TODO: make configurable via MACRO
    tape_player.fade_in.pos_q48_16 = 1 << 16;   // start at sample 1 for interpolation
    tape_player.fade_out.buf_b_ptr_l = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player.fade_out.buf_b_ptr_r = NULL;    // not used for simple fade in/out, only for crossfades
    tape_player.fade_out.len = FADE_IN_OUT_LEN; // fade length in samples
    tape_player.fade_out.pos_q48_16 = 1 << 16;  // start at sample 1 for interpolation
    tape_player.tape_recordhead = 0;

    // state logic
    tape_player.play_state = PLAY_STOPPED;
    tape_player.rec_state = REC_IDLE;

    // envelope init
    tape_player.env.state = ENV_IDLE;
    tape_player.env.value = 0.0f;
    tape_player.env.attack_inc = 1 / (0.001f * AUDIO_SAMPLE_RATE);
    tape_player.env.decay_inc = 1 / (0.5f * AUDIO_SAMPLE_RATE);
    tape_player.env.sustain = 0.0f;

    // parameters
    tape_player.params.pitch_factor = 1.0f;
    tape_player.params.env_attack = 0.0f; // normalized env values
    tape_player.params.env_decay = 0.2f;  // normalized env values

    tape_player.params.reverse = false;     // default to forward playback
    tape_player.params.cyclic_mode = false; // default to oneshot mode

    // init cmd
    tape_player.tape_cmd_q = cmd_queue;

    return 0;
}

// Maps decimation factor (1–16) to a 0–1 grit value on a logarithmic curve,
// so the lo-fi hold-sample blend feels perceptually uniform across the knob range.
static void compute_grit() {
    uint8_t dec = tape_player.playback_buf->decimation;
    if (dec < 1)
        dec = 1;

#define MAX_DECIMATION 16

    float g = (float) (dec - 1) / (float) (MAX_DECIMATION - 1); // 0..1

    // power curve - mixes linear and quardratic
    // tape_player.params.grit = g * 0.6f + g * g * 0.4f;
    // logarithmic curve: fast start, slow rise at the end
    // log10(1 + 9*g): +1 prevents log(0); *9 scales so log10(10)=1 at g=1; dividing by log10(10) converts natural log to base-10
    tape_player.params.grit = logf(1.0f + 9.0f * g) / logf(10.0f); // maps 0..1 -> 0..1
}

// Swap record/playback buffer pointers. The just-recorded buffer becomes the
// active playback source; the old playback buffer is recycled for the next pass.
static void swap_tape_buffers() {
    // switch recorded buffer to playback buffer
    tape_buffer_t* temp = tape_player.playback_buf;
    tape_player.playback_buf = tape_player.record_buf;
    tape_player.record_buf = temp;

    tape_player.tape_recordhead = 0;
    tape_player.swap_bufs_pending = false;

    // now that the new playback buffer holds the recorded audio at the respective decimation factor,
    // compute grit factor from this and save as parameter to be used in other dsp related functions.
    compute_grit();
}

// Clear all slice markers in buf. Called before each new recording pass so stale
// markers from a previous take cannot be played back.
static void tape_clear_slices(tape_buffer_t* buf) {
    // clear slice positions and reset slice index
    for (int i = 0; i < MAX_NUM_SLICES; i++) {
        buf->slice_positions[i] = 0;
    }
    buf->num_slices = 0;
}

// Resolve the normalized slice_pos parameter (0–1) to a Q48.16 playhead position.
// Minimum index 1 is enforced because Hermite reads sample[idx-1..idx+2].
// Returns -1 if fewer than 4 samples remain after the chosen slice (interpolation guard).
static int tape_buf_get_slice_start_pos_q48_16(uint64_t* out_pos) {
    uint32_t slice_idx = (uint32_t) (tape_player.params.slice_pos * (tape_player.playback_buf->num_slices - 1));
    uint32_t slice_pos = tape_player.playback_buf->slice_positions[slice_idx];

    if (slice_pos < 1) {
        slice_pos = 1;
    }

    if (slice_pos >= tape_player.playback_buf->valid_samples) {
        return -1;
    }

    uint32_t available = tape_player.playback_buf->valid_samples - slice_pos;
    if (available < 4) {
        return -1;
    }

    *out_pos = (uint64_t) slice_pos << 16; // convert integer sample index to Q48.16 (fractional part = 0)
    return 0;
}

// prototypes for FSM event handling
static void play_fsm_event(tape_event_t evt);
static void rec_fsm_event(tape_event_t evt);

// Playback FSM. States: PLAY_STOPPED <-> PLAY_PLAYING
//
// STOPPED + PLAY:  swap buffers if pending, position playhead at selected slice
//                  (or buffer end for reverse), arm fade-in and envelope, go PLAYING.
// PLAYING + PLAY:  retrigger — capture current region into crossfade temp buffer,
//                  optionally swap buffers, reposition playhead, start retrigger xfade.
// PLAYING + STOP:  immediately go STOPPED.
static void play_fsm_event(tape_event_t evt) {
    switch (tape_player.play_state) {
    case PLAY_STOPPED:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- PLAY FROM IDLE ----- */

            // each time a play event is triggered, switch buffers if pending.
            if (tape_player.swap_bufs_pending) {
                swap_tape_buffers();
                // only play can really prepare for next record.
                rec_fsm_event(TAPE_EVT_SWAP_DONE);
            }

            if (tape_player.playback_buf->valid_samples < 4) {
                // not enough audio recorded to play, ignore play command
                return;
            }

            if (!tape_player.params.reverse) {
                // aquire playback starting position depending on current set slice
                tape_buf_get_slice_start_pos_q48_16(&tape_player.pos_q48_16);
            } else {
                // if reverse, start at the end of the buffer, minus 4 samples for hermite safety.
                // TODO: for now ignore slices when reverse. This has to be implemented with thought.
                // what to do with the slices?
                tape_player.pos_q48_16 = ((uint64_t) (tape_player.playback_buf->valid_samples - 1)) << 16;
            }

            // Init Fade In
            tape_player.fade_in.pos_q48_16 = 0;
            tape_player.fade_in.active = true;

            // Ensure Fade Out is clean
            tape_player.fade_out.active = false;
            tape_player.fade_out.pos_q48_16 = 0; // this is not needed. Set anyway.
            tape_player.fade_out.fade_acc_q16 = 0;

            envelope_set_attack_norm(&tape_player.env, tape_player.params.env_attack);
            envelope_set_decay_norm(&tape_player.env, tape_player.params.env_decay);
            envelope_note_on(&tape_player.env);

            tape_player.play_state = PLAY_PLAYING;
        }
        break;

    case PLAY_PLAYING:
        if (evt == TAPE_EVT_PLAY) {
            /* ----- RETRIGGER PLAY ----- */

            // copy upcoming xfade.len samples starting at current phase
            // we need to start from one sample before the current phase for hermite interpolation
            uint32_t start_idx_xfade = (uint32_t) (tape_player.pos_q48_16 >> 16) - 1;
            uint32_t max_needed_xfade = tape_player.xfade_retrig.len + 3; // +3 for hermite interpolation safety (idx-1..idx+2)

            // number of samples available for crossfade starting from start_idx
            uint32_t available = tape_player.playback_buf->valid_samples - start_idx_xfade;

            // if available samples are less than max_needed, we have to adjust crossfade length and fade LUT access accordingly, otherwise we might read out of bounds of the playback buffer (if start_idx + max_needed exceeds valid_samples), or the fade LUT (if we use max_needed as xfade.len for LUT access, but there are not enough valid samples in temp buffer).
            tape_player.xfade_retrig.temp_buf_valid_samples = min_u32(available, max_needed_xfade);

            // switch buffers if pending. This has to happen after after copying the xfade buffer and calculating the available samples for xfade
            // buf_b_ptr holds the current playback buffer. On xfade it has to start at full volume and fade out.
            if (tape_player.swap_bufs_pending) { // new record buffer waiting to be switched to playback buffer
                // use static temp buffer for fade, because playback buffer has now switched
                // fill up only until temp_buf_valid, but ramp down, so on i = temp_buf_valid, the sample reaches 0
                tape_player.xfade_retrig.buf_b_ptr_l = xfade_retrig_temp_buf_l;
                tape_player.xfade_retrig.buf_b_ptr_r = xfade_retrig_temp_buf_r;
                for (uint32_t i = 0; i < tape_player.xfade_retrig.temp_buf_valid_samples; i++) {
                    tape_player.xfade_retrig.buf_b_ptr_l[i] = tape_player.playback_buf->ch[0][start_idx_xfade + i];
                    tape_player.xfade_retrig.buf_b_ptr_r[i] = tape_player.playback_buf->ch[1][start_idx_xfade + i];
                }
                swap_tape_buffers();
                rec_fsm_event(TAPE_EVT_SWAP_DONE);
            } else {
                // if no buffer switch pending, we can also just point the xfade buffer to the playback buffer, starting at the current phase. This saves us from copying the xfade buffer every time.
                tape_player.xfade_retrig.buf_b_ptr_l = &tape_player.playback_buf->ch[0][start_idx_xfade];
                tape_player.xfade_retrig.buf_b_ptr_r = &tape_player.playback_buf->ch[1][start_idx_xfade];
            }

            if (!tape_player.params.reverse) {
                // aquire playback starting position depending on current set slice
                tape_buf_get_slice_start_pos_q48_16(&tape_player.pos_q48_16);
            } else {
                // if reverse, start at the end of the buffer, minus 4 samples for hermite safety.
                // TODO: for now ignore slices when reverse. This has to be implemented with thought.
                // what to do with the slices?
                tape_player.pos_q48_16 = ((uint64_t) (tape_player.playback_buf->valid_samples - 1)) << 16;
            }

            // ph_b holdes the phase for crossfade buffer
            tape_player.xfade_retrig.pos_q48_16 = 1 << 16; // start at sample 1
            tape_player.xfade_retrig.fade_acc_q16 = 0;
            tape_player.xfade_retrig.active = true;
            tape_player.xfade_retrig.len = FADE_XFADE_RETRIG_LEN;

            tape_player.fade_in.active = true;

            // uint32_t base_ratio_q16 = ((uint32_t) (FADE_LUT_LEN - 1) << 16) / (tape_player.xfade_retrig.temp_buf_valid_samples - 1);
            // tape_player.xfade_retrig.base_ratio_q16 = base_ratio_q16;

            envelope_set_attack_norm(&tape_player.env, tape_player.params.env_attack);
            envelope_set_decay_norm(&tape_player.env, tape_player.params.env_decay);
            envelope_note_on(&tape_player.env);

        } else if (evt == TAPE_EVT_STOP) {
            // envelope_note_off(&tape_player.env);
            tape_player.play_state = PLAY_STOPPED;
        }
        break;
    }
}

// Save valid_samples and set swap_bufs_pending. Called while recording has already stopped,
// before the buffer swap.
static inline void finalize_rec_buf() {
    tape_player.record_buf->valid_samples = tape_player.tape_recordhead;
    tape_player.tape_recordhead = 0;
    tape_player.swap_bufs_pending = true;
}

// Set decimation, clear slices, and seed slice[0]=1 (Hermite lower bound).
// Called after the buffer swap, while the record buffer is idle.
static inline void prepare_next_rec_buf() {
    // apply decimation to recording buffer. This will be reach over to playback buffer by buffer swapping
    // prepare next rec buffer
#ifdef DECIMATION_FIXED
    tape_player.record_buf->decimation = DECIMATION_FIXED;
#else
    tape_player.record_buf->decimation = tape_player.params.decimation;
#endif

    tape_clear_slices(tape_player.record_buf);
    tape_player.record_buf->slice_positions[0] = 1; // always start at 1 for Hermite
    tape_player.record_buf->num_slices = 1;
}

// Recording FSM. States: REC_IDLE -> REC_RECORDING -> REC_DONE -> REC_IDLE
//                                               \-> REC_REREC -> REC_RECORDING
//
// IDLE + RECORD:           prepare buffer, start recording.
// RECORDING + RECORD:      re-record — finalise current buffer, wait for swap (REC_REREC).
// RECORDING + RECORD_DONE: finalise buffer, wait for swap (REC_DONE).
// DONE + SWAP_DONE:        swap complete, back to IDLE.
// REREC + SWAP_DONE:       swap complete, prepare buffer, resume recording.
static void rec_fsm_event(tape_event_t evt) {
    switch (tape_player.rec_state) {
    case REC_IDLE:
        if (evt == TAPE_EVT_RECORD) {
            prepare_next_rec_buf();

            tape_player.rec_state = REC_RECORDING;
        }
        break;

    case REC_RECORDING:
        if (evt == TAPE_EVT_RECORD) {
            // new recording while already recording -> just switch buffers and start recording on the other one
            finalize_rec_buf();

            // pend wait for buffer swap
            tape_player.rec_state = REC_REREC;
        } else if (evt == TAPE_EVT_RECORD_DONE || evt == TAPE_EVT_STOP) {
            finalize_rec_buf();

            tape_player.rec_state = REC_DONE;
        }
        break;
    case REC_DONE:
        // wait for buffer so be swapped before allowing to record again.
        if (evt == TAPE_EVT_SWAP_DONE) {
            tape_player.rec_state = REC_IDLE;
        }
        break;
    case REC_REREC:
        // wait for swap, than jump into next record
        if (evt == TAPE_EVT_SWAP_DONE) {
            prepare_next_rec_buf();

            tape_player.rec_state = REC_RECORDING;
        }
        break;
    }
}

/* ----- PUBLIC API ----- */

void tape_player_play(void) {
    play_fsm_event(TAPE_EVT_PLAY);
}

void tape_player_stop_play(void) {
    play_fsm_event(TAPE_EVT_STOP);
}

void tape_player_record(void) {
    rec_fsm_event(TAPE_EVT_RECORD);
}

void tape_player_stop_record(void) {
    rec_fsm_event(TAPE_EVT_RECORD_DONE);
}

void tape_player_set_pitch(float pitch_factor) {
    tape_player.params.pitch_factor = pitch_factor;
}

void tape_player_set_slice() {
    if (tape_player.rec_state == REC_RECORDING) {
        uint32_t current_rec_pos = tape_player.tape_recordhead;
        uint32_t num_slices = tape_player.record_buf->num_slices;
        if (num_slices < MAX_NUM_SLICES) {
            tape_player.record_buf->slice_positions[num_slices] = current_rec_pos;
            tape_player.record_buf->num_slices++;
        }
    }
}

// pitch_ui * pitch_cv: UI knob and V/Oct CV combine multiplicatively.
void tape_player_set_params(struct param_cache param_cache) {
    tape_player.params.pitch_factor = param_cache.pitch_ui * param_cache.pitch_cv;
    tape_player.params.env_attack = param_cache.env_attack;
    tape_player.params.env_decay = param_cache.env_decay;
    tape_player.params.reverse = param_cache.reverse_mode;
    tape_player.params.cyclic_mode = param_cache.cyclic_mode;
    tape_player.params.decimation = param_cache.decimation;
    tape_player.params.slice_pos = param_cache.slice_pos;
}

float tape_player_get_pitch() {
    return tape_player.params.pitch_factor;
}

// 0 = full Hermite interpolation (smooth); 1 = maximum zero-order hold blend (lo-fi, grainy).
// Derived from the buffer's decimation factor via compute_grit().
float tape_player_get_grit() {
    return tape_player.params.grit;
}
