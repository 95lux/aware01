#ifndef INC_TAPE_PLAYER_H_
#define INC_TAPE_PLAYER_H_

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    int16_t* ch[2]; // ch[0]=L, ch[1]=R
    uint32_t size;  // samples per channel
} tape_buffer_t;

struct audioengine_tape {
    volatile int16_t* dma_in_buf;  // input buffer from ADC / I2S RX
    volatile int16_t* dma_out_buf; // output buffer to ADC / I2S TX
    size_t dma_buf_size;           // buffer size RX/TX
    tape_buffer_t tape_buf;        // holds tape audio - play source and recording
                                   // target of the tape
    float tape_playhead;
    uint32_t tape_recordhead;
    bool is_playing;
    bool is_recording;
    float pitch_factor;
};

int init_tape_player(struct audioengine_tape* tape_player,
                     volatile int16_t* dma_in_buf,
                     volatile int16_t* dma_out_buf,
                     size_t dma_buf_size);
void tape_player_play(struct audioengine_tape* tape_player);
void tape_player_record(struct audioengine_tape* tape_player);
void tape_player_stop(struct audioengine_tape* tape_player);
void tape_player_change_pitch(struct audioengine_tape* tape_player, float pitch_factor);
void tape_player_process(struct audioengine_tape* tape);

#endif /* INC_TAPE_PLAYER_H_ */