// Task liveness bitmask consumed by the IWDG supervisor.
#include "task_watch.hpp"
#include "stm32l4xx_hal.h"

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
 * Called from task context on every loop pass.
 */
void task_watch_checkin(uint32_t task_bit)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();

    //each task will call this function and will update this
    alive_mask |= (task_bit & TASK_WATCH_ALL_TASKS);

    if (previous_primask == 0u) {
        __enable_irq();
    }
}
/*
 * check all the bits if all are 1 returns true . and clears the mask
 * */
bool task_watch_all_alive_and_clear(void)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();

    const bool all_alive = (alive_mask & TASK_WATCH_ALL_TASKS) == TASK_WATCH_ALL_TASKS;

    if (all_alive) {
        alive_mask = 0u;
    }

    if (previous_primask == 0u) {
        __enable_irq();
    }

    return all_alive;
}

uint32_t task_watch_get_mask(void)
{
    return alive_mask;
}
