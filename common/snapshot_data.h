#ifndef CUBESAT_COMMON_SNAPSHOT_DATA_H
#define CUBESAT_COMMON_SNAPSHOT_DATA_H

#include <stdint.h>
#include <stddef.h>

/*
 * Latest decoded telemetry for one node, held in RAM.
 *
 * The collector fills these fields from individual VTable key reads, and the
 * ground-station path reads them through PayloadCollector_GetSnapshot(). This
 * is not a wire format and is never written to the SD card.
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
} SnapshotData_t;

#define SNAPSHOT_DATA_CRC_SIZE offsetof(SnapshotData_t, crc32)

#ifdef __cplusplus
}
#endif

#endif
