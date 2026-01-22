#pragma once

/* audio engine config */
#define AUDIO_BLOCK_SIZE 256
// #define SAMPLE_RATE I2S_AUDIOFREQ_48K
#define SAMPLE_RATE 48000

/* tape engine configs*/
#define TAPE_SECONDS 5
#define NUM_CHANNELS 2 // stereo

#define TAPE_SIZE (SAMPLE_RATE * TAPE_SECONDS * NUM_CHANNELS) // number of samples
#define TAPE_SIZE_ALIGNED ((TAPE_SIZE / AUDIO_BLOCK_SIZE) * AUDIO_BLOCK_SIZE)
#define TAPE_SIZE_CHANNEL (TAPE_SIZE_ALIGNED / NUM_CHANNELS)

// CV Channel configuration
#define NUM_CV_CHANNELS 4
#define NUM_POT_CHANNELS 4
#define ADC_V_OCT_CV 0 // ADC channel index for 1V/oct CV input

/* ======= MEMORY SECTIONS ======= */

// define section for DMA buffers. This gets assigned in the linker script.
#if defined(__ICCARM__)
#define DMA_BUFFER _Pragma("location=\".dma_buffer\"")
#else
#define DMA_BUFFER __attribute__((section(".dma_buffer")))
#endif