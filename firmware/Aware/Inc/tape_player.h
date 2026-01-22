#ifndef INC_TAPE_PLAYER_H_
#define INC_TAPE_PLAYER_H_

#include "audioengine.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t* ch[2]; // ch[0]=L, ch[1]=R
    uint32_t size;  // samples per channel
} tape_buffer_t;

struct tape_player {
    volatile int16_t* dma_in_buf;  // input buffer from ADC / I2S RX
    volatile int16_t* dma_out_buf; // output buffer to ADC / I2S TX
    size_t dma_buf_size;           // buffer size RX/TX
    tape_buffer_t tape_buf;        // holds tape audio - play source and recording
                                   // target of the tape
    uint32_t tape_playphase;       // Q16.16 (int16_t integer part, uint16_t frac part)
    uint32_t tape_recordhead;

    bool is_playing;
    bool is_recording;
    float pitch_factor;

    QueueHandle_t tape_cmd_q; // command queue handle
};

// FREERTOS queue message structure
typedef enum { TAPE_CMD_PLAY, TAPE_CMD_STOP, TAPE_CMD_RECORD } tape_cmd_t;
typedef struct {
    tape_cmd_t cmd;
    float pitch;
} tape_cmd_msg_t;

/* ISR-safe send wrapper */
BaseType_t tape_player_send_cmd_from_isr(const tape_cmd_msg_t* msg, BaseType_t* pxHigherPriorityTaskWoken);

int init_tape_player(struct tape_player* tape_player,
                     volatile int16_t* dma_in_buf,
                     volatile int16_t* dma_out_buf,
                     size_t dma_buf_size,
                     QueueHandle_t cmd_queue);

void tape_player_process(struct tape_player* tape);

// FREERTOS queue commands
void tape_send_play_cmd(QueueHandle_t q);
void tape_send_record_cmd(QueueHandle_t q);
void tape_send_stop_cmd(QueueHandle_t q);
void tape_send_set_pitch_cmd(QueueHandle_t q, float pitch);

#endif /* INC_TAPE_PLAYER_H_ */