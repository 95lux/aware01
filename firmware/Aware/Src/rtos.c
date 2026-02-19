#include "rtos.h"

#include "FreeRTOS.h"
#include "drivers/ws2812_driver.h"
#include "project_config.h"
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
#include "ws2812_animations.h"

/* ===== Global FreeRTOS objects ===== */
TaskHandle_t audioTaskHandle;
TaskHandle_t controlIfTaskHandle;
TaskHandle_t userIfTaskHandle;

QueueHandle_t tape_cmd_q;
QueueHandle_t params_queue;

SemaphoreHandle_t audioReadySemaphore;

/* ===== Task prototypes ===== */
static void AudioTask(void* argument);
static void ControlInterfaceTask(void* argument);
static void UserInterfaceTask(void* argument);

/* ===== Global config structs =====*/
struct gpio_config gpio_cfg;
struct adc_config adc_interface_cfg;
// __attribute__((section(".sram1"))) struct SettingsData settings_data_ram;
struct SettingsData settings_data_ram;

/* ===== FreeRTOS init ===== */
void FREERTOS_Init(void) {
    /* create command queue */
    tape_cmd_q = xQueueCreate(8, sizeof(tape_cmd_msg_t));
    configASSERT(tape_cmd_q);

    params_queue = xQueueCreate(1, sizeof(struct parameters));
    configASSERT(params_queue);

    audioReadySemaphore = xSemaphoreCreateBinary();
    configASSERT(audioReadySemaphore);

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
        gpio_cfg.htim_button1_debounce = &htim13;
        gpio_cfg.htim_button2_debounce = &htim14;

        init_gpio_interface(&gpio_cfg);

#ifdef CONFIG_USE_CALIB_STORAGE
        int32_t b_read = read_settings_data(&settings_data_ram);
        if (b_read != sizeof(struct SettingsData) || settings_data_ram.magic != MAGIC_NUMBER) {
            // invalid data read, use defaults
#endif
            struct calibration_data* calibration = &settings_data_ram.calibration_data;
            // set default/fallback calibration data
            // C1/1V -> 44000 ADC value, C3/3V -> 25150 ADC value
            // C1 = 12. semitones, C3 = 36. semitones
            float c3 = float_value(25150);
            float c1 = float_value(44000);
            float delta_semitones = 36.0f - 12.0f;
            float delta = c3 - c1; // normaled CV difference between C1 and C3

            if (delta > -0.5f && delta < -0.0f) {
                calibration->voct_pitch_scale = delta_semitones / (c3 - c1);
                calibration->voct_pitch_offset =
                    12.0f - calibration->voct_pitch_scale * c1; // to calculate offset, either C1 or C3 can be used
            }

            for (size_t i = 1; i < NUM_CV_CHANNELS; ++i) {
                calibration->cv_offset[i] = 0.0f;
            }

#ifdef CONFIG_USE_CALIB_STORAGE
        }
#endif
    }
}

/* ===== Audio task ===== */
static void AudioTask(void* argument) {
    (void) argument;

    /* configure codec */
    if (codec_i2c_is_ok() == HAL_OK) {
        codec_init(AUDIO_SAMPLE_RATE);
    }

    struct audioengine_config audioengine_cfg = {
        .i2s_handle = &hi2s1, .sample_rate = AUDIO_SAMPLE_RATE, .buffer_size = AUDIO_BLOCK_SIZE, .audioTaskHandle = audioTaskHandle};

    struct tape_player tape_player;

    // wait for audio engine to be ready (signaled from uiface after calibration)
    if (xSemaphoreTake(audioReadySemaphore, portMAX_DELAY) == pdTRUE) {
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
#endif
            struct param_cache param_cache;
            param_cache_fetch(&param_cache);

            tape_player_set_params(param_cache);
        }
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
    // init ws2812 driver
    struct ws2812_config ws2812_cfg = {
        .htim_anim = &htim17,
        .htim_pwm = &htim15,
        .tim_channel_pwm = TIM_CHANNEL_1,
        .state.animation = anim_off,
    };

    ws2812_init(&ws2812_cfg);
    ws2812_start();

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

    uint32_t hold_time = 0;
    bool cv_feedback_given = false;
    bool pot_feedback_given = false;

    vTaskSuspend(audioTaskHandle);
    vTaskSuspend(controlIfTaskHandle);
    // Check both buttons pressed
    while (are_both_buttons_pushed()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        hold_time += 10;
        if (cv_feedback_given == false && hold_time >= CV_CALIB_HOLD_MS && hold_time < POT_CALIB_HOLD_MS) {
            // Short hold -> CV calibration feedback
            ws2812_change_animation(&anim_breathe_blue);
            cv_feedback_given = true;
        }
        if (pot_feedback_given == false && hold_time >= POT_CALIB_HOLD_MS) {
            // Long hold -> POT calibration
            ws2812_change_animation(&anim_breathe_blue_fast);
            pot_feedback_given = true;

            // wait for buttons released before starting calibration to avoid interference with button presses during calibration
            if (!wait_for_both_buttons_released()) {
                ws2812_change_animation(&anim_setting_error);
                break;
            }

            if (user_iface_calibrate_pitch_pot(&settings_data_ram.calibration_data) == 0) {
                write_settings_data(&settings_data_ram);
            } else {
                ws2812_change_animation(&anim_setting_error);
            }
        }
    }

    if (hold_time >= CV_CALIB_HOLD_MS && hold_time < POT_CALIB_HOLD_MS) {
        // Short hold -> CV calibration
        // led_feedback_cv_calib();
        if (control_interface_calibrate_voct(&settings_data_ram.calibration_data) == 0) {
            write_settings_data(&settings_data_ram);
        } else {
            ws2812_change_animation(&anim_setting_error);
        }
    }

    vTaskResume(audioTaskHandle);
    vTaskResume(controlIfTaskHandle);
    xSemaphoreGive(audioReadySemaphore);

    user_iface_init(&user_interface_cfg, &settings_data_ram.calibration_data);
    user_iface_start();

    ws2812_change_animation(&anim_bootup);

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
            }
            if (notified & GPIO_NOTIFY_BUTTON1) {
            }
            if (notified & GPIO_NOTIFY_BUTTON2) {
            }
            if (notified & GPIO_NOTIFY_GATE1) {
                ws2812_trigger_led(0, (struct ws2812_led) {.r = 0, .g = 255, .b = 0}, 3);
            }
            if (notified & GPIO_NOTIFY_GATE2) {
                ws2812_trigger_led(1, (struct ws2812_led) {.r = 255, .g = 0, .b = 0}, 3);
            }
            user_iface_process(notified);
        }
    }
}