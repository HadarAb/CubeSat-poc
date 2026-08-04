#pragma once

#include <stdint.h>
#include <stdbool.h>
#include "../../common/protocol.h" // PayloadData_t

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief thread safe snapshot buffer for Ground Station telemetry requests.
 */
typedef struct {
    bool valid;
    uint32_t obc_time_ms;
    PayloadData_t data;
} Snapshot;

/**
 * @brief Initializes resources required by the Payload Collector.
 *
 * Creates the priority-inheriting mutex for the telemetry snapshot.
 * Must be called before the FreeRTOS scheduler starts.
 */
void PayloadCollector_Init(void);

/**
 * @brief Main FreeRTOS task loop for the Payload Collector.
 *
 * Polls the I2C nodes at a fixed 500ms interval. Pushes telemetry records
 * to the SD logger queue and updates the live snapshot for Ground Station
 * queries. This function never returns.
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

#ifdef __cplusplus
}
#endif
