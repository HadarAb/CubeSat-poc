/* Coordinates UART commands, periodic I2C polling, and cached OBC telemetry. */


#include "ObcController.hpp"

#include "I2CMaster.hpp"
#include "UartProtocol.hpp"
#include "../../common/bus_config.h"
#include "../../common/protocol.h"
#include "../../common/uart_protocol.h"
#include "PayloadCollector.hpp"

namespace {
constexpr uint32_t PayloadPollPeriodMs = 500u;


/* Builds the fixed UART payload from the latest thread-safe snapshot. */
UartPayload_t BuildPayload(void)
{
	UartPayload_t payload = {};
	Snapshot snap;

	// Fetch the latest telemetry snapshot safely
	bool is_valid = PayloadCollector_GetSnapshot(NODE_ID_PAYLOAD, &snap);

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

/* handle request from ground station */
void HandleRequest(uint8_t msg_type, uint16_t sequence)
{
    switch (msg_type)
    {
        case UART_MSG_STATUS:
        case UART_MSG_PAYLOAD:
        case UART_MSG_BATTERY:
        {
            const UartPayload_t payload = BuildPayload();
            UartProtocol_SendFrame(msg_type, sequence, &payload, sizeof(payload));
            break;
        }

        default:
            SendError(sequence, UART_STATUS_UNKNOWN_MESSAGE);
            break;
    }
}
}

void ObcController_Init(I2C_HandleTypeDef* i2c_handle)
{
    I2CMaster_Init(i2c_handle);
    UartProtocol_Init();
    SendUartMsg("OBC UART protocol ready");
}

// handles incoming messages
void ObcController_Process(void)
{
    uint8_t msg_type = 0u;
    uint16_t sequence = 0u;

    while (UartProtocol_TryReceiveMessage(&msg_type, &sequence) != 0u)
    {
        HandleRequest(msg_type, sequence);
    }
}
