#pragma once

#include <math.h>

/* ===== Config Options ===== */
// #define CONFIG_AUDIO_LOOPBACK
// #define CONFIG_USE_CALIB_STORAGE
// #define CONFIG_DEBUG_LOGS

/* ===== Engine Parameters ===== */

/* audio engine config */
#define AUDIO_BLOCK_SIZE 256
// #define AUDIO_SAMPLE_RATE I2S_AUDIOFREQ_48K
#define AUDIO_SAMPLE_RATE 48000

/* tape engine configs*/
#define TAPE_SECONDS 2
#define NUM_CHANNELS 2 // stereo

// CV Channel configuration
#define NUM_CV_CHANNELS 4
#define NUM_POT_CHANNELS 4
#define ADC_V_OCT_CV 0 // ADC channel index for 1V/oct CV input
#define ADC_CV1 1      // ADC channel index for CV1 input
#define ADC_CV2 2      // ADC channel index for CV2 input
#define ADC_CV3 3      // ADC channel index for CV3 input

// Potentiometer configuration
#define POT_PITCH 0 // potentiometer index for pitch control
#define POT_PARAM2 1

#define CV_V_OCT 0
#define CV_1 1
#define CV_2 2
#define CV_3 3

#define NUM_POT_LEDS 3 // TODO: currently only 3, since last LED gpio does not support PWM output :(
// TODO: add more potentiometer mappings as needed

/* ======= MEMORY SECTIONS ======= */

// define section for DMA buffers. This gets assigned in the linker script.
#if defined(__ICCARM__)
#define DMA_BUFFER _Pragma("location=\".dma_buffer\"")
#else
#define DMA_BUFFER __attribute__((section(".dma_buffer")))
#endif

/* ===== Derived values (do not edit) ===== */
// playback tape size calculations
#define TAPE_SIZE (AUDIO_SAMPLE_RATE * TAPE_SECONDS * NUM_CHANNELS)           // number of samples
#define TAPE_SIZE_ALIGNED ((TAPE_SIZE / AUDIO_BLOCK_SIZE) * AUDIO_BLOCK_SIZE) // make sure tape size is multiple of audio block size
#define TAPE_SIZE_CHANNEL (TAPE_SIZE_ALIGNED / NUM_CHANNELS)                  // number of samples per channel

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

#define FADE_LUT_LEN 128
#define FADE_OUT_LENGTH 64 // fade out length when approaching end of buffer, to prevent clicks

#define CV_CALIB_HOLD_MS 1000
#define POT_CALIB_HOLD_MS 3000