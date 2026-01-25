
#include "i2c.h"
#include "stdio.h"

#include "drivers/tlv320_driver.h"

#define I2C_ADDR (0x18 << 1)

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

int codec_check_status() {
    int ret = 0;

    uint8_t status;

    codec_write_reg(0x00, 0x00); // Go to Page 0

    // Check DAC Power
    codec_read_reg(0x25, &status);

    if (!(status & (0x01 << 1))) {
        printf("HPR powered down\n");
    }
    if (!(status & (0x01 << 2))) {
        printf("LOR Powered down\n");
        ret = -1;
    }
    if (!(status & (0x01 << 3))) {
        printf("DACR Powered down\n");
        ret = -1;
    }
    if (!(status & (0x01 << 5))) {
        printf("HPL powered down\n");
        ret = -1;
    }
    if (!(status & (0x01 << 6))) {
        printf("LOL Powered down\n");
        ret = -1;
    }
    if (!(status & (0x01 << 7))) {
        printf("DACL Powered down\n");
        ret = -1;
    }

    printf("codec seems to be configured fine via I2C\n");
    return ret;
}

void codec_write_reg(uint8_t reg, uint8_t value) {
    uint8_t data[2];
    data[0] = reg;
    data[1] = value;

    if (HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDR, data, 2, HAL_MAX_DELAY) != HAL_OK) {
        Error_Handler();
    }
}

void codec_read_reg(uint8_t reg, uint8_t* buf) {
    if (HAL_I2C_Master_Transmit(&hi2c1, I2C_ADDR, &reg, 1, HAL_MAX_DELAY) != HAL_OK) {
        Error_Handler();
    }

    if (HAL_I2C_Master_Receive(&hi2c1, I2C_ADDR, buf, 1, HAL_MAX_DELAY) != HAL_OK) {
        Error_Handler();
    }
}

void codec_init() {
    codec_write_reg(0x00, 0x00); // Page 0
    codec_write_reg(0x01, 0x01); // Reset

    // Clock & Interface
    codec_write_reg(0x0b, 0x81); // NDAC = 1
    codec_write_reg(0x0c, 0x82); // MDAC = 2
    codec_write_reg(0x12, 0x81); // NADC = 1
    codec_write_reg(0x13, 0x82); // MADC = 2
    codec_write_reg(0x0d, 0x00);
    codec_write_reg(0x0e, 0x80); // DAC OSR = 128
    codec_write_reg(0x14, 0x80); // ADC OSR = 128
    codec_write_reg(0x3d, 0x01); // PRB_R1
    codec_write_reg(0x1b, 0x00); // I2S 16-bit

    // Power Supplies
    codec_write_reg(0x00, 0x01); // Page 1
    codec_write_reg(0x01, 0x08); // Disable weak AVDD-DVDD link
    codec_write_reg(0x02, 0x01); // Power AVDD LDO
    codec_write_reg(0x14, 0x25); // HP soft step
    codec_write_reg(0x0a, 0x4b); // LDOIN config
    codec_write_reg(0x3D, 0x00); // ADC PTM_R4
    codec_write_reg(0x47, 0x32); // MicPGA delay
    codec_write_reg(0x7b, 0x01); // REF charge 40ms

    // Routing
    codec_write_reg(0x0e, 0x08); // DAC to LOL
    codec_write_reg(0x0f, 0x08); // DAC to LOR
    codec_write_reg(0x34, 0x80);
    codec_write_reg(0x36, 0x08); // LINE_IN L
    codec_write_reg(0x37, 0x80);
    codec_write_reg(0x39, 0x08); // LINE_IN R
    codec_write_reg(0x12, 0x00);
    codec_write_reg(0x13, 0x00); // Unmute LOL/LOR
    codec_write_reg(0x3b, 0x0c);
    codec_write_reg(0x3c, 0x0c); // Unmute PGA L/R
    codec_write_reg(0x09, 0x3C); // Power LOL/LOR

    codec_write_reg(0x00, 0x00); // Back to Page 0
    codec_write_reg(0x41, 0x00);
    codec_write_reg(0x42, 0x00); // DACL/R 0dB
    codec_write_reg(0x3f, 0xd5); // Power up DACs
    codec_write_reg(0x40, 0x00); // Unmute DAC volume
    codec_write_reg(0x51, 0xc0); // Power up ADCs
    codec_write_reg(0x52, 0x00); // Unmute ADC volume
}

int8_t codec_i2c_is_ok() {
    if (HAL_I2C_IsDeviceReady(&hi2c1, I2C_ADDR, 3, 1000) == HAL_OK)
        return 0;

    return -1;
}
