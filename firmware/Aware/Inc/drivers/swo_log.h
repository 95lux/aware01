#pragma once
// TODO: change to UART logging for simplicity.

#include "core_cm7.h"
#include "system_stm32h7xx.h"

static inline void DWT_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk; // master trace enable
    DWT->LAR = 0xC5ACCE55;                          // unlock DWT
    DWT->CYCCNT = 0;                                // reset counter
    DWT->CTRL |= DWT_CTRL_CYCCNTENA_Msk;            // start counting
}

static inline void ITM_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    ITM->LAR = 0xC5ACCE55;

    TPI->ACPR = (SystemCoreClock / 2000000U) - 1;
    TPI->SPPR = 0x00000002; // NRZ
    TPI->FFCR = 0x00000100;

    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_SYNCENA_Msk | ITM_TCR_DWTENA_Msk;
    ITM->TER = 0x1;
}

static inline void ITM_SendU32(uint32_t v) {
    ITM_SendChar(v);
    ITM_SendChar(v >> 8);
    ITM_SendChar(v >> 16);
    ITM_SendChar(v >> 24);
}
