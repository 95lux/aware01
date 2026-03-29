/**
 * @file util.c
 * @brief CPU load statistics from FreeRTOS task runtime counters.
 */
#include "util.h"

#include "FreeRTOS.h"
#include "task.h"
#include <stdbool.h>
#include <string.h>

volatile cpu_stats_t cpu_stats;

void update_cpu_stats(void) {
    static TaskStatus_t prev[8];
    static uint32_t prev_total;
    static bool first = true;

    TaskStatus_t curr[8];
    uint32_t total;
    uint32_t n = uxTaskGetSystemState(curr, 8, &total);

    if (first || total == prev_total) {
        memcpy(prev, curr, sizeof(TaskStatus_t) * n);
        prev_total = total;
        first = false;
        return;
    }

    uint32_t delta_total = total - prev_total;

    for (uint32_t i = 0; i < n; i++) {
        uint32_t prev_counter = 0;
        for (uint32_t j = 0; j < n; j++) {
            if (curr[i].xHandle == prev[j].xHandle) {
                prev_counter = prev[j].ulRunTimeCounter;
                break;
            }
        }

        uint32_t delta = curr[i].ulRunTimeCounter - prev_counter;
        uint32_t pct = (uint32_t) (((uint64_t) delta * 100ULL) / delta_total);
        const char* name = curr[i].pcTaskName;

        if (strncmp(name, "Audio", 5) == 0)
            cpu_stats.audio_percent = pct;
        else if (strncmp(name, "ControlIF", 9) == 0)
            cpu_stats.control_percent = pct;
        else if (strncmp(name, "UserIF", 6) == 0)
            cpu_stats.userif_percent = pct;
        else if (strncmp(name, "IDLE", 4) == 0)
            cpu_stats.idle_percent = pct;
    }

    memcpy(prev, curr, sizeof(TaskStatus_t) * n);
    prev_total = total;
}
