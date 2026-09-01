// Task liveness bitmask consumed by the IWDG supervisor.
#pragma once

#include <stdbool.h>
#include <stdint.h>

// One bit per supervised task
#define TASK_WATCH_COLLECTOR (1u << 0)
#define TASK_WATCH_SD_LOGGER (1u << 1)
#define TASK_WATCH_POWER_MGMT (1u << 2)
#define TASK_WATCH_GROUND_COMM (1u << 3)

#define TASK_WATCH_ALL_TASKS (TASK_WATCH_COLLECTOR | TASK_WATCH_SD_LOGGER | TASK_WATCH_POWER_MGMT | TASK_WATCH_GROUND_COMM)

#ifdef __cplusplus
extern "C" {
#endif

// Call once before the scheduler starts
void task_watch_init(void);

// Each supervised task calls this once per loop pass
void task_watch_checkin(uint32_t task_bit);

// Atomically test for a complete supervision window. When all tasks have
// checked in, clear the accumulated bits and return true. When any task is
// missing, keep the reported bits and return false.
bool task_watch_all_alive_and_clear(void);

// Read-only bitmask, for reporting over UART
uint32_t task_watch_get_mask(void);

#ifdef __cplusplus
}
#endif
