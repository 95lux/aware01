#include "stm32h7xx_hal.h"
#include "adc.h"
#include "FreeRTOS.h"
#include "task.h"

#include "settings.h"
#include "project_config.h"

struct control_interface_config {
    uint16_t adc_cv_working_buf[NUM_CV_CHANNELS];

    TaskHandle_t userIfTaskHandle;

    ADC_HandleTypeDef* hadc_cvs;

    struct calibration_data* calib_data;
    float cv_c1;
};

int init_control_interface(struct control_interface_config* config, struct calibration_data* calib_data);
int start_control_interface();
int process_cv_samples();
int control_interface_start_calibration(struct calibration_data* calib_data);