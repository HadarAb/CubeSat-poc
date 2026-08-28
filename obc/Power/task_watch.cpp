// Task liveness bitmask. Groundwork for the IWDG supervisor, not yet armed
#include "task_watch.hpp"
#include "cmsis_os2.h"
#include "stm32l4xx_hal.h"

#include "cmsis_os2.h"

namespace
{
// Bits accumulate as tasks check in; the supervisor clears them
volatile uint32_t alive_mask = 0u;
}

void task_watch_init(void)
{
    alive_mask = 0u;
}

/*
 * Called from task context on every loop pass. The critical section keeps the
 * read-modify-write atomic against preemption by a higher priority task.
 */
void task_watch_checkin(uint32_t task_bit)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();

    alive_mask |= task_bit;

    if (previous_primask == 0u) {
        __enable_irq();
    }
}

bool task_watch_all_alive(void)
{
    return (alive_mask & TASK_WATCH_ALL_TASKS) == TASK_WATCH_ALL_TASKS;
}

void task_watch_clear(void)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();

    alive_mask = 0u;

    if (previous_primask == 0u) {
        __enable_irq();
    }
}

uint32_t task_watch_get_mask(void)
{
    return alive_mask;
}
