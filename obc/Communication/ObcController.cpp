/* Coordinates UART commands, periodic I2C polling, and cached OBC telemetry. */


#include "ObcController.hpp"

#include "I2CMaster.hpp"
#include "UartProtocol.hpp"
#include "../../common/bus_config.h"
#include "../../common/protocol.h"
#include "../../common/uart_protocol.h"

namespace
{
constexpr uint32_t PayloadPollPeriodMs = 500u;

PayloadData_t last_payload = {};
HAL_StatusTypeDef last_whoami_status = HAL_ERROR;
HAL_StatusTypeDef last_data_status = HAL_ERROR;
uint8_t last_payload_id = 0u;
uint8_t last_crc_valid = 0u;
uint8_t payload_available = 0u;
uint8_t payload_read_attempted = 0u;
uint32_t i2c_success_count = 0u;
uint32_t i2c_error_count = 0u;
uint32_t next_payload_poll_ms = 0u;

/*current communication, uart, payload status */
uint8_t GetStatus(void)
{
    if (payload_read_attempted == 0u)
    {
        return UART_STATUS_NO_DATA;
    }

    if ((last_whoami_status != HAL_OK) || (last_payload_id != PAYLOAD_NODE_ID)
    				|| (last_data_status != HAL_OK))
    {
        return UART_STATUS_I2C_ERROR;
    }

    if (last_crc_valid == 0u)
    {
        return UART_STATUS_CRC_ERROR;
    }

    return (payload_available != 0u) ? UART_STATUS_OK : UART_STATUS_NO_DATA;
}

/* Builds the fixed UART payload from the latest cached telemetry and counters. */
UartPayload_t BuildPayload(void)
{
    UartPayload_t payload = {};
    payload.status = GetStatus();
    payload.i2c_success_count = i2c_success_count;
    payload.i2c_error_count = i2c_error_count;

    if (payload_available != 0u)
    {
        payload.valid = 1u;
        payload.node_id = last_payload.node_id;
        payload.flags = last_payload.flags;
        payload.timestamp_ms = last_payload.timestamp_ms;
        payload.temperature_c_x10 = last_payload.temperature_c_x10;
        payload.humidity_pct_x10 = last_payload.humidity_pct_x10;
        payload.radiation_cps = last_payload.radiation_cps;
        payload.battery_pct = last_payload.battery_pct;
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

/* gets last payload threw i2c  */
void PollPayload(void)
{
    uint8_t payload_id = 0u;
    uint8_t crc_valid = 0u;
    PayloadData_t candidate = {};

    payload_read_attempted = 1u;
    last_whoami_status = I2CMaster_ReadWhoAmI(PAYLOAD_I2C_ADDRESS_HAL, &payload_id);
    last_payload_id = payload_id;

    if ((last_whoami_status != HAL_OK) || (payload_id != PAYLOAD_NODE_ID))
    {
        last_data_status = HAL_ERROR;
        last_crc_valid = 0u;
        ++i2c_error_count;
        return;
    }

    last_data_status = I2CMaster_ReadPayloadData(PAYLOAD_I2C_ADDRESS_HAL, &candidate,
                                                 &crc_valid, nullptr);
    last_crc_valid = crc_valid;

    if (last_data_status != HAL_OK)
    {
        ++i2c_error_count;
        return;
    }

    ++i2c_success_count;

    if (crc_valid == 0u)
    {
        return;
    }

    last_payload = candidate;
    payload_available = 1u;
}
}

void ObcController_Init(I2C_HandleTypeDef* i2c_handle)
{
    I2CMaster_Init(i2c_handle);
    UartProtocol_Init();
    SendUartMsg("OBC UART protocol ready");
}

/*handles incoming msges */
void ObcController_Process(void)
{
    uint8_t msg_type = 0u;
    uint16_t sequence = 0u;

    while (UartProtocol_TryReceiveMessage(&msg_type, &sequence) != 0u)
    {
        HandleRequest(msg_type, sequence);
    }

    const uint32_t now = HAL_GetTick();
    if (static_cast<int32_t>(now - next_payload_poll_ms) >= 0)
    {
        next_payload_poll_ms = now + PayloadPollPeriodMs;
        PollPayload();
    }
}
