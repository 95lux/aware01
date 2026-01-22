#ifndef TLV320_H
#define TLV320_H

#include "stdint.h"

void codec_read_reg(uint8_t reg, uint8_t* buf);
void codec_write_reg(uint8_t reg, uint8_t value);

void codec_beep_test(void);
int codec_check_status();

int8_t codec_i2c_is_ok();
void codec_init();

#endif // TLV320_H