/* Telemetry file creation, writing, rotation, retention, and reboot recovery. */
#pragma once

#include "../../common/log_record.h"

#include <stdint.h>

/* These helper functions must be called only from Task_SD_Logger. */

/* Mounts storage, recovers session state, and creates a new telemetry file. */
bool TelemetryFileStore_Connect(void);

/* Closes the current telemetry file and unmounts storage after an error. */
void TelemetryFileStore_Disconnect(void);

/* Returns the previous boot's final time, which is added to the current tick. */
uint32_t TelemetryFileStore_GetTimeBaseMs(void);

/* Writes and syncs one batch, rotating first when the 1 MiB limit requires it. */
bool TelemetryFileStore_Write(const LogRecord_t* records, uint32_t record_count);
