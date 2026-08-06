#ifndef CUBESAT_COMMON_LEGACY_PAYLOAD_DATA_H
#define CUBESAT_COMMON_LEGACY_PAYLOAD_DATA_H

#include <stddef.h>
#include <stdint.h>

/*
 * Retired fixed-struct payload record.
 *
 * Superseded by the VTable key/value interface in vtable.h and by I2C protocol
 * v2 in protocol.h. It is kept only so the pre-Phase-4 Payload path keeps
 * building while the nodes are migrated. Do not use it in new code.
 *
 * Delete this file, and every include of it, once REG_DATA is gone from both
 * the OBC and the node firmwares. "grep -rn legacy_payload_data" is the
 * migration checklist.
 */

#ifdef __cplusplus
extern "C" {
#endif

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
