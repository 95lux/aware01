
#include "i2c.h"

#include "tlv320.h"

#define I2C_ADDR  (0x18 << 1) 

void codec_beep_test(void) {
    // Page 0
    codec_write_reg(0x00, 0x00);

    // Beep length (Registers 73–75)
    codec_write_reg(0x49, 0x01); // R73
    codec_write_reg(0x4A, 0x77); // R74
    codec_write_reg(0x4B, 0x00); // R75

    // Sine and cosine coefficients (Registers 76–79)
    codec_write_reg(0x4C, 0x23); // R76
    codec_write_reg(0x4D, 0xFB); // R77
    codec_write_reg(0x4E, 0x7A); // R78
    codec_write_reg(0x4F, 0xD7); // R79

    // Beep volume & enable
    codec_write_reg(0x48, 0x04); // Right beep: -2dB, independent
    codec_write_reg(0x47, 0x84); // Left beep: -2dB, beep ON
}

void codec_write_reg(uint8_t reg, uint8_t value)
{
    uint8_t data[2];
    data[0] = reg;
    data[1] = value;

    if (HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDR, data, 2, HAL_MAX_DELAY) != HAL_OK)
    {
        Error_Handler();
    }
}

void codec_init_playback(){
    // select page 0
    codec_write_reg(0x00, 0x00);

    // software reset
    codec_write_reg(0x01, 0x01);

    // NDAC divider
    codec_write_reg(0x0b, 0x81);

    // MDAC divider
    codec_write_reg(0x0c, 0x82);
    
    // OSR of DAC = 128
    codec_write_reg(0x0d, 0x00);
    codec_write_reg(0x0e, 0x80);

    // set i2s mode
    // set word length/frame size to 32bit to send 24bit audio data
    // b00110000
    codec_write_reg(0x1b, 0x30);

    // set the DAC Mode to PRB_P8
    codec_write_reg(0x3c, 0x08);

    // select page 1
    codec_write_reg(0x00, 0x01);

    // Disable weak AVDD-DVDD link     
    codec_write_reg(0x01, 0x08);

    // Enable Analog Blocks + set LDOs
    codec_write_reg(0x02, 0x01);

    // // enable master analog power control
    // codec_write_reg(0x02, 0x01);

    // set REF charging time to 40ms
    codec_write_reg(0x7b, 0x01);

    // HP soft stepping settings for optimal pop performance at power up
    // Rpop used is 6k with N = 6 and soft step = 20usec. This should work with 47uF coupling
    // capacitor. Can try N=5,6 or 7 time constants as well. Trade-off delay vs “pop” sound
    codec_write_reg(0x14, 0x25);

    // common mode to 0.75V
    // LDO input range 1.8V - 3.6V
    // Power HP by AVdd
    // 01000011
    codec_write_reg(0x0a, 0x43);

    // route dac to line outs
    // left channel
    codec_write_reg(0x0e, 0x08);
    // right channel
    codec_write_reg(0x0f, 0x08);

    // unmute line outs
    // LOL: unmute, 0 dB gain
    codec_write_reg(0x12, 0x00);
    // LOR: unmute, 0 dB gain
    codec_write_reg(0x13, 0x00);

    // Power up LOL and LOR drivers
	codec_write_reg(0x09, 0x3C);

    // TODO: soft stepping required?
    // switch to page 0
    codec_write_reg(0x00, 0x00);

    //  Power up the Left and Right DAC Channels with route the Left Audio digital data to
    // Left Channel DAC and Right Audio digital data to Right Channel DAC
    // 11010101
    codec_write_reg(0x3f, 0xd5);

    // unmute dac digital volume control
    codec_write_reg(0x40, 0x00);
}

int8_t codec_i2c_is_ok(){
    if(HAL_I2C_IsDeviceReady(&hi2c1, I2C_ADDR, 3, 1000) == HAL_OK)
        return 0;
        else return -1;
}
