/* Coordinates UART commands, periodic I2C polling, and cached OBC telemetry. */


#include "ObcController.hpp"

#include "I2CMaster.hpp"
#include "ScheduleApi.hpp"
#include "UartProtocol.hpp"
#include "../../common/i2c/bus_config.h"
#include "../../common/i2c/protocol.h"
#include "../../common/uart/uart_protocol.h"
#include "PayloadCollector.hpp"
#include "../Storage/SdLogger.hpp"

/*
 * Dev 1 owns these implementations. Weak references let this branch build
 * before the state and schedule modules are merged. Automatic status stays
 * disabled until the real functions are linked into the OBC firmware.
 */
#if defined(__GNUC__)
extern "C" SatState_t PowerState_Get(void) __attribute__((weak));
extern "C" void Schedule_Init(void) __attribute__((weak));
extern "C" bool Schedule_TryTakeDue(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks) __attribute__((weak));
#endif

namespace {
uint16_t automatic_status_sequence = 0u;

/* Builds the fixed UART payload from one node's latest thread safe snapshot. */
UartPayload_t BuildPayload(uint8_t node_id)
{
	UartPayload_t payload = {};
	payload.node_id = node_id;
	Snapshot snap;

	// Fetch the latest telemetry snapshot safely
	bool is_valid = PayloadCollector_GetSnapshot(node_id, &snap);

	if (is_valid) {
		// Populate payload with valid telemetry data
		payload.status = UART_STATUS_OK;
		payload.valid = 1u;
		payload.node_id = snap.data.node_id;
		payload.flags = snap.data.flags;
		payload.timestamp_ms = snap.data.timestamp_ms;
		payload.temperature_c_x10 = snap.data.temperature_c_x10;
		payload.humidity_pct_x10 = snap.data.humidity_pct_x10;
		payload.radiation_cps = snap.data.radiation_cps;
		payload.battery_pct = snap.data.battery_pct;

		// Legacy I2C counters are now managed internally by the Collector task
		payload.i2c_success_count = 0;
		payload.i2c_error_count = 0;
	} else {
		// Snapshot is invalid or sensor is currently offline
		payload.status = UART_STATUS_NO_DATA;
		payload.valid = 0u;
	}

	return payload;
}

/*send error frame */
void SendError(uint16_t sequence, uint8_t status)
{
    UartPayload_t payload = {};
    payload.status = status;

    UartProtocol_SendFrame(UART_MSG_ERROR, sequence, &payload, sizeof(payload));
}

/* Return true only when Dev 1's state and schedule functions are linked. */
bool ScheduleApiIsAvailable()
{
#if defined(__GNUC__)
    return (PowerState_Get != nullptr) && (Schedule_Init != nullptr) && (Schedule_TryTakeDue != nullptr);
#else
    return true;
#endif
}

/* Read the power state when Dev 1's power module is available. */
uint8_t GetPowerStateOrUnknown()
{
#if defined(__GNUC__)
    if (PowerState_Get == nullptr)
    {
        return 0xFFu;
    }
#endif

    const SatState_t state = PowerState_Get();

    if (static_cast<uint32_t>(state) >= static_cast<uint32_t>(SAT_STATE_COUNT))
    {
        return 0xFFu;
    }

    return static_cast<uint8_t>(state);
}

/*
 * Build OBC health from cached task data. GroundComm never accesses I2C or
 * FatFs directly; it only calls the read only status getters.
 */
UartStatusPayload_t BuildStatusPayload(uint8_t power_state)
{
    UartStatusPayload_t status = {};
    status.status = UART_STATUS_OK;
    status.power_state = power_state;
    status.sd_state = static_cast<uint8_t>(SdLogger_GetState());
    status.sd_error_count = SdLogger_GetErrorCount();

    Snapshot battery_snapshot = {};
    bool battery_valid = PayloadCollector_GetSnapshot(EPS_NODE_ID, &battery_snapshot) && battery_snapshot.battery_valid;

    if (!battery_valid)
    {
        // Payload is the PDF's fallback battery source when EPS has no sample.
        battery_valid = PayloadCollector_GetSnapshot(PAYLOAD_NODE_ID, &battery_snapshot) && battery_snapshot.battery_valid;
    }

    if (battery_valid)
    {
        status.battery_valid = 1u;
        status.battery_pct = battery_snapshot.data.battery_pct;
    }
    else
    {
        status.status = UART_STATUS_NO_DATA;
    }

    PayloadCollectorStatus_t collector = {};
    if (PayloadCollector_GetStatus(&collector))
    {
        status.payload_online = collector.payload_online ? 1u : 0u;
        status.eps_online = collector.eps_online ? 1u : 0u;
        status.dropped_frames = collector.dropped_frames;
        status.collector_overruns = collector.overruns;
        status.payload_i2c_errors = collector.payload_i2c_errors;
        status.eps_i2c_errors = collector.eps_i2c_errors;
        status.payload_crc_failures = collector.payload_crc_failures;
        status.eps_crc_failures = collector.eps_crc_failures;
    }

    return status;
}

/* Send the same system status structure for requested and automatic reports. */
void SendStatusResponse(uint8_t message_type, uint16_t sequence, uint8_t power_state)
{
    const UartStatusPayload_t status = BuildStatusPayload(power_state);
    UartProtocol_SendFrame(message_type, sequence, &status, sizeof(status));
}

/*
 * Send one automatic OBC status report. Sensor measurements are sent only by
 * UART_MSG_PAYLOAD and are not part of this message.
 */
void SendAutomaticStatus(SatState_t state)
{
    automatic_status_sequence++;
    SendStatusResponse(UART_MSG_AUTO_STATUS,
    		automatic_status_sequence, static_cast<uint8_t>(state));
}

/*
 * Ask the shared schedule if automatic status is due in the current state.
 * The schedule owns the timing policy; this function only performs the send.
 */
void ProcessAutomaticStatus()
{
    if (!ScheduleApiIsAvailable())
    {
        return;
    }

    const SatState_t state = PowerState_Get();

    if (static_cast<uint32_t>(state) >= static_cast<uint32_t>(SAT_STATE_COUNT))
    {
        // Do not flood UART if the power-state module temporarily reports bad data.
        return;
    }

    // HAL_GetTick() is the millisecond clock used by the schedule API.
    const uint32_t now_ticks = HAL_GetTick();

    if (Schedule_TryTakeDue(SCHEDULE_ITEM_AUTO_STATUS, state, now_ticks))
    {
        SendAutomaticStatus(state);
    }
}

/*
 * Handle one request from the ground station. Commands that take arguments read
 * them from req.payload and must check req.payload_length themselves, replying
 * UART_STATUS_BAD_REQUEST when it does not match what the command expects.
 */
void HandleRequest(const UartRequest_t& req)
{
    switch (req.msg_type)
    {
        case UART_MSG_STATUS:
        {
            SendStatusResponse(UART_MSG_STATUS, req.sequence, GetPowerStateOrUnknown());
            break;
        }

        case UART_MSG_PAYLOAD:
        {
            const UartPayload_t payload = BuildPayload(PAYLOAD_NODE_ID);
            UartProtocol_SendFrame(req.msg_type, req.sequence, &payload, sizeof(payload));
            break;
        }

        case UART_MSG_BATTERY:
        {
            const UartPayload_t payload = BuildPayload(EPS_NODE_ID);
            UartProtocol_SendFrame(req.msg_type, req.sequence, &payload, sizeof(payload));
            break;
        }

        default:
            SendError(req.sequence, UART_STATUS_UNKNOWN_MESSAGE);
            break;
    }
}
}

void ObcController_Init(I2C_HandleTypeDef* i2c_handle)
{
    automatic_status_sequence = 0u;

    I2CMaster_Init(i2c_handle);
    UartProtocol_Init();

#if defined(__GNUC__)
    if (Schedule_Init != nullptr)
    {
        Schedule_Init();
    }
#else
    Schedule_Init();
#endif

    SendUartMsg("OBC UART protocol ready");
}

// handles incoming messages
void ObcController_Process(void)
{
    UartRequest_t request;

    while (UartProtocol_TryReceiveRequest(&request) != 0u)
    {
        HandleRequest(request);
    }

    // Answer operator requests first, then check the automatic schedule.
    ProcessAutomaticStatus();
}
