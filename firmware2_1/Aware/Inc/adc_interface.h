
#include "stm32h7xx_hal.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include "project_config.h"

struct adc_config {
    int16_t* adc_cv_buf_ptr;
    int16_t* adc_pot_buf_ptr;

    TaskHandle_t userIfTaskHandle;
    TaskHandle_t controlIfTaskHandle;

    ADC_HandleTypeDef* hadc_pots;
    ADC_HandleTypeDef* hadc_cvs;
};

/* notification bits for ADC sources */
#define ADC_NOTIFY_CV (1U << 0)
#define ADC_NOTIFY_POTS (1U << 1)

int init_adc_interface(struct adc_config* config);
int start_adc_interface(void);