#include "core_cm7.h"

#define PUTCHAR_PROTOTYPE int __io_putchar(int ch)

PUTCHAR_PROTOTYPE {
    ITM_SendChar(ch);
    return ch;
}