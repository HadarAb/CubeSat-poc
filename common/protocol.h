#ifndef CUBESAT_COMMON_PROTOCOL_H
#define CUBESAT_COMMON_PROTOCOL_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * I2C register-file interface.
 *
 * Read flow:
 *   1. The OBC writes one register number.
 *   2. The OBC reads the fixed-size response for that register.
 */
#define REG_WHOAMI  0x00u
#define REG_DATA    0x10u
#define REG_SETBATT 0x20u

/*
 * Public 7-bit bus address and the left-shifted value expected by STM32 HAL
 * master/slave APIs.
 */
// payload id
#define PAYLOAD_NODE_ID                0x02u
#define PAYLOAD_I2C_ADDRESS_7BIT       0x02u
#define PAYLOAD_I2C_ADDRESS_HAL        (PAYLOAD_I2C_ADDRESS_7BIT << 1u)

#define PAYLOAD_FLAG_SEU_INJECTED      0x01u

/*
 * Fixed little-endian payload response.
 *
 * radiation_cps is a simulated particle detector count rate in counts/second.
 * crc32 covers every byte before the crc32 field.
 */
// so the compiler wont add padding
typedef struct __attribute__((packed))
{
    uint32_t timestamp_ms;
    int16_t temperature_c_x10;
    uint16_t humidity_pct_x10;
    uint16_t radiation_cps;
    uint8_t battery_pct;
    uint8_t node_id;
    uint8_t flags;
    uint32_t crc32;
} PayloadData_t;

// macros to calculate size of crc or data size
#define PAYLOAD_DATA_CRC_SIZE ((uint32_t)offsetof(PayloadData_t, crc32))
#define PAYLOAD_DATA_WIRE_SIZE ((uint16_t)sizeof(PayloadData_t))

#ifdef __cplusplus
}

static_assert(sizeof(PayloadData_t) == 17u,
              "PayloadData_t wire layout must stay 17 bytes");
#else
_Static_assert(sizeof(PayloadData_t) == 17u,
               "PayloadData_t wire layout must stay 17 bytes");
#endif

#endif
