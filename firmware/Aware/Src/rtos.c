#include "FreeRTOS.h"
#include "queue.h"
#include "task.h"
#include <stdio.h>
#include <string.h>

#include "audioengine.h"
#include "control_interface.h"
#include "drivers/adc_driver.h"
#include "drivers/gpio_driver.h"
#include "drivers/tlv320_driver.h"
#include "main.h"
#include "tape_player.h"
#include "user_interface.h"
#include <stdbool.h>

/* ===== Global FreeRTOS objects ===== */
TaskHandle_t audioTaskHandle;
TaskHandle_t controlIfTaskHandle;
TaskHandle_t userIfTaskHandle;

QueueHandle_t tape_cmd_q;
QueueHandle_t params_queue;

/* ===== Task prototypes ===== */
static void AudioTask(void* argument);
static void ControlInterfaceTask(void* argument);
static void UserInterfaceTask(void* argument);

/* ===== FreeRTOS init ===== */
void FREERTOS_Init(void) {
    /* create command queue */
    tape_cmd_q = xQueueCreate(8, sizeof(tape_cmd_msg_t));
    configASSERT(tape_cmd_q);

    params_queue = xQueueCreate(1, sizeof(struct parameters));
    configASSERT(params_queue);

    /* create audio task (highest priority) */
    xTaskCreate(AudioTask, "Audio", 512, NULL, configMAX_PRIORITIES - 1, &audioTaskHandle);

    /* create control interface task */
    xTaskCreate(ControlInterfaceTask, "ControlIF", 256, NULL, configMAX_PRIORITIES - 3, &controlIfTaskHandle);

    /* create user interface task */
    xTaskCreate(UserInterfaceTask, "UserIF", 256, NULL, configMAX_PRIORITIES - 4, &userIfTaskHandle);

    {
        struct adc_config adc_interface_cfg = {
            .adc_cv_buf_ptr = NULL,
            .adc_pot_buf_ptr = NULL,
            .controlIfTaskHandle = controlIfTaskHandle,
            .userIfTaskHandle = userIfTaskHandle,
            .hadc_pots = &hadc1,
            .hadc_cvs = &hadc2
        };

        init_adc_interface(&adc_interface_cfg);
        start_adc_interface();

        struct gpio_config gpio_cfg = {
            .controlIfTaskHandle = controlIfTaskHandle,
            .userIfTaskHandle = userIfTaskHandle,
        };
        init_gpio_interface(&gpio_cfg);
    }
}

/* ===== Audio task ===== */
static void AudioTask(void* argument) {
    printf("Hello i am the audio task\n");
    (void) argument;

    /* configure codec */
    if (codec_i2c_is_ok() == HAL_OK) {
        codec_init();
    }

    struct audioengine_config audioengine_cfg = {
        .i2s_handle = &hi2s1, .sample_rate = AUDIO_SAMPLE_RATE, .buffer_size = AUDIO_BLOCK_SIZE, .audioTaskHandle = audioTaskHandle};

    struct tape_player tape_player;

    /* initialize audio engine */
    init_audioengine(&audioengine_cfg);
    init_tape_player(&tape_player, audioengine_cfg.rx_buf_ptr, audioengine_cfg.tx_buf_ptr, audioengine_cfg.buffer_size, tape_cmd_q);

    start_audio_engine();

    for (;;) {
        /* wait for DMA signal */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef CONFIG_AUDIO_LOOPBACK
        loopback_samples();
#else

        /* process audio block */
        tape_player_process(&tape_player);

        /* handle pending commands (non-blocking) */
        tape_cmd_msg_t msg;
        while (xQueueReceive(tape_cmd_q, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
            case TAPE_CMD_PLAY:
                tape_player.is_playing = true;
                break;
            case TAPE_CMD_STOP:
                tape_player.is_playing = false;
                tape_player.is_recording = false;
                break;
            case TAPE_CMD_RECORD:
                tape_player.is_recording = true;
                break;
            }
        }
        struct parameters params;
        /* consume all pending parameter updates and apply latest pitch */
        while (xQueueReceive(params_queue, &params, 0) == pdTRUE) {
            tape_player.pitch_factor = params.v_oct;
        }
#endif
    }
}

/* ===== Control interface task ===== */
static void ControlInterfaceTask(void* argument) {
    (void) argument;

    struct control_interface_config control_interface_cfg = {
        .userIfTaskHandle = userIfTaskHandle,
        .hadc_cvs = &hadc1,
    };

    struct calibration_data calib_data;

    int b_read = read_calibration_data(&calib_data);
    if (b_read != sizeof(struct calibration_data)) {
        // set default/fallback calibration data
        calib_data.pitch_offset = 0.0f;
        calib_data.pitch_scale = 12.0f;
        for (size_t i = 0; i < NUM_CV_CHANNELS; ++i) {
            calib_data.offset[i] = 0.0f;
        }
    }

    // Init calibration if both buttons are held on startup
    if (HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
        HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET) {
        /* simple debounce / require 500 ms hold */
        vTaskDelay(pdMS_TO_TICKS(500));
        if (HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
            HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET) {
            control_interface_start_calibration(&calib_data);
        }
    }

    init_control_interface(&control_interface_cfg, &calib_data);
    start_control_interface();

    uint32_t notified;

    struct parameters params;

    for (;;) {
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            if (notified & ADC_NOTIFY_CV) {
                int res = adc_copy_cv_to_working_buf(control_interface_cfg.adc_cv_working_buf, NUM_CV_CHANNELS);
                if (res != 0) {
                    // skip processing if error
                    continue;
                };
                process_cv_samples(&params);

                xQueueOverwrite(params_queue, &params);
            }
        }
    }
}

/* ===== User interface task ===== */
static void UserInterfaceTask(void* argument) {
    struct user_interface_config user_interface_cfg = {
        .userIfTaskHandle = userIfTaskHandle,
        .hadc_pots = &hadc2,
    };

    init_user_interface(&user_interface_cfg);
    start_user_interface();

    uint32_t notified;

    for (;;) {
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            if (notified & ADC_NOTIFY_POTS) {
                int res = adc_copy_pots_to_working_buf(user_interface_cfg.adc_pot_working_buf, NUM_POT_CHANNELS);
                // process_pot_samples(user_interface_cfg->adc_pot_buf_ptr);
            }
        }
    }
}
