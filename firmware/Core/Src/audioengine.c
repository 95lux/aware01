#include "audioengine.h"
#include "dma.h"

bool dma_out_dataReady = false;
DMA_BUFFER int16_t tx_buf[BUFFER_SIZE];
volatile int16_t *tx_buf_ptr = &tx_buf[0];

bool dma_dataReady = false;
DMA_BUFFER int16_t rx_buf[BUFFER_SIZE];
volatile int16_t *rx_buf_ptr = &rx_buf[0];

#define SINE_TABLE_SIZE 256 // Adjust the table size as needed for accuracy/performance tradeoff

// Precomputed sine wave table
int16_t sineTable[SINE_TABLE_SIZE];

// Populate the sine wave table during initialization
void initSineTable()
{
    for (int i = 0; i < SINE_TABLE_SIZE; ++i)
    {
        sineTable[i] = (int16_t)(32767 * sin(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
}

void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef *hi2s)
{
    // First half of TX and RX buffers completed
    tx_buf_ptr = &tx_buf[0];
    rx_buf_ptr = &rx_buf[0];
    dma_dataReady = true;
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef *hi2s)
{
    // Second half of TX and RX buffers completed
    tx_buf_ptr = &tx_buf[HALF_BUFFER_SIZE];
    rx_buf_ptr = &rx_buf[HALF_BUFFER_SIZE];
    dma_dataReady = true;
}

void loopback_samples(){
    for (uint8_t n = 0; n < (HALF_BUFFER_SIZE)-1; n += 2)
    {
        // loopback adc data to dac
        tx_buf_ptr[n] = rx_buf_ptr[n];
        tx_buf_ptr[n+1] = rx_buf_ptr[n + 1];
    }
}

void receiveTest()
{
    double freq = 1000;
    static uint16_t phaseIndex = 0; /**< Retains the index between function calls */
    double phaseIncrement = freq * SINE_TABLE_SIZE / I2S_AUDIOFREQ_48K;

    if (HAL_I2SEx_TransmitReceive_DMA(&hi2s1, (void *)tx_buf, (void *)rx_buf, BUFFER_SIZE) != HAL_OK)
    {
        Error_Handler();
    }

    while (1)
    {
        if (dma_dataReady)
        {
            loopback_samples();
            // generateSineWave(&phaseIndex, phaseIncrement);
            dma_dataReady = false;
        }
    }
}


void generateSineWave(uint16_t *phaseIndex, double phaseIncrement)
{
    for (uint8_t n = 0; n < (HALF_BUFFER_SIZE)-1; n += 2)
    {
        // Lookup sine value from table
        // left+right
        tx_buf_ptr[n] = sineTable[*phaseIndex];
        tx_buf_ptr[n + 1] = sineTable[*phaseIndex];

        // Increment phase index
        *phaseIndex += phaseIncrement;

        // Wrap phase index if it exceeds table size
        if (*phaseIndex >= SINE_TABLE_SIZE)
        {
            *phaseIndex -= SINE_TABLE_SIZE;
        }
    }
}