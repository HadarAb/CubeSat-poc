#pragma once

#include <stdbool.h>
#include <stdint.h>

/*Satellite power states*/
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
    SCHEDULE_ITEM_COUNT
} ScheduleItemId_t;

#ifdef __cplusplus
extern "C" {
#endif

/* Return the satellite's current power state. */
SatState_t PowerState_Get(void);

/* Reset every schedule item to its initial state. */
void Schedule_Init(void);

/* Return true once when an item is due, then move its next-due time forward. */
bool Schedule_TryTakeDue(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks);

/* Return the period for an item and state. Zero means the item is disabled. */
uint32_t Schedule_GetPeriodMs(ScheduleItemId_t item, SatState_t state);

#ifdef __cplusplus
}
#endif
