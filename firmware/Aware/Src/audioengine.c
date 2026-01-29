#include "audioengine.h"
#include "dma.h"

// local DMA buffers for audio I/O - will be allocated in DMA-capable memory, not in FREERTOS task stack!
DMA_BUFFER static int16_t tx_buf[AUDIO_BLOCK_SIZE] = {0};
DMA_BUFFER static int16_t rx_buf[AUDIO_BLOCK_SIZE] = {0};

// file-local pointer to the active config (not exported)
static struct audioengine_config* active_cfg = NULL;

int init_audioengine(struct audioengine_config* config) {
    if (config == NULL || config->i2s_handle == NULL)
        return AUDIOENGINE_ERROR;
    active_cfg = config;

    active_cfg->tx_buf_ptr = &tx_buf[0];
    active_cfg->rx_buf_ptr = &rx_buf[0];

    return AUDIOENGINE_OK;
}

int start_audio_engine(void) {
    if (active_cfg == NULL || active_cfg->i2s_handle == NULL)
        return AUDIOENGINE_NOT_INITIALIZED;

    HAL_StatusTypeDef hal_status = HAL_I2SEx_TransmitReceive_DMA(
        active_cfg->i2s_handle, (void*) active_cfg->tx_buf_ptr, (void*) active_cfg->rx_buf_ptr, active_cfg->buffer_size);

    if (hal_status != HAL_OK)
        return AUDIOENGINE_ERROR;

    return AUDIOENGINE_OK;
}

// overload HAL I2S DMA Complete and HalfComplete callbacks to handle double buffering
void HAL_I2SEx_TxRxHalfCpltCallback(I2S_HandleTypeDef* i2s_handle) {
    // First half of TX and RX buffers completed
    active_cfg->tx_buf_ptr = &tx_buf[0];
    active_cfg->rx_buf_ptr = &rx_buf[0];

    // signal task from ISR
    BaseType_t hpw = pdFALSE;
    xTaskNotifyFromISR(active_cfg->audioTaskHandle, 1, eSetValueWithOverwrite, &hpw);
    portYIELD_FROM_ISR(hpw);
}

void HAL_I2SEx_TxRxCpltCallback(I2S_HandleTypeDef* i2s_handle) {
    // Second half of TX and RX buffers completed
    active_cfg->tx_buf_ptr = &tx_buf[active_cfg->buffer_size / 2];
    active_cfg->rx_buf_ptr = &rx_buf[active_cfg->buffer_size / 2];

    // signal task from ISR
    BaseType_t hpw = pdFALSE;
    xTaskNotifyFromISR(active_cfg->audioTaskHandle, 1, eSetValueWithOverwrite, &hpw);
    portYIELD_FROM_ISR(hpw);
}

/* TESTFUNCTIONALITIES FROM HERE ON */
void loopback_samples() {
    for (uint8_t n = 0; n < (active_cfg->buffer_size / 2) - 1; n += 2) {
        // loopback adc data to dac
        active_cfg->tx_buf_ptr[n] = active_cfg->rx_buf_ptr[n];
        active_cfg->tx_buf_ptr[n + 1] = active_cfg->rx_buf_ptr[n + 1];
    }
}

#define SINE_TABLE_SIZE 256 // Adjust the table size as needed for accuracy/performance tradeoff

// Precomputed sine wave table
int16_t sineTable[SINE_TABLE_SIZE];

// Populate the sine wave table during initialization
void initSineTable() {
    for (int i = 0; i < SINE_TABLE_SIZE; ++i) {
        sineTable[i] = (int16_t) (32767 * sin(2.0 * M_PI * i / SINE_TABLE_SIZE));
    }
}

void generateSineWave(uint16_t* phaseIndex, double phaseIncrement) {
    for (uint8_t n = 0; n < (active_cfg->buffer_size / 2) - 1; n += 2) {
        // Lookup sine value from table
        // left+right
        active_cfg->tx_buf_ptr[n] = sineTable[*phaseIndex];
        active_cfg->tx_buf_ptr[n + 1] = sineTable[*phaseIndex];

        // Increment phase index
        *phaseIndex += phaseIncrement;

        // Wrap phase index if it exceeds table size
        if (*phaseIndex >= SINE_TABLE_SIZE) {
            *phaseIndex -= SINE_TABLE_SIZE;
        }
    }
}