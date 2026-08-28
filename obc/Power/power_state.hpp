// Battery driven satellite power state, published for the dispatcher and beacon
#pragma once

#include "../Communication/ScheduleApi.hpp" // SatState_t

#ifdef __cplusplus
extern "C" {
#endif

// Call once before the scheduler starts
void power_state_init(void);

// Feed one battery percent sample. Called from Task_PowerMgmt
void power_state_update(uint8_t battery_pct, bool battery_valid);

// PowerState_Get() is declared in ScheduleApi.hpp. Implemented in PowerState.cpp.

#ifdef __cplusplus
}
#endif

