#include "drivers/adc_driver.h"

#include "main.h"
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "string.h"
#include <FreeRTOS.h>
#include <queue.h>

#include "rtos.h"
#include "tape_player.h"

static struct adc_config adc_config;

// local DMA buffers for adc cv channels and potentiometer channels - will be allocated in DMA-capable memory, not in FREERTOS task stack!
DMA_BUFFER static uint16_t adc_dma_cv_buf[NUM_CV_CHANNELS];
DMA_BUFFER static uint16_t adc_dma_pot_buf[NUM_POT_CHANNELS];

int init_adc_interface(TaskHandle_t controlIfTaskHandle,
                       TaskHandle_t userIfTaskHandle,
                       ADC_HandleTypeDef* hadc_cvs,
                       ADC_HandleTypeDef* hadc_pots) {
    if (hadc_cvs == NULL || hadc_pots == NULL)
        return -1;

    adc_config.controlIfTaskHandle = controlIfTaskHandle;
    adc_config.userIfTaskHandle = userIfTaskHandle;
    adc_config.hadc_pots = hadc_pots;
    adc_config.hadc_cvs = hadc_cvs;

    // assign DMA buffers
    adc_config.adc_cv_buf_ptr = adc_dma_cv_buf;
    adc_config.adc_pot_buf_ptr = adc_dma_pot_buf;
    return 0;
}

int start_adc_interface(void) {
    if (adc_config.hadc_cvs != NULL && adc_config.adc_cv_buf_ptr != NULL) {
        HAL_ADC_Start_DMA(adc_config.hadc_cvs, (uint32_t*) adc_config.adc_cv_buf_ptr, NUM_CV_CHANNELS);
    }
    if (adc_config.hadc_pots != NULL && adc_config.adc_pot_buf_ptr != NULL) {
        HAL_ADC_Start_DMA(adc_config.hadc_pots, (uint32_t*) adc_config.adc_pot_buf_ptr, NUM_POT_CHANNELS);
    }
    return 0;
}

// snapshots latest samples from DMA buffer to working buffer for processing in control or user interface task
int adc_copy_dma_to_working_buf(uint16_t* dma_buf, uint16_t* working_buf, size_t len) {
    if (dma_buf == NULL || working_buf == NULL || len == 0) {
        return -1;
    }
    memcpy(working_buf, dma_buf, len * sizeof(uint16_t));
    return 0;
}

// gets latest CV samples from DMA buffer to working buffer
int adc_copy_cv_to_working_buf(uint16_t* working_buf, size_t len) {
    if (adc_config.adc_cv_buf_ptr == NULL)
        return -1;
    return adc_copy_dma_to_working_buf(adc_config.adc_cv_buf_ptr, working_buf, len);
}

// gets latest potentiometer samples from DMA buffer to working buffer
int adc_copy_pots_to_working_buf(uint16_t* working_buf, size_t len) {
    if (adc_config.adc_pot_buf_ptr == NULL)
        return -1;
    return adc_copy_dma_to_working_buf(adc_config.adc_pot_buf_ptr, working_buf, len);
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    BaseType_t hpw = pdFALSE;

    if (adc_config.hadc_cvs == NULL && adc_config.hadc_pots == NULL) {
        return;
    }

    // Notify control or user interface task about new data
    // Can be retrieved by adc_copy_*_to_working_buf functions
    if (hadc == adc_config.hadc_cvs && adc_config.controlIfTaskHandle != NULL) {
        xTaskNotifyFromISR(adc_config.controlIfTaskHandle, ADC_NOTIFY_CV_RDY, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else if (hadc == adc_config.hadc_pots && adc_config.userIfTaskHandle != NULL) {
        xTaskNotifyFromISR(adc_config.userIfTaskHandle, ADC_NOTIFY_POTS_RDY, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }
}

// Not used currently, but could be implemented for half-complete DMA handling
// void HAL_ADC_ConvHalfCpltCallback(ADC_HandleTypeDef* hadc) {
//     BaseType_t hpw = pdFALSE;

//     if (active_cfg == NULL) {
//         return;
//     }

//     if (hadc == active_cfg->hadc_cvs && active_cfg->controlIfTaskHandle != NULL) {
//         xTaskNotifyFromISR(active_cfg->controlIfTaskHandle, ADC_NOTIFY_CV, eSetBits, &hpw);
//         portYIELD_FROM_ISR(hpw);
//     } else if (hadc == active_cfg->hadc_pots && active_cfg->userIfTaskHandle != NULL) {
//         xTaskNotifyFromISR(active_cfg->userIfTaskHandle, ADC_NOTIFY_POTS, eSetBits, &hpw);
//         portYIELD_FROM_ISR(hpw);
//     }
// }
