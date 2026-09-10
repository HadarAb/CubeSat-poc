// Coordinates UART commands, periodic I2C polling, and cached OBC telemetry.


#include "ObcController.hpp"

#include "I2CMaster.hpp"
#include "ScheduleApi.hpp"
#include "UartProtocol.hpp"
#include "../../common/i2c/bus_config.h"
#include "../../common/i2c/protocol.h"
#include "../../common/uart/uart_protocol.h"
#include "PayloadCollector.hpp"
#include "../Storage/SdLogger.hpp"
#include "../Power/task_watch.hpp"
#include "../Core/Inc/rtc.h"

#include <cstring>

/*
 * Dev 1 owns these implementations. Weak references let this branch build
 * before the state and schedule modules are merged. Automatic status stays
 * disabled until the real functions are linked into the OBC firmware.
 */
#if defined(__GNUC__)
extern "C" SatState_t power_state_get(void) __attribute__((weak));
extern "C" void schedule_init(void) __attribute__((weak));
extern "C" bool schedule_try_take_due(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks) __attribute__((weak));
#endif

namespace {
uint16_t automatic_status_sequence = 0u;

/* Builds the fixed UART payload from one node's latest thread safe snapshot. */
UartPayload_t build_payload(uint8_t node_id)
{
	UartPayload_t payload = {};
	payload.node_id = node_id;
	Snapshot snap;

	// Fetch the latest telemetry snapshot safely
	bool is_valid = payload_collector_get_snapshot(node_id, &snap);

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

// send error frame
void send_error(uint16_t sequence, uint8_t status)
{
    UartPayload_t payload = {};
    payload.status = status;

    uart_protocol_send_frame(UART_MSG_ERROR, sequence, &payload, sizeof(payload));
}

/* Return true only when Dev 1's state and schedule functions are linked. */
bool schedule_api_is_available()
{
#if defined(__GNUC__)
    return (power_state_get != nullptr) && (schedule_init != nullptr) && (schedule_try_take_due != nullptr);
#else
    return true;
#endif
}

/* Read the power state when Dev 1's power module is available. */
uint8_t get_power_state_or_unknown()
{
#if defined(__GNUC__)
    if (power_state_get == nullptr)
    {
        return 0xFFu;
    }
#endif

    const SatState_t state = power_state_get();

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
UartStatusPayload_t build_status_payload(uint8_t power_state)
{
    UartStatusPayload_t status = {};
    status.status = UART_STATUS_OK;
    status.power_state = power_state;
    status.sd_state = static_cast<uint8_t>(sd_logger_get_state());
    status.sd_error_count = sd_logger_get_error_count();

    Snapshot battery_snapshot = {};
    bool battery_valid = payload_collector_get_snapshot(EPS_NODE_ID, &battery_snapshot) && battery_snapshot.battery_valid;

    if (!battery_valid)
    {
        // Payload is the PDF's fallback battery source when EPS has no sample.
        battery_valid = payload_collector_get_snapshot(PAYLOAD_NODE_ID, &battery_snapshot) && battery_snapshot.battery_valid;
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
    if (payload_collector_get_status(&collector))
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

    // Inject the task-watch bitmask into status flags for ground visibility.
    status.flags |= static_cast<uint8_t>(task_watch_get_mask() & 0x0Fu);
    if (rtc_time_is_valid() != 0u) {
        status.flags |= OBC_FLAG_TIME_VALID;
    }

    return status;
}

/* Send the same system status structure for requested and automatic reports. */
void send_status_response(uint8_t message_type, uint16_t sequence, uint8_t power_state)
{
    const UartStatusPayload_t status = build_status_payload(power_state);
    uart_protocol_send_frame(message_type, sequence, &status, sizeof(status));
}

/*
 * Send one automatic OBC status report. Sensor measurements are sent only by
 * UART_MSG_PAYLOAD and are not part of this message.
 */
void send_automatic_status(SatState_t state)
{
    automatic_status_sequence++;
    send_status_response(UART_MSG_AUTO_STATUS,
    		automatic_status_sequence, static_cast<uint8_t>(state));
}

/*
 * Ask the shared schedule if automatic status is due in the current state.
 * The schedule owns the timing policy; this function only performs the send.
 */
void process_automatic_status()
{
    if (!schedule_api_is_available())
    {
        return;
    }

    const SatState_t state = power_state_get();

    if (static_cast<uint32_t>(state) >= static_cast<uint32_t>(SAT_STATE_COUNT))
    {
        // Do not flood UART if the power-state module temporarily reports bad data.
        return;
    }

    // HAL_GetTick() is the millisecond clock used by the schedule API.
    const uint32_t now_ticks = HAL_GetTick();

    if (schedule_try_take_due(SCHEDULE_ITEM_AUTO_STATUS, state, now_ticks))
    {
        send_automatic_status(state);
    }
}

/*
 * Handle one request from the ground station. Commands that take arguments read
 * them from req.payload and must check req.payload_length themselves, replying
 * UART_STATUS_BAD_REQUEST when it does not match what the command expects.
 */
void handle_request(const UartRequest_t& req)
{
    switch (req.msg_type)
    {
        case UART_MSG_STATUS:
        {
            send_status_response(UART_MSG_STATUS, req.sequence, get_power_state_or_unknown());
            break;
        }

        case UART_MSG_PAYLOAD:
        {
            const UartPayload_t payload = build_payload(PAYLOAD_NODE_ID);
            uart_protocol_send_frame(req.msg_type, req.sequence, &payload, sizeof(payload));
            break;
        }

        case UART_MSG_BATTERY:
        {
            const UartPayload_t payload = build_payload(EPS_NODE_ID);
            uart_protocol_send_frame(req.msg_type, req.sequence, &payload, sizeof(payload));
            break;
        }

        case UART_MSG_SET_TIME:
        {
            if (req.payload_length != sizeof(UartSetTimePayload_t)) {
                send_error(req.sequence, UART_STATUS_BAD_REQUEST);
                break;
            }

            UartSetTimePayload_t request = {};
            std::memcpy(&request, req.payload, sizeof(request));
            if (rtc_set_epoch(request.epoch_s) == 0u) {
                send_error(req.sequence, UART_STATUS_BAD_REQUEST);
                break;
            }

            uart_protocol_send_frame(req.msg_type, req.sequence, nullptr, 0u);
            break;
        }

        case UART_MSG_FETCH:
        {
            if (req.payload_length != sizeof(UartFetchPayload_t)) {
                send_error(req.sequence, UART_STATUS_BAD_REQUEST);
                break;
            }

            UartFetchPayload_t request = {};
            std::memcpy(&request, req.payload, sizeof(request));

            if ((request.from_epoch_s > request.to_epoch_s) || (request.volume > 1u)) {
                send_error(req.sequence, UART_STATUS_BAD_REQUEST);
                break;
            }

            if (sd_logger_request_fetch(req.sequence, &request) == 0u) {
                send_error(req.sequence, UART_STATUS_BUSY);
            }
            break;
        }

        default:
            send_error(req.sequence, UART_STATUS_UNKNOWN_MESSAGE);
            break;
    }
}
}

void obc_controller_init(I2C_HandleTypeDef* i2c_handle)
{
    automatic_status_sequence = 0u;

    i2c_master_init(i2c_handle);
    uart_protocol_init();

#if defined(__GNUC__)
    if (schedule_init != nullptr)
    {
        schedule_init();
    }
#else
    schedule_init();
#endif

    send_uart_msg("OBC UART protocol ready");
}

// handles incoming messages
void obc_controller_process(void)
{
    UartRequest_t request;

    while (uart_protocol_try_receive_request(&request) != 0u)
    {
        handle_request(request);
    }

    // Answer operator requests first, then check the automatic schedule.
    process_automatic_status();
}
