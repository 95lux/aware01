#include "adc_interface.h"

#include <FreeRTOS.h>
#include <queue.h>
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "string.h"

#include "tape_player.h"

static struct adc_config* active_cfg = NULL;

int init_adc_interface(struct adc_config* config) {
    if (config == NULL || config->hadc_cvs == NULL || config->hadc_pots == NULL)
        return -1;

    active_cfg = config;
    return 0;
}

int start_adc_interface(void) {
    if (active_cfg == NULL)
        return -1;

    if (active_cfg->hadc_cvs != NULL && active_cfg->adc_cv_buf_ptr != NULL) {
        HAL_ADC_Start_DMA(active_cfg->hadc_cvs, (uint32_t*) active_cfg->adc_cv_buf_ptr, NUM_CV_CHANNELS);
    }
    if (active_cfg->hadc_pots != NULL && active_cfg->adc_pot_buf_ptr != NULL) {
        HAL_ADC_Start_DMA(active_cfg->hadc_pots, (uint32_t*) active_cfg->adc_pot_buf_ptr, NUM_POT_CHANNELS);
    }
    return 0;
}

void HAL_ADC_ConvCpltCallback(ADC_HandleTypeDef* hadc) {
    BaseType_t hpw = pdFALSE;

    if (active_cfg == NULL) {
        return;
    }

    if (hadc == active_cfg->hadc_cvs && active_cfg->controlIfTaskHandle != NULL) {
        xTaskNotifyFromISR(active_cfg->controlIfTaskHandle, ADC_NOTIFY_CV, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    } else if (hadc == active_cfg->hadc_pots && active_cfg->userIfTaskHandle != NULL) {
        xTaskNotifyFromISR(active_cfg->userIfTaskHandle, ADC_NOTIFY_POTS, eSetBits, &hpw);
        portYIELD_FROM_ISR(hpw);
    }

    portYIELD_FROM_ISR(hpw);
}
