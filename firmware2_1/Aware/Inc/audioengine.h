#ifndef INC_AUDIO_H_
#define INC_AUDIO_H_

#include <stdbool.h>
#include <math.h>
#include "i2s.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

// define a section for DMA buffers. This gets assigned in the linker script.
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


// Callbacks for DMA Complete
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s);
void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s);


// Test Functions
void generateSineWave(uint16_t *phaseIndex, double phaseIncrement);
void receiveTest();
void initSineTable();

#endif /* INC_AUDIO_H_ */