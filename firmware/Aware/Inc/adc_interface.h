
#include "stm32h7xx_hal.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include "project_config.h"
#include <stdint.h>

struct adc_config {
    uint16_t* adc_cv_buf_ptr;
    uint16_t* adc_pot_buf_ptr;

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

int adc_copy_cv_to_working_buf(uint16_t* working_buf, size_t len);
int adc_copy_pots_to_working_buf(uint16_t* working_buf, size_t len);

static inline float float_value(uint16_t val) {
    return (float) val / 65536.0f;
}