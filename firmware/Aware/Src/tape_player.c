/**
 * @file tape_player.c
 * @brief Tape player — buffer management, FSMs, init, and public control API.
 *
 * Owns the tape buffer memory, the @c tape_player state struct, and the two
 * finite state machines (play FSM, record FSM) that govern state transitions.
 * Per-sample DSP (interpolation, fades, crossfades) lives in tape_player_dsp.c,
 * which accesses @c tape_player via an @c extern declaration.
 *
 * @see tape_player_dsp.c  for the audio-rate processing core.
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

/** Shared tape player state — also accessed by tape_player_dsp.c via extern. */
struct tape_player tape_player;

/**
 * @brief Initialize the tape player engine.
 *
 * Clears both tape buffers, assigns channel pointers, resets the playhead,
 * initialises all crossfade and fade structures, and sets default parameters.
 * Must be called once before any other tape_player function.
 *
 * @param dma_buf_size  Number of samples in the DMA transfer buffer.
 * @param cmd_queue     FreeRTOS queue handle used to receive tape commands.
 * @return              0 on success, -1 if @p dma_buf_size is invalid.
 */
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

/**
 * @brief Recompute the grit factor from the current playback buffer decimation.
 *
 * Maps decimation (1–16) to a 0–1 grit value on a logarithmic curve so that
 * the lo-fi hold-sample blend is perceptually uniform. The result is stored in
 * @c tape_player.params.grit for use by the DSP exciter.
 */
static void compute_grit() {
    uint8_t dec = tape_player.playback_buf->decimation;
    if (dec < 1)
        dec = 1;

#define MAX_DECIMATION 16

    float g = (float) (dec - 1) / (float) (MAX_DECIMATION - 1); // 0..1

    // power curve - mixes linear and quardratic
    // tape_player.params.grit = g * 0.6f + g * g * 0.4f;
    // logarithmic curve: fast start, slow rise at the end
    tape_player.params.grit = logf(1.0f + 9.0f * g) / logf(10.0f); // maps 0..1 -> 0..1
}

/**
 * @brief Swap the record and playback buffer pointers.
 *
 * Makes the just-recorded buffer the active playback source and recycles the
 * old playback buffer for the next recording pass. Also resets the record head
 * and recomputes the grit factor for the new buffer's decimation setting.
 */
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

/**
 * @brief Reset all slice markers in a tape buffer.
 *
 * Sets every entry in @c buf->slice_positions to 0 and resets @c buf->num_slices.
 * Called before each new recording pass so stale markers from a previous take
 * cannot be played back.
 *
 * @param buf  Tape buffer whose slice table should be cleared.
 */
static void tape_clear_slices(tape_buffer_t* buf) {
    // clear slice positions and reset slice index
    for (int i = 0; i < MAX_NUM_SLICES; i++) {
        buf->slice_positions[i] = 0;
    }
    buf->num_slices = 0;
}

/**
 * @brief Resolve the normalized slice parameter to a Q48.16 buffer position.
 *
 * Selects a slice index proportional to @c tape_player.params.slice_pos (0–1)
 * and converts the stored sample offset to a Q48.16 value. Enforces the minimum
 * index of 1 required by the Hermite interpolator and ensures at least 4 samples
 * remain after the slice start.
 *
 * @param[out] out_pos  Q48.16 playhead position corresponding to the chosen slice.
 * @return              0 on success, -1 if the slice is out of range or too close
 *                      to the end of the buffer.
 */
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

    *out_pos = (uint64_t) slice_pos << 16;
    return 0;
}

// prototypes for FSM event handling
static void play_fsm_event(tape_event_t evt);
static void rec_fsm_event(tape_event_t evt);

/**
 * @brief Dispatch an event to the playback finite state machine.
 *
 * States: @c PLAY_STOPPED → @c PLAY_PLAYING → @c PLAY_STOPPED
 *
 * - **STOPPED + PLAY**: swaps buffers if pending, initialises the playhead at
 *   the selected slice (or end of buffer in reverse), arms the fade-in and
 *   envelope, and transitions to @c PLAY_PLAYING.
 * - **PLAYING + PLAY** (retrigger): captures the current playback region into
 *   the crossfade temp buffer, optionally swaps buffers, repositions the main
 *   playhead, and starts a retrigger crossfade so the transition is click-free.
 * - **PLAYING + STOP**: immediately transitions to @c PLAY_STOPPED.
 *
 * @param evt  Event to process (@c TAPE_EVT_PLAY or @c TAPE_EVT_STOP).
 */
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

/**
 * @brief Finalise the record buffer after recording stops.
 *
 * Saves the current record head position as @c valid_samples, resets the record
 * head, and sets the @c swap_bufs_pending flag so the next play event will
 * promote the new recording to the playback buffer.
 *
 * Called before the buffer swap — must happen while recording has already stopped.
 */
static inline void finalize_rec_buf() {
    tape_player.record_buf->valid_samples = tape_player.tape_recordhead;
    tape_player.tape_recordhead = 0;
    tape_player.swap_bufs_pending = true;
}

/**
 * @brief Prepare the record buffer for the next recording pass.
 *
 * Sets the decimation factor (from params or fixed compile-time value), clears
 * all slice markers, and seeds the first slice at index 1 (Hermite lower bound).
 *
 * Called after the buffer swap — must happen while the record buffer is idle and
 * not yet written to.
 */
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

/**
 * @brief Dispatch an event to the recording finite state machine.
 *
 * States: @c REC_IDLE → @c REC_RECORDING → @c REC_DONE → @c REC_IDLE
 *                                        ↘ @c REC_REREC → @c REC_RECORDING
 *
 * - **IDLE + RECORD**: prepares the record buffer and starts recording.
 * - **RECORDING + RECORD** (re-record): finalises the current buffer and enters
 *   @c REC_REREC, waiting for the buffer swap before starting the next pass.
 * - **RECORDING + RECORD_DONE / STOP**: finalises the buffer and moves to
 *   @c REC_DONE, waiting for the swap signal.
 * - **DONE + SWAP_DONE**: swap complete, returns to @c REC_IDLE.
 * - **REREC + SWAP_DONE**: swap complete, prepares the buffer and resumes recording.
 *
 * @param evt  Event to process (see @c tape_event_t).
 */
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

/** @brief Start or retrigger tape playback. Thread-safe via FSM event dispatch. */
void tape_player_play(void) {
    play_fsm_event(TAPE_EVT_PLAY);
}

/** @brief Stop tape playback immediately. */
void tape_player_stop_play(void) {
    play_fsm_event(TAPE_EVT_STOP);
}

/** @brief Start recording, or re-record over the current tape if already recording. */
void tape_player_record(void) {
    rec_fsm_event(TAPE_EVT_RECORD);
}

/** @brief Stop recording and finalise the tape buffer. */
void tape_player_stop_record(void) {
    rec_fsm_event(TAPE_EVT_RECORD_DONE);
}

/**
 * @brief Set the playback pitch factor directly.
 *
 * A value of 1.0 plays at the recorded speed; 0.5 plays at half speed (one octave
 * down); 2.0 plays at double speed (one octave up).
 *
 * @param pitch_factor  Linear speed ratio relative to the recorded sample rate.
 */
void tape_player_set_pitch(float pitch_factor) {
    tape_player.params.pitch_factor = pitch_factor;
}

/**
 * @brief Mark the current record head position as a new slice boundary.
 *
 * Has no effect outside the @c REC_RECORDING state or when @c MAX_NUM_SLICES
 * has already been reached. Slices are used to jump playback to sub-regions of
 * the tape via @c tape_player.params.slice_pos.
 */
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

/**
 * @brief Update all tape player parameters from the current UI/CV cache.
 *
 * Called once per audio block before @c tape_player_process(). Combines the UI
 * pitch knob and V/Oct CV input multiplicatively to produce the final pitch factor.
 *
 * @param param_cache  Snapshot of all current parameter values.
 */
void tape_player_set_params(struct param_cache param_cache) {
    tape_player.params.pitch_factor = param_cache.pitch_ui * param_cache.pitch_cv;
    tape_player.params.env_attack = param_cache.env_attack;
    tape_player.params.env_decay = param_cache.env_decay;
    tape_player.params.reverse = param_cache.reverse_mode;
    tape_player.params.cyclic_mode = param_cache.cyclic_mode;
    tape_player.params.decimation = param_cache.decimation;
    tape_player.params.slice_pos = param_cache.slice_pos;
}

/** @brief Return the current pitch factor (combined UI knob × V/Oct CV). */
float tape_player_get_pitch() {
    return tape_player.params.pitch_factor;
}

/**
 * @brief Return the grit factor for the current playback buffer (0–1).
 *
 * Grit is derived from the buffer's decimation factor via a logarithmic curve
 * (see @c compute_grit()). A value of 0 means full Hermite interpolation
 * (smooth, high-fidelity); a value of 1 blends in zero-order hold maximally,
 * producing a lo-fi, grainy texture that is characteristic of heavily decimated
 * tape.
 *
 * @return  Grit blend factor in the range [0, 1].
 */
float tape_player_get_grit() {
    return tape_player.params.grit;
}
