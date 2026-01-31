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

/* ===== Global config structs =====*/
struct gpio_config gpio_cfg;
struct adc_config adc_interface_cfg;

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
        adc_interface_cfg.adc_cv_buf_ptr = NULL;
        adc_interface_cfg.adc_pot_buf_ptr = NULL;
        adc_interface_cfg.controlIfTaskHandle = controlIfTaskHandle;
        adc_interface_cfg.userIfTaskHandle = userIfTaskHandle;
        adc_interface_cfg.hadc_pots = &hadc1;
        adc_interface_cfg.hadc_cvs = &hadc2;

        init_adc_interface(&adc_interface_cfg);
        start_adc_interface();

        gpio_cfg.controlIfTaskHandle = controlIfTaskHandle;
        gpio_cfg.userIfTaskHandle = userIfTaskHandle;
        gpio_cfg.tape_cmd_q = tape_cmd_q;

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
    init_tape_player(&tape_player, audioengine_cfg.buffer_size, tape_cmd_q);

    start_audio_engine();

    for (;;) {
        /* wait for DMA signal */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef CONFIG_AUDIO_LOOPBACK
        loopback_samples();
#else

        /* process audio block */
        tape_player_process(&tape_player, (int16_t*) audioengine_cfg.rx_buf_ptr, (int16_t*) audioengine_cfg.tx_buf_ptr);

        /* handle pending commands (non-blocking) */
        tape_cmd_msg_t msg;
        while (xQueueReceive(tape_cmd_q, &msg, 0) == pdTRUE) {
            switch (msg.cmd) {
            case TAPE_CMD_PLAY:
                tape_player_play();
                break;
            case TAPE_CMD_STOP:
                tape_player_stop();
                break;
            case TAPE_CMD_RECORD:
                tape_player_record();
                break;
            }
        }
        struct parameters params;
        /* consume all pending parameter updates and apply latest pitch */
        while (xQueueReceive(params_queue, &params, 0) == pdTRUE) {
            if (params.pitch_factor_dirty)
                tape_player_change_pitch(params.pitch_factor);
            // TODO: This will come later
            // if(params.starting_position_dirty)
            // tape_player_set_position(params.starting_position);
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

#if CONFIG_USE_CALIB_STORAGE
    int b_read = read_calibration_data(&calib_data);
    if (b_read != sizeof(struct calibration_data)) {
#endif
        // set default/fallback calibration data
        // C1/1V -> 44000 ADC value, C3/3V -> 25150 ADC value
        // C1 = 12. semitones, C3 = 36. semitones
        float c3 = float_value(25150);
        float c1 = float_value(44000);
        float delta_semitones = 36.0f - 12.0f;
        float delta = c3 - c1; // normaled CV difference between C1 and C3

        if (delta > -0.5f && delta < -0.0f) {
            calib_data.pitch_scale = delta_semitones / (c3 - c1);
            calib_data.pitch_offset = 12.0f - calib_data.pitch_scale * c1; // to calculate offset, either C1 or C3 can be used
        }

        for (size_t i = 1; i < NUM_CV_CHANNELS; ++i) {
            calib_data.offset[i] = 0.0f;
        }
#if CONFIG_USE_CALIB_STORAGE
    }
#endif

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
            // TODO: How to handle simultaneous parameter updates from user interface and CVs?
            // can i check if jack is plugged in(probably not) ? Maybe calculate offset,
            // then add CV and pot values together ? if (notified & ADC_NOTIFY_CV_RDY) {
            int res = adc_copy_cv_to_working_buf(control_interface_cfg.adc_cv_working_buf, NUM_CV_CHANNELS);
            if (res != 0) {
                // skip processing if error
                continue;
            };
            tape_player_copy_params(&params);
            control_interface_process(&params);
            xQueueOverwrite(params_queue, &params);
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
    struct parameters params;

    for (;;) {
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            if (notified & ADC_NOTIFY_POTS_RDY) {
                // adc_copy_pots_to_working_buf(user_interface_cfg.adc_pot_working_buf, NUM_POT_CHANNELS);
                // tape_player_copy_params(&params);
                // user_interface_process(&params);
                // xQueueOverwrite(params_queue, &params);
            }
        }
    }
}