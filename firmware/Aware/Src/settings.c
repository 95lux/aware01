#include "stm32h7xx_hal.h"
#include "stm32h7xx_hal_flash.h"
#include "string.h"
#include <stdint.h>

#include "settings.h"

#define FLASH_USER_START_ADDR (FLASH_BANK2_BASE)                     /* Start @ of user Flash area in Bank2 */
#define FLASH_USER_END_ADDR (FLASH_BANK2_BASE + FLASH_BANK_SIZE - 1) /* End @ of user Flash area in Bank2 */

// get flash sector number from address
uint32_t GetSector(uint32_t Address) {
    uint32_t sector = 0;

    if (Address < (FLASH_BANK2_BASE + FLASH_BANK_SIZE)) {
        sector = (Address - FLASH_BANK2_BASE) / FLASH_SECTOR_SIZE;
    } else {
        sector = (Address - (FLASH_BANK2_BASE + FLASH_BANK_SIZE)) / FLASH_SECTOR_SIZE;
    }

    return sector;
}

// requires data to be word-aligned
// writes 'words' 32-bit words from data to flash starting at FLASH_USER_START_ADDR
// flashword is 16 bytes (4 x 32-bit words) at a time
// returns 0 on success, HAL error code on failure
// note: this function erases the entire user flash area before writing
uint32_t flash_write_words(uint32_t* src_ptr, uint32_t dest_addr, uint16_t numberofwords) {
    uint32_t flash_addr = dest_addr;
    static FLASH_EraseInitTypeDef EraseInitStruct;
    uint32_t SECTORError;

    // SCB_DisableICache();
    HAL_FLASH_Unlock();

    // Erase sectors
    uint32_t StartSector = GetSector(dest_addr);
    uint32_t EndSectorAddress = dest_addr + (numberofwords * 4) - 1;
    uint32_t EndSector = GetSector(EndSectorAddress);

    EraseInitStruct.TypeErase = FLASH_TYPEERASE_SECTORS;
    EraseInitStruct.Sector = StartSector;
    EraseInitStruct.Banks = FLASH_BANK_2;
    EraseInitStruct.NbSectors = EndSector - StartSector + 1;

    if (HAL_FLASHEx_Erase(&EraseInitStruct, &SECTORError) != HAL_OK) {
        HAL_FLASH_Lock();
        // SCB_EnableICache();
        return HAL_FLASH_GetError();
    }

    uint32_t err = HAL_FLASH_ERROR_NONE;

    // Write in 16-byte (FLASHWORD) chunks
    uint8_t* byte_ptr = (uint8_t*) src_ptr; // pointer to data as bytes
    uint32_t total_bytes = numberofwords * 4;

    for (uint32_t offset = 0; offset < total_bytes; offset += 16) {
        // pointer to 16-byte block in RAM
        uint32_t block_ptr = (uint32_t) (byte_ptr + offset);

        if (HAL_FLASH_Program(FLASH_TYPEPROGRAM_FLASHWORD, flash_addr + offset, block_ptr) != HAL_OK) {
            err |= HAL_FLASH_GetError();
            __HAL_FLASH_CLEAR_FLAG(FLASH_FLAG_ALL_ERRORS_BANK1 | FLASH_FLAG_ALL_ERRORS_BANK2);
        }
    }

    // TODO: maybe cache maintenance is not needed.
    SCB_CleanDCache();
    SCB_InvalidateDCache();

    HAL_FLASH_Lock();
    return err;
}

int write_settings_data(const struct SettingsData* settings_data) {
    static __attribute__((aligned(16))) struct SettingsData buffer;

    buffer = *settings_data; // real copy
    buffer.magic = MAGIC_NUMBER;

    uint32_t* data_ptr = (uint32_t*) &buffer;
    uint16_t words = sizeof(struct SettingsData) / sizeof(uint32_t);

    uint32_t result = flash_write_words(data_ptr, FLASH_USER_START_ADDR, words);

    if (result != HAL_FLASH_ERROR_NONE) {
        return -1;
    }

    return 0;
}

/**
 * @brief A generic, safe function to read 32-bit words from Flash.
 * @param src_addr  The starting physical address in Flash.
 * @param dst_buf   Pointer to the RAM buffer.
 * @param num_words Number of 32-bit words to read.
 * @return size_t   Total bytes read.
 */
size_t flash_read_words(uint32_t src_addr, uint32_t* dst_buf, uint16_t num_words) {
    if (dst_buf == NULL || num_words == 0)
        return 0;

    SCB_InvalidateDCache();

    // Point to the Flash address as a volatile 32-bit source
    // This prevents the compiler from optimizing away the reads.
    volatile uint32_t* flash_ptr = (volatile uint32_t*) src_addr;

    for (uint16_t i = 0; i < num_words; i++) {
        dst_buf[i] = flash_ptr[i];
    }

    return (size_t) num_words * sizeof(uint32_t);
}

/**
 * @brief Specific application function to load settings.
 */
int read_settings_data(struct SettingsData* settings_data) {
    uint16_t word_count = sizeof(struct SettingsData) / sizeof(uint32_t);

    return (int) flash_read_words(FLASH_USER_START_ADDR, (uint32_t*) settings_data, word_count);
}

// TODO: Maybe write test to verify that data written to flash can be read back correctly, and that invalid data is handled properly (e.g. by checking magic number).
int flash_roundtrip() {
    struct SettingsData settings_data_write = {
        .calibration_data =
            {
                .voct_pitch_scale = 123.4,
                .voct_pitch_offset = 124.5,
                .cv_offset = {1.1f, 2.2f, 3.3f, 4.4f},
                .pitchpot_max = 0.9f,
                .pitchpot_mid = 0.5f,
                .pitchpot_min = 0.1f,
            },

        .magic = MAGIC_NUMBER,
        .state =
            {
                .tobe_reserved0 = 0xAA,
                .tobe_reserved1 = 0xBB,
                .tobe_reserved2 = 0xCC,
                .tobe_reserved3 = 0xDD,
            },
    };
    write_settings_data(&settings_data_write);
    struct SettingsData settings_data_read;
    read_settings_data(&settings_data_read);
    return 0;
}