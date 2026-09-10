#pragma once

#include <stdbool.h>
#include <stdint.h>

// Satellite power states
typedef enum
{
    SAT_STATE_CRITICAL = 0,
    SAT_STATE_NORMAL = 1,
    SAT_STATE_FULL = 2,
    SAT_STATE_COUNT = 3
} SatState_t;

/*
 * Scheduled jobs known by the schedule module.
 * Sensor jobs are added here .
 */
typedef enum
{
    SCHEDULE_ITEM_AUTO_STATUS = 0,
    SCHEDULE_ITEM_PAYLOAD_TEMP,
    SCHEDULE_ITEM_PAYLOAD_TDOSE,
    SCHEDULE_ITEM_PAYLOAD_SEL,
    SCHEDULE_ITEM_PAYLOAD_NRESET,
    SCHEDULE_ITEM_EPS_VBAT,
    SCHEDULE_ITEM_EPS_TEMP,
    SCHEDULE_ITEM_EPS_SP0_TEMP,
    SCHEDULE_ITEM_EPS_SP0_CURRENT,
	SCHEDULE_ITEM_SD_FLUSH,
    SCHEDULE_ITEM_COUNT
} ScheduleItemId_t;

#ifdef __cplusplus
extern "C" {
#endif

// Return the satellite's current power state.
SatState_t power_state_get(void);

// Reset every schedule item to its initial state.
void schedule_init(void);

// Return true once when an item is due, then move its next-due time forward.
bool schedule_try_take_due(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks);

// Return the period for an item and state. Zero means the item is disabled.
uint32_t schedule_get_period_ms(ScheduleItemId_t item, SatState_t state);

#ifdef __cplusplus
}
#endif
