#include "audioengine.h"
#include "dma.h"

bool dma_dataReady = false;
DMA_BUFFER int16_t dacData[BUFFER_SIZE];
volatile int16_t *outBufPtr = &dacData[0];

#define SINE_TABLE_SIZE 256 // Adjust the table size as needed for your accuracy/performance tradeoff

// Precomputed sine wave table
int16_t sineTable[SINE_TABLE_SIZE];

// Populate the sine wave table during initialization
void initSineTable() {
    for (int i = 0; i < SINE_TABLE_SIZE; ++i) {
        sineTable[i] = (int16_t)(32767 * sin(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
}

void HAL_I2S_TxHalfCpltCallback(I2S_HandleTypeDef *hi2s){
    outBufPtr = &dacData[0]; // set file read Buffer Pointer
    dma_dataReady = true;
}


void HAL_I2S_TxCpltCallback(I2S_HandleTypeDef *hi2s){
    outBufPtr = &dacData[HALF_BUFFER_SIZE]; // set read Buffer Pointer to second half
    dma_dataReady = true;
}

void generateSineWave(double frequency) {

    uint8_t rxbufferdummy[BUFFER_SIZE];

    static uint16_t phaseIndex = 0;                 /**< Retains the index between function calls */
    double phaseIncrement = frequency * SINE_TABLE_SIZE / I2S_AUDIOFREQ_48K;  
    HAL_Delay(1000);

    if(HAL_I2S_Transmit_DMA(&hi2s1, (void *)dacData, BUFFER_SIZE) != HAL_OK){
        Error_Handler();
    }
    // if(HAL_I2S_Receive_DMA(&hi2s1, (void *)rxbufferdummy, BUFFER_SIZE) != HAL_OK){
    //     Error_Handler();
    // }

    // HAL_I2S_DMAResume(&hi2s1);

    // if(HAL_DMA_Start_IT(hi2s1.hdmatx, (uint32_t)dacData, (uint32_t)&SPI1->TXDR, BUFFER_SIZE) != HAL_OK){
    //     Error_Handler();
    // }

    volatile HAL_DMA_StateTypeDef txState = HAL_DMA_GetState(hi2s1.hdmatx);
    
    while(1){
        if(dma_dataReady){
            for (uint8_t n = 0; n < (HALF_BUFFER_SIZE) - 1; n += 2) {
                // Lookup sine value from table
                // left+right
                outBufPtr[n] = sineTable[phaseIndex];
                outBufPtr[n+1] = sineTable[phaseIndex];

                
                // Increment phase index
                phaseIndex += phaseIncrement;
                
                // Wrap phase index if it exceeds table size
                if (phaseIndex >= SINE_TABLE_SIZE) {
                    phaseIndex -= SINE_TABLE_SIZE;
                }
            }
            dma_dataReady = false;   
        }
    }
 }