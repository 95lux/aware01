#include "control_interface.h"

#include <FreeRTOS.h>
#include <queue.h>
#include "project_config.h"
#include "stm32h7xx_hal.h"
#include "main.h"
#include "string.h"

#include "tape_player.h"

// local DMA buffers for adc cv channels and potentiometer channels - will be allocated in DMA-capable memory, not in FREERTOS task stack!
DMA_BUFFER static int16_t adc_cv_buf[NUM_CV_CHANNELS];

static struct control_interface_config* active_cfg = NULL;

int init_control_interface(struct control_interface_config* config) {
    if (config == NULL || config->hadc_cvs == NULL)
        return -1;

    active_cfg = config;

    active_cfg->adc_cv_buf_ptr = &adc_cv_buf[0];

    return 0;
}

int start_control_interface() {
    // implement something else later if needed
    return 0;
}

// TODO: implement processing of CV samples
int process_cv_samples() {
    memcpy(active_cfg->adc_cv_working_buf, active_cfg->adc_cv_buf_ptr, sizeof(active_cfg->adc_cv_working_buf));
    return 0;
}
