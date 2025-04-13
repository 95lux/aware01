#ifndef TLV320_H
#define TLV320_H

#include "stdint.h"

void codec_beep_test(void);
void codec_write_reg(uint8_t reg, uint8_t value);

int8_t codec_i2c_is_ok();
void codec_init_playback();


#endif // TLV320_H