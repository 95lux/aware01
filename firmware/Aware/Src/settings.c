#include "stm32h7xx_hal.h"
#include <stdint.h>

#include "settings.h"

#define FLASH_USER_START_ADDR (FLASH_BASE + (FLASH_SECTOR_SIZE * 4)) /* Start @ of user Flash area in Bank1 */
#define FLASH_USER_END_ADDR (FLASH_BASE + FLASH_BANK_SIZE - 1)       /* End @ of user Flash area in Bank1 */

// get flash sector number from address
uint32_t GetSector(uint32_t Address) {
    uint32_t sector = 0;

    if (Address < (FLASH_BASE + FLASH_BANK_SIZE)) {
        sector = (Address - FLASH_BASE) / FLASH_SECTOR_SIZE;
    } else {
        sector = (Address - (FLASH_BASE + FLASH_BANK_SIZE)) / FLASH_SECTOR_SIZE;
    }

    return sector;
}

// requires data to be word-aligned
// writes 'words' 32-bit words from data to flash starting at FLASH_USER_START_ADDR
// flashword is 16 bytes (4 x 32-bit words) at a time
// returns 0 on success, HAL error code on failure
// note: this function erases the entire user flash area before writing
uint32_t flash_write(uint32_t* data, uint16_t words) {
    uint32_t first_sector, number_sectors, address;
    FLASH_EraseInitTypeDef erase = {0};
    uint32_t error;

    HAL_FLASH_Unlock();

    first_sector = GetSector(FLASH_USER_START_ADDR);
    number_sectors = GetSector(FLASH_USER_END_ADDR) - first_sector + 1;

    erase.TypeErase = FLASH_TYPEERASE_SECTORS;
    erase.Banks = FLASH_BANK_1;
    erase.Sector = first_sector;
    erase.NbSectors = number_sectors;

    if (HAL_FLASHEx_Erase(&erase, &error) != HAL_OK)
        return HAL_FLASH_GetError();

    address = FLASH_USER_START_ADDR;

    // write loop, 16 bytes (4 words) at a time
    for (uint16_t i = 0; i < words; i += 4) {
        uint32_t FlashWord[4] = {0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF, 0xFFFFFFFF};

        // copy remaining words (avoid overflow)
        for (uint8_t j = 0; j < 4 && (i + j) < words; j++) {
            FlashWord[j] = data[i + j];
        }

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, address, (uint32_t) FlashWord) != HAL_OK) {
            HAL_FLASH_Lock();
            return HAL_FLASH_GetError();
        }
        address += 16; // next 16-byte block
    }

    HAL_FLASH_Lock();
    return 0;
}

// returns 1 on success, 0 on failure
int write_calibration_data(struct CalibrationData* calib_data) {
    uint32_t* data_ptr = (uint32_t*) calib_data;
    uint16_t words = sizeof(struct CalibrationData) / 4; // number of 32-bit words

    // write calibration data to flash
    uint32_t result = flash_write(data_ptr, words);
    return (result == 0);
}

// returns the number of bytes read
int read_calibration_data(struct CalibrationData* calib_data) {
    uint32_t* data_ptr = (uint32_t*) calib_data;
    uint16_t words = sizeof(struct CalibrationData) / 4;

    uint32_t* flash_ptr = (uint32_t*) FLASH_USER_START_ADDR;
    for (uint16_t i = 0; i < words; i++) {
        data_ptr[i] = flash_ptr[i];
    }
    return words * 4; // number of bytes read
}