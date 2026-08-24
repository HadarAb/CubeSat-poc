#ifndef CUBESAT_COMMON_UART_PROTOCOL_H
#define CUBESAT_COMMON_UART_PROTOCOL_H

#include <stdint.h>

#include "../vtable/vtable.h"

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


//different msges that the uart can send
#define UART_MSG_STATUS              0x01u
#define UART_MSG_PAYLOAD             0x02u
#define UART_MSG_BATTERY             0x03u
#define UART_MSG_AUTO_STATUS         0x04u
#define UART_MSG_SIM_SET             0x40u
#define UART_MSG_SIM_GET             0x41u
#define UART_MSG_SIM_LIST            0x42u
#define UART_MSG_SIM_ACK             0x43u
#define UART_MSG_DEBUG_TEXT          0x70u
#define UART_MSG_ERROR               0xFFu

#define UART_STATUS_OK               0x00u
#define UART_STATUS_NO_DATA          0x01u
#define UART_STATUS_I2C_ERROR        0x02u
#define UART_STATUS_CRC_ERROR        0x03u
#define UART_STATUS_UNKNOWN_MESSAGE  0x80u
#define UART_STATUS_BAD_REQUEST      0x81u

#define UART_SIM_STATUS_OK           0x00u
#define UART_SIM_STATUS_BAD_REQUEST  0x01u
#define UART_SIM_STATUS_UNKNOWN_KEY  0x02u
#define UART_SIM_STATUS_TABLE_FULL   0x03u
#define UART_SIM_STATUS_BAD_TYPE     0x04u

// first header bytes for uart
typedef struct __attribute__((packed))
{
    uint16_t start;
    uint8_t msg_type;
    uint16_t sequence;
    uint32_t timestamp_ms;
    uint16_t payload_length;
} UartFrameHeader_t;

//old payload that we will replace
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

/*
 * OBC system status. STATUS and AUTO_STATUS use this structure, while PAYLOAD
 * and BATTERY continue using UartPayload_t.
 */
typedef struct __attribute__((packed))
{
    uint8_t status;
    uint8_t power_state;
    uint8_t battery_pct;
    uint8_t battery_valid;
    uint8_t payload_online;
    uint8_t eps_online;
    uint8_t sd_state;
    uint8_t reserved;
    uint32_t dropped_frames;
    uint32_t collector_overruns;
    uint32_t payload_i2c_errors;
    uint32_t eps_i2c_errors;
    uint32_t payload_crc_failures;
    uint32_t eps_crc_failures;
    uint32_t sd_error_count;
} UartStatusPayload_t;

// when you want to set a sensor value / or make a new sensor value
// watch out that all name and value have fixed size
typedef struct __attribute__((packed))
{
    char name[VT_NAME_LEN];		 // Temp , vbat , sel
    uint8_t type;                // anumber that later we will translate to data type. from vtable.h
    uint8_t len;                 // how many bytes from value are valid .
    uint8_t value[VT_VALUE_LEN]; // the value it self
} UartSimSetPayload_t;

// when you want to get a sensor data
typedef struct __attribute__((packed))
{
    char name[VT_NAME_LEN];
} UartSimGetPayload_t;

/*
 * response for this commands
 * SIM_SET  → create/update a value
 * SIM_GET  → get one value
 * SIM_LIST → get all values
*/
typedef struct __attribute__((packed))
{
    uint8_t status;				//success or error
    uint8_t request_type;		//reply to SET, GET, or LIST
    uint16_t index;				//its index in the vtable
    uint16_t count;				//total entries / diffrent names
    char name[VT_NAME_LEN];
    uint8_t type;
    uint8_t len;
    uint8_t value[VT_VALUE_LEN];
} UartSimAckPayload_t;

/*
 * One decoded, CRC-valid UART message. This is an in-memory object used by
 * both nodes after the shared parser removes the wire header and CRC.
 */
typedef struct
{
    uint8_t msg_type;
    uint16_t sequence;
    uint16_t payload_length;
    uint8_t payload[UART_MAX_PAYLOAD_SIZE];
} UartReceivedFrame_t;

#define UART_FRAME_HEADER_SIZE ((uint16_t)sizeof(UartFrameHeader_t))
#define UART_MAX_FRAME_SIZE (UART_FRAME_HEADER_SIZE + UART_MAX_PAYLOAD_SIZE + UART_FRAME_CRC_SIZE)

#ifdef __cplusplus
}

static_assert(sizeof(UartFrameHeader_t) == 11u,
              "UartFrameHeader_t wire layout must stay 11 bytes");
static_assert(sizeof(UartPayload_t) == 23u,
              "UartPayload_t wire layout must stay 23 bytes");
static_assert(sizeof(UartStatusPayload_t) == 36u,
              "UartStatusPayload_t wire layout must stay 36 bytes");
static_assert(sizeof(UartSimSetPayload_t) == 18u,
              "UartSimSetPayload_t wire layout must stay 18 bytes");
static_assert(sizeof(UartSimGetPayload_t) == 8u,
              "UartSimGetPayload_t wire layout must stay 8 bytes");
static_assert(sizeof(UartSimAckPayload_t) == 24u,
              "UartSimAckPayload_t wire layout must stay 24 bytes");
#else
_Static_assert(sizeof(UartFrameHeader_t) == 11u,
               "UartFrameHeader_t wire layout must stay 11 bytes");
_Static_assert(sizeof(UartPayload_t) == 23u,
               "UartPayload_t wire layout must stay 23 bytes");
_Static_assert(sizeof(UartStatusPayload_t) == 36u,
               "UartStatusPayload_t wire layout must stay 36 bytes");
_Static_assert(sizeof(UartSimSetPayload_t) == 18u,
               "UartSimSetPayload_t wire layout must stay 18 bytes");
_Static_assert(sizeof(UartSimGetPayload_t) == 8u,
               "UartSimGetPayload_t wire layout must stay 8 bytes");
_Static_assert(sizeof(UartSimAckPayload_t) == 24u,
               "UartSimAckPayload_t wire layout must stay 24 bytes");
#endif

#endif
