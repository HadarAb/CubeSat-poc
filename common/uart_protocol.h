#ifndef CUBESAT_COMMON_UART_PROTOCOL_H
#define CUBESAT_COMMON_UART_PROTOCOL_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Ground-station UART wire format (little-endian):
 *			PACKET :
 *   UartFrameHeader_t | payload_length bytes | uint32_t crc32
 *
 * CRC-32 covers the complete header and payload.
 * UART_FRAME_START 0xA55A is transmitted as bytes 0x5A, 0xA5.
 */
#define UART_FRAME_START             0xA55Au
#define UART_MAX_PAYLOAD_SIZE        64u
#define UART_FRAME_CRC_SIZE          4u

#define UART_MSG_STATUS              0x01u
#define UART_MSG_PAYLOAD             0x02u
#define UART_MSG_BATTERY             0x03u
#define UART_MSG_DEBUG_TEXT          0x70u
#define UART_MSG_ERROR               0xFFu

#define UART_STATUS_OK               0x00u
#define UART_STATUS_NO_DATA          0x01u
#define UART_STATUS_I2C_ERROR        0x02u
#define UART_STATUS_CRC_ERROR        0x03u
#define UART_STATUS_UNKNOWN_MESSAGE  0x80u
#define UART_STATUS_BAD_REQUEST      0x81u

typedef struct __attribute__((packed))
{
    uint16_t start;
    uint8_t msg_type;
    uint16_t sequence;
    uint32_t timestamp_ms;
    uint16_t payload_length;
} UartFrameHeader_t;

typedef struct __attribute__((packed))
{
    uint8_t valid;
    uint8_t status;
    uint8_t node_id;
    uint8_t flags;
    uint32_t timestamp_ms;
    int16_t temperature_c_x10;
    uint16_t humidity_pct_x10;
    uint16_t radiation_cps;
    uint8_t battery_pct;
    uint32_t i2c_success_count;
    uint32_t i2c_error_count;
} UartPayload_t;

#define UART_FRAME_HEADER_SIZE ((uint16_t)sizeof(UartFrameHeader_t))
#define UART_MAX_FRAME_SIZE \
    (UART_FRAME_HEADER_SIZE + UART_MAX_PAYLOAD_SIZE + UART_FRAME_CRC_SIZE)

#ifdef __cplusplus
}

static_assert(sizeof(UartFrameHeader_t) == 11u,
              "UartFrameHeader_t wire layout must stay 11 bytes");
static_assert(sizeof(UartPayload_t) == 23u,
              "UartPayload_t wire layout must stay 23 bytes");
#else
_Static_assert(sizeof(UartFrameHeader_t) == 11u,
               "UartFrameHeader_t wire layout must stay 11 bytes");
_Static_assert(sizeof(UartPayload_t) == 23u,
               "UartPayload_t wire layout must stay 23 bytes");
#endif

#endif
