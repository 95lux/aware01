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
#include "dsp/exciter.h"
#include "dsp/schroeder_reverb.h"
#include "main.h"
#include "param_cache.h"
#include "tape_player.h"
#include "tim.h"
#include "user_interface.h"
#include "util.h"
#include "ws2812_animations.h"

/* ===== Global FreeRTOS objects ===== */
TaskHandle_t audioTaskHandle;
TaskHandle_t controlIfTaskHandle;
TaskHandle_t userIfTaskHandle;
static TaskHandle_t bootCalibTaskHandle = NULL;

QueueHandle_t tape_cmd_q;

SemaphoreHandle_t audioReadySemaphore;

/* ===== Task prototypes ===== */
static void AudioTask(void* argument);
static void ControlInterfaceTask(void* argument);
static void UserInterfaceTask(void* argument);

/* ===== Global config structs =====*/
// __attribute__((section(".sram1"))) struct SettingsData settings_data_ram;
struct SettingsData settings_data_ram;

/* ===== FreeRTOS init ===== */
void FREERTOS_Init(void) {
    /* create command queue */
    tape_cmd_q = xQueueCreate(8, sizeof(tape_cmd_msg_t));
    configASSERT(tape_cmd_q);

    audioReadySemaphore = xSemaphoreCreateBinary();
    configASSERT(audioReadySemaphore);

    /* create audio task (highest priority) */
    xTaskCreate(AudioTask, "Audio", 512, NULL, configMAX_PRIORITIES - 1, &audioTaskHandle);

    /* create control interface task */
    xTaskCreate(ControlInterfaceTask, "ControlIF", 256, NULL, configMAX_PRIORITIES - 3, &controlIfTaskHandle);

    /* create user interface task */
    xTaskCreate(UserInterfaceTask, "UserIF", 256, NULL, configMAX_PRIORITIES - 4, &userIfTaskHandle);

    {
        init_adc_interface(controlIfTaskHandle, userIfTaskHandle, &hadc2, &hadc1);
        start_adc_interface();

        init_gpio_interface(controlIfTaskHandle, userIfTaskHandle, &htim13, &htim14, tape_cmd_q);

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

    excite_config_t exciter;
    schroeder_stereo_t reverb;

    // wait for audio engine to be ready (signaled from uiface after calibration)
    if (xSemaphoreTake(audioReadySemaphore, portMAX_DELAY) == pdTRUE) {
        /* initialize audio engine */
        init_audioengine(&audioengine_cfg);
        init_tape_player(audioengine_cfg.buffer_size, tape_cmd_q);

        excite_init(&exciter);
        schroeder_rev_init(&reverb);
        schroeder_rev_set_wet(&reverb, 0.5f);

        start_audio_engine();

        for (;;) {
            // processing half audio block from half dma buffer.

            // fetch params
            struct param_cache param_cache;
            param_cache_fetch(&param_cache);

            /* wait for DMA signal */
            ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
#ifdef CONFIG_AUDIO_LOOPBACK
            // simple loopback for testing
            loopback_samples();
#else

            int16_t processed[AUDIO_HALF_BLOCK_SIZE]; // output of exciter, input to reverb, output of reverb (pre-dac)
            int16_t in_buf[AUDIO_HALF_BLOCK_SIZE];    // input from codec, output of tape player (pre-exciter)
            int16_t dry[AUDIO_HALF_BLOCK_SIZE];       // output of tape player, input to exciter (pre-reverb)

            audio_get_dma_in_buf(in_buf, AUDIO_HALF_BLOCK_SIZE);

            /* ----- TAPE PLAYER ----- */
            tape_player_set_params(param_cache);

#ifdef CONFIG_ENABLE_TAPE_PLAYER
            // tape player may be disabled to check simple dsp processing without tape player in the way, since it is currently the only source of audio input (no external input implemented yet)
            tape_player_process(in_buf, (int16_t*) dry);

            /* ----- TAPE PLAYER END ----- */

            /* ------ EXCITER ------ */
            excite_block(&exciter, dry, processed, AUDIO_HALF_BLOCK_SIZE, 1000.0f);

            float excite_amount = tape_player_get_grit();
            excite_amount = excite_amount * MAX_EXCITE_ON_MAX_DECIMATION;

            // mix wet and dry with fixed ratio for now (can be made variable later)
            for (uint32_t i = 0; i < AUDIO_HALF_BLOCK_SIZE; i++) {
                processed[i] = 1.0f * dry[i] + excite_amount * processed[i];

                // hardware saturation
                processed[i] = __SSAT(processed[i], 16);
            }
            /* ------ EXCITER END ------ */

#else
            // if tape player is disabled, just pass input directly to exciter and reverb for testing
            memcpy(processed, in_buf, sizeof(int16_t) * AUDIO_HALF_BLOCK_SIZE);
#endif

#ifdef CONFIG_ENABLE_REVERB
            /* ------ REVERB ------ */
            float reverb_size = param_cache.schroeder_verb_size;
            float reverb_feedback = param_cache.schroeder_verb_feedback;
            float reverb_wet = param_cache.schroeder_verb_wet;
            float reverb_lp_alpha = param_cache.schroeder_verb_lp_alpha;
            schroeder_rev_set_feedback(&reverb, reverb_feedback);
            schroeder_rev_set_size(&reverb, reverb_size);
            schroeder_rev_set_wet(&reverb, reverb_wet);
            // schroeder_rev_set_wet(&reverb, 1.0);
            schroeder_rev_set_lp_alpha(&reverb, reverb_lp_alpha);

            for (uint32_t i = 0; i < AUDIO_HALF_BLOCK_SIZE; i += 2) {
                float inL = (float) processed[i] / 32768.0f;
                float inR = (float) processed[i + 1] / 32768.0f;

                float outL, outR;

                schroeder_rev_process(&reverb, inL, inR, &outL, &outR);

                /* back to int16 */
                int32_t sL = (int32_t) (outL * 32768.0f);
                int32_t sR = (int32_t) (outR * 32768.0f);

                processed[i] = __SSAT(sL, 16);
                processed[i + 1] = __SSAT(sR, 16);
            }
            /* ------ REVERB END ------ */
#endif
            audio_write_dma_out_buf(processed, AUDIO_HALF_BLOCK_SIZE);

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
                case TAPE_CMD_SLICE:
                    tape_player_set_slice();
                    break;
                }
            }
#endif
        }
    }
}

static void boot_calibration_cleanup(void) {
    bootCalibTaskHandle = NULL;
    vTaskResume(audioTaskHandle);
    vTaskResume(controlIfTaskHandle);
    xSemaphoreGive(audioReadySemaphore);
}

static void BootCalibTask(void* argument) {
    (void) argument;

    uint32_t hold_time = 0;
    bool cv_feedback_given = false;
    bool pot_feedback_given = false;

    vTaskSuspend(audioTaskHandle);
    vTaskSuspend(controlIfTaskHandle);

    while (are_both_buttons_pushed()) {
        vTaskDelay(pdMS_TO_TICKS(10));
        hold_time += 10;

        if (!cv_feedback_given && hold_time >= CV_CALIB_HOLD_MS && hold_time < POT_CALIB_HOLD_MS) {
            ws2812_change_animation_all(&anim_breathe_blue);
            cv_feedback_given = true;
        }

        if (!pot_feedback_given && hold_time >= POT_CALIB_HOLD_MS) {
            ws2812_change_animation_all(&anim_breathe_blue_fast);
            pot_feedback_given = true;

            if (!wait_for_both_buttons_released()) {
                ws2812_change_animation_all(&anim_setting_error);
                boot_calibration_cleanup();
                vTaskDelete(NULL);
                return;
            }

            if (user_iface_calibrate_pitch_pot(&settings_data_ram.calibration_data) == 0)
                write_settings_data(&settings_data_ram);
            else
                ws2812_change_animation_all(&anim_setting_error);

            boot_calibration_cleanup();
            vTaskDelete(NULL);
            return;
        }
    }

    if (hold_time >= CV_CALIB_HOLD_MS && hold_time < POT_CALIB_HOLD_MS) {
        if (control_interface_calibrate_voct(&settings_data_ram.calibration_data) == 0)
            write_settings_data(&settings_data_ram);
        else
            ws2812_change_animation_all(&anim_setting_error);
    }

    boot_calibration_cleanup();
    vTaskDelete(NULL);
}

static void run_boot_calibration(void) {
    xTaskCreate(BootCalibTask, "BootCalib", 256, NULL, configMAX_PRIORITIES - 2, &bootCalibTaskHandle);
}

/* ===== Control interface task ===== */
static void ControlInterfaceTask(void* argument) {
    (void) argument;

    init_control_interface(&settings_data_ram.calibration_data, userIfTaskHandle, &hadc1);
    start_control_interface();

    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        control_interface_process();
    }
}

TickType_t last_print = 0;

/* ===== User interface task ===== */
static void UserInterfaceTask(void* argument) {
    DWT_Init();

    // init ws2812 driver
    ws2812_init_t ws2812_init_cfg = {
        .htim_anim = &htim17,
        .htim_pwm = &htim15,
        .tim_channel_pwm = TIM_CHANNEL_1,
        .default_animation = &anim_off,
        .userIfTaskHandle = userIfTaskHandle,
    };

    ws2812_init(&ws2812_init_cfg);
    ws2812_start();

    // runs its own task of buttons are held during boot
    run_boot_calibration();

    // TODO: rewire to different pin which supports pwm output.
    // user_interface_cfg.pot_leds[0].htim_led = &tim12; // this is wrongly assigned currently :(
    // proceed with UI initialization
    user_interface_init_t user_iface_init_cfg = {
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

    user_iface_init(&settings_data_ram.calibration_data, &user_iface_init_cfg, userIfTaskHandle);
    user_iface_start();

    ws2812_change_animation_all(&anim_bootup);

    uint32_t notified;

    for (;;) {
        // wait for any notification (ADC pots ready, buttons, gates)
        if (xTaskNotifyWait(0, UINT32_MAX, &notified, portMAX_DELAY) == pdTRUE) {
            if (notified & ADC_NOTIFY_POTS_RDY)
                user_iface_process_pots();

            if (notified & (GPIO_NOTIFY_BUTTON1 | GPIO_NOTIFY_BUTTON2))
                if (!bootCalibTaskHandle) // only process button presses if not in boot calibration, to avoid interference with calibration process
                    user_iface_process_buttons(notified);

            if (notified & (GPIO_NOTIFY_GATE1 | GPIO_NOTIFY_GATE2))
                user_iface_process_gates(notified);

            if (notified & WS2812_ANIM_NOTIFY)
                ws2812_run_step();
        }

        if ((xTaskGetTickCount() - last_print) > pdMS_TO_TICKS(100)) {
            update_cpu_stats();
            last_print = xTaskGetTickCount();
        }
    }
}