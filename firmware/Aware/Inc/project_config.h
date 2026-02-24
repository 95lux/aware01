#pragma once

#include "drivers/tlv320_driver.h"
#include <math.h>

#define ARM_MATH_CM7

/* ===== Config Options ===== */
// #define CONFIG_AUDIO_LOOPBACK
#define CONFIG_ENABLE_TAPE_PLAYER
#define CONFIG_USE_CALIB_STORAGE
// #define CONFIG_DEBUG_LOGS
#define CONFIG_TAPE_PLAYER_ENABLE_HERMITE

/* ===== Engine Parameters ===== */

/* audio engine config */
#define AUDIO_BLOCK_SIZE 128
#define AUDIO_HALF_BLOCK_SIZE (AUDIO_BLOCK_SIZE / 2)
#define AUDIO_SAMPLE_RATE SAMPLERATE_48KHZ
// #define AUDIO_SAMPLE_RATE SAMPLERATE_24KHZ
// #define AUDIO_SAMPLE_RATE_HW SAMPLERATE_48KHZ
#define AUDIO_SAMPLE_RATE_INTERNAL SAMPLERATE_24KHZ // buffer processing is done at lower sample rate to save ram
#define AUDIO_BIT_DEPTH 16 // or 8 // TODO: maybe make tapebuffer use 8bit. Increase tape lenth and create nice texture?

/* tape engine configs*/
// Length of the rec/playback tape in milliseconds, if samplerate is full 48kHz.
// this length is dynamic, dependent on the decimation factor
#define TAPE_SECONDS_MS 2500
#define NUM_CHANNELS 2 // stereo

// CV Channel configuration
#define NUM_CV_CHANNELS 4
#define NUM_POT_CHANNELS 4
#define ADC_V_OCT_CV 0 // ADC channel index for 1V/oct CV input
#define ADC_CV1 1      // ADC channel index for CV1 input
#define ADC_CV2 2      // ADC channel index for CV2 input
#define ADC_CV3 3      // ADC channel index for CV3 input

// CV buffer indices
#define CV_V_OCT 0
#define CV1 1
#define CV2 2
#define CV3 3

// Potentiometer indeces
#define POT_PITCH 0 // potentiometer index for pitch control
#define POT_PARAM2 1
#define POT_PARAM3 2
#define POT_PARAM4 3

#define NUM_POT_LEDS 3 // TODO: currently only 3, since last LED gpio does not support PWM output :(
// TODO: add more potentiometer mappings as needed

/* ======= MEMORY SECTIONS ======= */

// define section for DMA buffers. This gets assigned in the linker script.
#if defined(__ICCARM__)
#define DMA_BUFFER _Pragma("location=\".dma_buffer\"")
#else
#define DMA_BUFFER __attribute__((section(".dma_buffer")))
#endif

#define MAGIC_NUMBER 0xDEADBEEF

/* ===== Derived values (do not edit) ===== */
// playback tape size calculations
#define TAPE_SIZE (AUDIO_SAMPLE_RATE * (TAPE_SECONDS_MS) * NUM_CHANNELS / 1000) // number of samples
#define TAPE_SIZE_ALIGNED ((TAPE_SIZE / AUDIO_BLOCK_SIZE) * AUDIO_BLOCK_SIZE)   // make sure tape size is multiple of audio block size
#define TAPE_SIZE_CHANNEL (TAPE_SIZE_ALIGNED / NUM_CHANNELS)                    // number of samples per channel

// we have to dimension the recording buffer to handle the playhead advancement at maximum pitch shift
#define MAX_PITCH_SHIFT_SEMITONES 48.0f // maximum pitch shift in semitones (upwards)
// compute number of blocks needed for max pitch shift + 10% safety margin
// approximate 2^(n/12) using fixed values or a table
// for 48 semitones: 2^(48/12) = 16

#define UI_PITCH_MAX_SEMITONE_RANGE 24.0f

#define PITCH_FACTOR_BLOCKS 16
// add 10% safety margin using integer math: 16 + 10% ≈ 17.6 -> round up to 18
#define TAPE_REC_BUF_NUM_BLOCKS ((PITCH_FACTOR_BLOCKS * 110 + 99) / 100) // ceil integer division

// recording buffer size per channel, aligned to block size
#define TAPE_REC_BUF_SIZE_CHANNEL (AUDIO_BLOCK_SIZE * TAPE_REC_BUF_NUM_BLOCKS)

#define FADE_XFADE_RETRIG_LEN 64
#define FADE_XFADE_CYCLIC_LEN 64
#define FADE_IN_OUT_LEN 32 // fade in/out length when approaching start/end of buffer, to prevent clicks
#define FADE_IN_OUT_STEP_Q16 (uint32_t) (((float) FADE_LUT_LEN * 65536.0f) / (float) FADE_IN_OUT_LEN)

#define CV_CALIB_HOLD_MS 1000
#define POT_CALIB_HOLD_MS 5000

#define MAX_GRIT_ON_MAX_DECIMATION 0.7f
#define MAX_EXCITE_ON_MAX_DECIMATION 4.0f