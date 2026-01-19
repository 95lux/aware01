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

#define NUM_CV_CHANNELS 4