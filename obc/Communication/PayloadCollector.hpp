#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../common/i2c/protocol.h"
#include "../../common/snapshot_data.h" // SnapshotData_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief thread safe snapshot buffer for Ground Station telemetry requests.
 */
typedef struct {
    bool valid;
    bool battery_valid;
    uint32_t obc_time_ms;
    SnapshotData_t data;
} Snapshot;

/**
 * @brief Cached communication health published by the collector task.
 */
typedef struct
{
    bool valid;
    bool payload_online;
    bool eps_online;
    uint32_t dropped_frames;
    uint32_t overruns;
    uint32_t payload_i2c_errors;
    uint32_t eps_i2c_errors;
    uint32_t payload_crc_failures;
    uint32_t eps_crc_failures;
} PayloadCollectorStatus_t;

/**
 * @brief Initializes resources required by the Payload Collector.
 *
 * Creates the priority-inheriting mutex for the telemetry snapshot.
 * Must be called before the FreeRTOS scheduler starts.
 */
void PayloadCollector_Init(void);

/**
 * @brief Main FreeRTOS task loop for the Payload/EPS telemetry collector.
 *
 * Checks every known sensor against the state-dependent schedule. Due values
 * are queued for SD storage and update their field in the persistent Ground
 * Station snapshot. This function never returns.
 */
void payload_collector_run(void);

/**
 * @brief Safely reads the latest telemetry snapshot for a given node.
 *
 * @param node_id The ID of the node (e.g., NODE_ID_PAYLOAD)
 * @param out Pointer to the Snapshot struct to copy the data into
 * @return true if the snapshot contains valid data, false otherwise
 */
bool PayloadCollector_GetSnapshot(uint8_t node_id, Snapshot *out);

/**
 * @brief Safely reads the collector's cached node and queue health.
 */
bool PayloadCollector_GetStatus(PayloadCollectorStatus_t* out);

#ifdef __cplusplus
}
#endif
