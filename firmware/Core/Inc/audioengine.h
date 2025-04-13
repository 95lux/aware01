#ifndef INC_AUDIO_H_
#define INC_AUDIO_H_

#include <stdbool.h>
#include <math.h>
#include "i2s.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

#if defined( __ICCARM__ )
  #define DMA_BUFFER \
      _Pragma("location=\".dma_buffer\"")
#else
  #define DMA_BUFFER \
      __attribute__((section(".dma_buffer")))
#endif

/**
 * @brief Size of the audio buffer.
 */
#define BUFFER_SIZE 256

/**
 * @brief Size of half of the audio buffer.
 */
#define HALF_BUFFER_SIZE (BUFFER_SIZE / 2)

extern DMA_BUFFER int16_t dacData[BUFFER_SIZE];
extern bool dma_dataReady;
extern volatile int16_t *outBufPtr;

// Callbacks for DMA Complete
void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s);
void HAL_I2SS_TxCpltCallback(I2S_HandleTypeDef *hi2s);


// Test Functions
void generateSineWave(double frequency);
void initSineTable();

#endif /* INC_AUDIO_H_ */