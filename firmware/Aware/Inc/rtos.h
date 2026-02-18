#pragma once

/* notification bits */
#define ADC_NOTIFY_CV_RDY (1U << 0)
#define ADC_NOTIFY_POTS_RDY (1U << 1)
#define GPIO_NOTIFY_GATE1 (1U << 2)
#define GPIO_NOTIFY_GATE2 (1U << 3)
#define GPIO_NOTIFY_GATE3 (1U << 4)
#define GPIO_NOTIFY_GATE4 (1U << 5)
#define GPIO_NOTIFY_BUTTON1 (1U << 6)
#define GPIO_NOTIFY_BUTTON2 (1U << 7)

void FREERTOS_Init(void);