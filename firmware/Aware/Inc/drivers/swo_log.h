#pragma once
// TODO: change to UART logging for simplicity.

#include "core_cm7.h"

static inline void ITM_Init(void) {
    CoreDebug->DEMCR |= CoreDebug_DEMCR_TRCENA_Msk;
    ITM->TCR = ITM_TCR_ITMENA_Msk | ITM_TCR_SYNCENA_Msk;
    ITM->TER = 0x1; // enable stimulus port 0
}

static inline void ITM_SendU32(uint32_t v) {
    ITM_SendChar(v);
    ITM_SendChar(v >> 8);
    ITM_SendChar(v >> 16);
    ITM_SendChar(v >> 24);
}
