// Exponential Moving Average smoothed battery state machine with hysteresis
#include "power_state.hpp"

#include <stdint.h>

namespace
{
/*
 * Thresholds in battery percent. Enter a lower state at the threshold;
 * leave it only above threshold + hysteresis, so a battery parked on a
 * boundary cannot oscillate and flood the log.
 */
constexpr uint8_t critical_enter_pct = 20u;
constexpr uint8_t full_enter_pct = 80u;
constexpr uint8_t hysteresis_pct = 5u;

// EMA divisor per state: larger = slower, steadier. Indexed by SatState_t
constexpr uint8_t ema_divisor[SAT_STATE_COUNT] = { 8u, 4u, 4u };

volatile SatState_t s_state = SAT_STATE_NORMAL;
int32_t s_ema_x100 = -1; // -1 = not yet seeded
}

void power_state_init(void)
{
    s_state = SAT_STATE_NORMAL;
    s_ema_x100 = -1;
}

void power_state_update(uint8_t battery_pct, bool battery_valid)
{
    if (!battery_valid) {
        return; // hold the last known state
    }

    const int32_t sample_x100 = static_cast<int32_t>(battery_pct) * 100;

    if (s_ema_x100 < 0) {
        s_ema_x100 = sample_x100; // seed on first valid sample
    } else {
        const int32_t n = ema_divisor[s_state];
        s_ema_x100 += (sample_x100 - s_ema_x100) / n;
    }

    const int32_t pct = s_ema_x100 / 100;
    const SatState_t current = s_state;

    if (pct < critical_enter_pct) {
        s_state = SAT_STATE_CRITICAL;
    } else if (pct >= full_enter_pct) {
        s_state = SAT_STATE_FULL;
    } else if (current == SAT_STATE_CRITICAL) {
        // leave CRITICAL only once clearly above the threshold
        if (pct >= (critical_enter_pct + hysteresis_pct)) {
            s_state = SAT_STATE_NORMAL;
        }
    } else if (current == SAT_STATE_FULL) {
        if (pct < (full_enter_pct - hysteresis_pct)) {
            s_state = SAT_STATE_NORMAL;
        }
    } else {
        s_state = SAT_STATE_NORMAL;
    }
}

/*
 * Definition of the weak declaration in ScheduleApi.hpp.
 */
extern "C" SatState_t PowerState_Get(void)
{
    return s_state;
}
