#include "FreeRTOS.h"
#include "task.h"
#include "queue.h"

#include "main.h"
#include <stdbool.h>
#include "tlv320.h"
#include "audioengine.h"
#include "tape_player.h"

/* ===== Global FreeRTOS objects ===== */
TaskHandle_t audioTaskHandle;
TaskHandle_t controlIfTaskHandle;
TaskHandle_t userIfTaskHandle;

QueueHandle_t tape_cmd_q;
SemaphoreHandle_t dma_ready_sem;

/* ===== Task prototypes ===== */
static void AudioTask(void* argument);
static void ControlInterfaceTask(void* argument);
static void UserInterfaceTask(void* argument);

/* ===== FreeRTOS init ===== */
void FREERTOS_Init(void) {
    /* create command queue */
    tape_cmd_q = xQueueCreate(8, sizeof(tape_cmd_msg_t));
    configASSERT(tape_cmd_q);

    /* create audio task (highest priority) */
    xTaskCreate(AudioTask, "Audio", 512, NULL, configMAX_PRIORITIES - 1, &audioTaskHandle);

    /* create control interface task */
    xTaskCreate(ControlInterfaceTask, "ControlIF", 256, NULL, configMAX_PRIORITIES - 3, &controlIfTaskHandle);

    /* create user interface task */
    xTaskCreate(UserInterfaceTask, "UserIF", 256, NULL, configMAX_PRIORITIES - 4, &userIfTaskHandle);
}

/* ===== Audio task ===== */
static void AudioTask(void* argument) {
    (void) argument;

    /* configure codec */
    if (codec_i2c_is_ok() == HAL_OK) {
        codec_init();
    }

    struct audioengine_config audioengine_cfg = {
        .i2s_handle = &hi2s1, .sample_rate = SAMPLE_RATE, .buffer_size = AUDIO_BLOCK_SIZE, .audioTaskHandle = audioTaskHandle};

    struct audioengine_tape tape_player;

    /* initialize audio engine */
    init_audioengine(&audioengine_cfg);
    init_tape_player(&tape_player, audioengine_cfg.rx_buf_ptr, audioengine_cfg.tx_buf_ptr, audioengine_cfg.buffer_size, tape_cmd_q);

    start_audio_engine();

    for (;;) {
        /* wait for DMA signal */
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);

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
            case TAPE_CMD_SET_PITCH:
                tape_player.pitch_factor = msg.pitch;
                break;
            }
        }
    }
}

/* ===== Control interface task ===== */
static void ControlInterfaceTask(void* argument) {
    (void) argument;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}

/* ===== User interface task ===== */
static void UserInterfaceTask(void* argument) {
    (void) argument;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(1));
    }
}
