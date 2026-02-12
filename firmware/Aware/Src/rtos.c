#include "FreeRTOS.h"
#include "queue.h"
#include "settings.h"
#include "stm32h7xx_hal.h"
#include "task.h"
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "audioengine.h"
#include "control_interface.h"
#include "drivers/adc_driver.h"
#include "drivers/gpio_driver.h"
#include "drivers/swo_log.h"
#include "drivers/tlv320_driver.h"
#include "main.h"
#include "param_cache.h"
#include "tape_player.h"
#include "tim.h"
#include "user_interface.h"

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
struct SettingsData settings_data_ram;

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

#if CONFIG_USE_CALIB_STORAGE
        int b_read = read_settings_data(&settings_data);
        if (b_read != sizeof(struct settings_data)) {
#endif
            struct calibration_data calibration = settings_data_ram.calibration_data;
            // set default/fallback calibration data
            // C1/1V -> 44000 ADC value, C3/3V -> 25150 ADC value
            // C1 = 12. semitones, C3 = 36. semitones
            float c3 = float_value(25150);
            float c1 = float_value(44000);
            float delta_semitones = 36.0f - 12.0f;
            float delta = c3 - c1; // normaled CV difference between C1 and C3

            if (delta > -0.5f && delta < -0.0f) {
                calibration.voct_pitch_scale = delta_semitones / (c3 - c1);
                calibration.voct_pitch_offset =
                    12.0f - calibration.voct_pitch_scale * c1; // to calculate offset, either C1 or C3 can be used
            }

            for (size_t i = 1; i < NUM_CV_CHANNELS; ++i) {
                calibration.cv_offset[i] = 0.0f;
            }
#if CONFIG_USE_CALIB_STORAGE
        }
#endif
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
                tape_player_stop_play();
                break;
            case TAPE_CMD_RECORD:
                tape_player_record();
                break;
            }
        }

        // check for param changes
        struct param_cache param_cache;
        uint32_t dirty_flags;

        float next_pitch = tape_player_get_pitch();

        dirty_flags = param_cache_fetch(&param_cache);
        if (dirty_flags & PARAM_DIRTY_PITCH_CV)
            next_pitch = param_cache.pitch_cv;

        if (dirty_flags & PARAM_DIRTY_PITCH_UI)
            next_pitch = next_pitch * param_cache.pitch_ui;

        tape_player_set_pitch(next_pitch);
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

    init_control_interface(&control_interface_cfg, &settings_data_ram.calibration_data);
    start_control_interface();

    uint32_t notified;
    struct parameters params;

    for (;;) {
        // wait for next adc conversion
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            int res = adc_copy_cv_to_working_buf(control_interface_cfg.adc_cv_working_buf, NUM_CV_CHANNELS);
            if (res != 0) {
                // skip processing if samples cant be fetched.
                continue;
            };
            control_interface_process();
        }
    }
}

/* ===== User interface task ===== */
static void UserInterfaceTask(void* argument) {
    struct user_interface_config user_interface_cfg = {
        .userIfTaskHandle = userIfTaskHandle,
        .pots =
            {
                [0] = {.inverted = true},
                [1] = {.inverted = false},
                [2] = {.inverted = false},
                [3] = {.inverted = true},
            },
        .pot_leds =
            {
                [0] = {.htim_led = &htim12, .timer_channel = TIM_CHANNEL_2},
                [1] = {.htim_led = &htim12, .timer_channel = TIM_CHANNEL_1},
                [2] = {.htim_led = &htim1, .timer_channel = TIM_CHANNEL_1},
            },
    };

    // TODO: rewire to different pin which supports pwm output.
    // user_interface_cfg.pot_leds[0].htim_led = &tim12; // this is wrongly assigned currently :(

    struct calibration_data calibration = settings_data_ram.calibration_data;

    uint32_t hold_time = 0;

    // Check both buttons pressed
    while (HAL_GPIO_ReadPin(BUTTON1_IN_GPIO_Port, BUTTON1_IN_Pin) == GPIO_PIN_RESET &&
           HAL_GPIO_ReadPin(BUTTON2_IN_GPIO_Port, BUTTON2_IN_Pin) == GPIO_PIN_RESET) {
        vTaskDelay(pdMS_TO_TICKS(10));
        hold_time += 10;

        if (hold_time >= POT_CALIB_HOLD_MS) {
            // Long hold -> POT calibration
            // led_feedback_pot_calib(); // TODO: calibraiton schemes for WS2812 LEDs
            user_iface_calibrate_pitch_pot(&settings_data_ram.calibration_data);
            write_settings_data(&settings_data_ram);
        }
    }

    if (hold_time >= CV_CALIB_HOLD_MS && hold_time < POT_CALIB_HOLD_MS) {
        // Short hold -> CV calibration
        // led_feedback_cv_calib();
        control_interface_start_calibration(&settings_data_ram.calibration_data);
        write_settings_data(&settings_data_ram);
    }

    user_iface_init(&user_interface_cfg, &calibration);
    user_iface_start();

    uint32_t notified;

    for (;;) {
        // wait for adc conversion
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            if (notified & ADC_NOTIFY_POTS_RDY) {
                int res = adc_copy_pots_to_working_buf(user_interface_cfg.adc_pot_working_buf, NUM_POT_CHANNELS);
                if (res != 0) {
                    // skip processing if adc fetch failed
                    continue;
                }
                user_iface_process();
            }
        }
    }
}