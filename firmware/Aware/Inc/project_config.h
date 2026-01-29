#pragma once

/* ===== Config Options ===== */
// #define CONFIG_AUDIO_LOOPBACK

/* ===== Engine Parameters ===== */

/* audio engine config */
#define AUDIO_BLOCK_SIZE 32
#define AUDIO_SAMPLE_RATE I2S_AUDIOFREQ_48K

/* tape engine configs*/
#define TAPE_SECONDS 5
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
// TODO: add more potentiometer mappings as needed

/* ======= MEMORY SECTIONS ======= */

// define section for DMA buffers. This gets assigned in the linker script.
#if defined(__ICCARM__)
#define DMA_BUFFER _Pragma("location=\".dma_buffer\"")
#else
#define DMA_BUFFER __attribute__((section(".dma_buffer")))
#endif

/* ===== Derived values (do not edit) ===== */
#define TAPE_SIZE (AUDIO_SAMPLE_RATE * TAPE_SECONDS * NUM_CHANNELS) // number of samples
#define TAPE_SIZE_ALIGNED ((TAPE_SIZE / AUDIO_BLOCK_SIZE) * AUDIO_BLOCK_SIZE)
#define TAPE_SIZE_CHANNEL (TAPE_SIZE_ALIGNED / NUM_CHANNELS) // number of samples per channel