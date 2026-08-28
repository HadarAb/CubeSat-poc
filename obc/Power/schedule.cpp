// State dependent scheduling for every periodic job on the OBC
#include "../Communication/ScheduleApi.hpp"

#include <stdint.h>

namespace
{

// Zero means the item is disabled in that state
constexpr uint32_t item_disabled_ms = 0u;

// Next tick at which each item becomes due, in HAL_GetTick() milliseconds
uint32_t next_due_ms[SCHEDULE_ITEM_COUNT] = {};

// False until schedule_init has seeded the deadlines
bool schedule_ready = false;

}

namespace
{
/*
 * Mode to interval table. One row per schedule item, one column per power
 * state, in milliseconds. Zero disables the item in that state entirely,
 * which is what makes Critical genuinely cheaper rather than merely slower.
 */
constexpr uint32_t period_ms[SCHEDULE_ITEM_COUNT][SAT_STATE_COUNT] = {
    //                              CRITICAL   NORMAL     FULL
    /* SCHEDULE_ITEM_AUTO_STATUS */ {  60000u,   40000u,   20000u },
    /* PAYLOAD_TEMP             */  { 300000u,   60000u,   30000u },
    /* PAYLOAD_TDOSE            */  {3600000u, 1800000u,  900000u },
    /* PAYLOAD_SEL              */  {  10000u,    5000u,    1000u },
    /* PAYLOAD_NRESET           */  { 300000u,  120000u,   60000u },
    /* EPS_VBAT                 */  {  30000u,   15000u,    5000u },
    /* EPS_TEMP                 */  { 300000u,   60000u,   30000u },
    /* EPS_SP0_TEMP             */  {      0u,   60000u,   30000u },
    /* EPS_SP0_CURRENT          */  {      0u,   60000u,   30000u },
	/* SD_FLUSH                 */  {  30000u,   10000u,    2000u },
};
}

// Return the period for an item and state. Zero means the item is disabled
uint32_t Schedule_GetPeriodMs(ScheduleItemId_t item, SatState_t state)
{
    if ((static_cast<uint32_t>(item) >= static_cast<uint32_t>(SCHEDULE_ITEM_COUNT))
        || (static_cast<uint32_t>(state) >= static_cast<uint32_t>(SAT_STATE_COUNT))) {
        return item_disabled_ms;
    }

    return period_ms[item][state];
}


// Reset every schedule item to its initial state
void Schedule_Init(void)
{
    for (uint32_t index = 0u; index < static_cast<uint32_t>(SCHEDULE_ITEM_COUNT); ++index) {
        next_due_ms[index] = 0u;
    }

    schedule_ready = true;
}


/*
 * Return true once when an item is due, then move its next due time forward.
 * The signed cast makes the comparison safe when HAL_GetTick wraps at 2^32.
 */
bool Schedule_TryTakeDue(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks)
{
    const uint32_t period = Schedule_GetPeriodMs(item, state);

    if ((period == item_disabled_ms) || !schedule_ready) {
        return false;
    }

    const uint32_t index = static_cast<uint32_t>(item);

    // A zero deadline means this item has never run, so it is due immediately
    if (next_due_ms[index] == 0u) {
        next_due_ms[index] = now_ticks + period;
        return true;
    }

    if (static_cast<int32_t>(now_ticks - next_due_ms[index]) < 0) {
        return false;
    }

    /*
     * Advance from the deadline, not from now, so a late call does not shift
     * the whole cadence. If the item fell more than one period behind, skip
     * the missed slots instead of firing repeatedly to catch up.
     */
    next_due_ms[index] += period;

    if (static_cast<int32_t>(now_ticks - next_due_ms[index]) >= 0) {
        next_due_ms[index] = now_ticks + period;
    }

    return true;
}
