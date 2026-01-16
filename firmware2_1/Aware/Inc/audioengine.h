#ifndef INC_AUDIO_H_
#define INC_AUDIO_H_

#include <stdbool.h>
#include <math.h>
#include <stdint.h>
#include <sys/_intsup.h>
#include "i2s.h"
#include "FreeRTOS.h"
#include "semphr.h"
#include "project_config.h"

// return types
#define AUDIOENGINE_OK 0
#define AUDIOENGINE_ERROR -1
#define AUDIOENGINE_NOT_INITIALIZED -2

struct audioengine_config {
    I2S_HandleTypeDef* i2s_handle;
    uint32_t sample_rate;
    uint16_t buffer_size;

    SemaphoreHandle_t dma_ready_sem;
    volatile int16_t* tx_buf_ptr;
    volatile int16_t* rx_buf_ptr;
};

int init_audioengine(struct audioengine_config* config);
int start_audio_engine(void);

// Test Functions
void generateSineWave(uint16_t* phaseIndex, double phaseIncrement);
void receiveTest();
void initSineTable();

#endif /* INC_AUDIO_H_ */