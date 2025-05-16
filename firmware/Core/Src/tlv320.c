
#include "i2c.h"
#include "stdio.h"

#include "tlv320.h"

#define I2C_ADDR  (0x18 << 1) 

void codec_beep_test(void)
{
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

void codec_check_status()
{
    uint8_t status;

    codec_write_reg(0x00, 0x00); // Go to Page 0

    // Check DAC Power
    codec_read_reg(0x25, &status);
    if (!(status ||  (0x01 << 1))) {
        printf("HPR powered down\n");
    }
    if (!(status ||  (0x01 << 2))) {
        printf("LOR Powered down\n");
    }
    if (!(status ||  (0x01 << 3))) {
        printf("DACR Powered down\n");
    }
    if (!(status ||  (0x01 << 5))) {
        printf("HPL powered down\n");
    }
    if (!(status ||  (0x01 << 6))) {
        printf("LOL Powered down\n");
    }
    if (!(status ||  (0x01 << 7))) {
        printf("DACL Powered down\n");
    }

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

void codec_read_reg(uint8_t reg, uint8_t *buf)
{
    if (HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDR, &reg, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        Error_Handler();
    }

    if (HAL_I2C_Master_Receive(&hi2c1, I2C_ADDR, buf, 1, HAL_MAX_DELAY) != HAL_OK)
    {
        Error_Handler();
    }
}

void codec_init(){
    /* ---------- switch to page 0 ---------- */
    codec_write_reg(0x00, 0x00);
    // software reset
    codec_write_reg(0x01, 0x01);

    /*---- Clock and Interface Settings ----*/
    // for Fs = 48KHz and MCLK = 12.288MHz
    // Fs = MCLK/(NDAC * MDAC * OSR)
    // 48kHz = 12.288MHz/(1 * 2 * 128)

    // NDAC divider = 1
    codec_write_reg(0x0b, 0x81);
    // MDAC divider = 2
    codec_write_reg(0x0c, 0x82);

    // Power up NADC divider with value 1
    codec_write_reg(0x12, 0x81);

    // Power up MADC divider with value 2
    codec_write_reg(0x13, 0x82);
    
    // OSR of DAC = 128
    codec_write_reg(0x0d, 0x00);
    codec_write_reg(0x0e, 0x80);

    // OSR of ADC = 128
    codec_write_reg(0x14, 0x80);

    // set PRB_R1
    codec_write_reg(0x3d, 0x01);

    // set i2s mode
    // set word length/frame size to 16bit
    codec_write_reg(0x1b, 0x00);

    /*---- Configure Power Supplies ----*/

    /* ---------- switch to page 1 ---------- */
    codec_write_reg(0x00, 0x01);

    // Disable weak AVDD-DVDD link     
    codec_write_reg(0x01, 0x08);

    // Power AVDD LDO
    codec_write_reg(0x02, 0x01);

    // HP soft stepping settings for optimal pop performance at power up
    // Rpop used is 6k with N = 6 and soft step = 20usec. This should work with 47uF coupling
    // capacitor. Can try N=5,6 or 7 time constants as well. Trade-off delay vs “pop” sound
    codec_write_reg(0x14, 0x25);

    // D7   = 0     reserved
    // D6   = 1     common mode to 0.75V
    // D4-5 = 00    Output Common Mode for HPL and HPR is same as full-chip common mode
    // D3   = 0     Output Common Mode for LOL and LOR is same as full-chip common mode
    // D2   = 0     reserved
    // D1   = 1     Power HP by LDOIN     
    // D0   = 1     LDOIN pin range 1.8V - 3.6V
    // 
    // 01000011 = 0x43
    codec_write_reg(0x0a, 0x4b);

    // Select ADC PTM_R4
    codec_write_reg(0x3D, 0x00);

    // Set MicPGA startup delay to 3.1ms
    codec_write_reg(0x47, 0x32);

    // set REF charging time to 40ms
    codec_write_reg(0x7b, 0x01);
    
    /* ---- Routing ----*/

    // route dac to line outs
    // left channel
    codec_write_reg(0x0e, 0x08);
    // right channel
    codec_write_reg(0x0f, 0x08);

    // Route LINE_IN Left to LEFT_P with 20K input impedance
    codec_write_reg(0x34, 0x80);  
    // Route Common Mode to LEFT_M with impedance of 20K
    codec_write_reg(0x36, 0x80);  
    // Route LINE_IN Right to RIGHT_P with 20K input impedance
    codec_write_reg(0x37, 0x80);
    // Route Common Mode to RIGHT_M with impedance of 20K
    codec_write_reg(0x39, 0x80);

    // unmute line outs
    // LOL: unmute, 0 dB gain
    codec_write_reg(0x12, 0x00);
    // LOR: unmute, 0 dB gain
    codec_write_reg(0x13, 0x00);

    // unmute left PGA
    codec_write_reg(0x3b, 0x0c);
    // unmute right PGA
    codec_write_reg(0x3c, 0x0c);

    // Power up LOL and LOR drivers
	codec_write_reg(0x09, 0x3C);

    /* ---------- switch to page 0 ---------- */
    codec_write_reg(0x00, 0x00);

    // route codec DIN to DOUT
    // codec_write_reg(0x1d, 0x20);

    // DACL DACR -> 0dB
    codec_write_reg(0x41, 0x00);
    codec_write_reg(0x42, 0x00);

    //  Power up the Left and Right DAC Channels with route the Left Audio digital data to
    // Left Channel DAC and Right Audio digital data to Right Channel DAC
    // 11010101
    codec_write_reg(0x3f, 0xd5);

    // unmute dac digital volume control
    codec_write_reg(0x40, 0x00);

    // Power up Left and Right ADC Channels
    // Do not use digital mic
    codec_write_reg(0x51, 0xc0);

    // Unmute Left and Right ADC Digital Volume Control
    codec_write_reg(0x52, 0x00);
}

int8_t codec_i2c_is_ok(){
    if(HAL_I2C_IsDeviceReady(&hi2c1, I2C_ADDR, 3, 1000) == HAL_OK)
        return 0;
        else return -1;
}
