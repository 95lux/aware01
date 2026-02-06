#pragma once

#include <stdint.h>

uint32_t min_u32(uint32_t a, uint32_t b) {
    return (a < b) ? a : b;
}