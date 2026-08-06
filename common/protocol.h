#ifndef CUBESAT_COMMON_PROTOCOL_H
#define CUBESAT_COMMON_PROTOCOL_H

#include <stdint.h>

#include "vtable.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Logical node IDs. These are a data field the ground station decodes, not bus
 * addresses. Physical I2C addresses live in bus_config.h and are unrelated.
 */
#define PAYLOAD_NODE_ID  0x02u
#define EPS_NODE_ID      0x03u

/*
 * I2C register file, protocol v2.
 *
 * Read flow is unchanged: the OBC writes one register number, then reads the
 * fixed-size response for that register. What changed is that the OBC now asks
 * for one VTable entry at a time instead of one bulk struct, so polling can be
 * scheduled per sensor.
 *
 *   Read one value by key:   write REG_VT_SELECT + key, then read REG_VT_VALUE
 *   Discover what a node has: read REG_VT_COUNT, then for each index
 *                             write REG_VT_AT + index, then read REG_VT_ENTRY
 */
#define REG_WHOAMI     0x00u  /* read 1 byte, the logical node ID */
#define REG_VT_COUNT   0x30u  /* read 2 bytes, live VTable entry count */
#define REG_VT_SELECT  0x31u  /* write VT_NAME_LEN bytes, select by key name */
#define REG_VT_VALUE   0x32u  /* read VT_VALUE_WIRE_SIZE bytes from the selection */
#define REG_VT_AT      0x33u  /* write 2 bytes, select by index */
#define REG_VT_ENTRY   0x34u  /* read VT_ENTRY_WIRE_SIZE bytes from the selection */

/*
 * Legacy bulk-read registers. Retired by the VTable registers above and served
 * only by the pre-Phase-4 Payload firmware. Remove once both nodes are migrated.
 */
#define REG_DATA       0x10u
#define REG_SETBATT    0x20u

#define PAYLOAD_FLAG_SEU_INJECTED  0x01u

#ifdef __cplusplus
}
#endif

#endif
