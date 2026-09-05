// Telemetry file creation, writing, rotation, retention, and reboot recovery.
#pragma once

#include "../../common/log_record.h"

#include <stdint.h>

// These helper functions must be called only from Task_SD_Logger.

// Mounts storage, recovers session state, and creates a new telemetry file.
bool TelemetryFileStore_Connect(void);

// Closes the current telemetry file and unmounts storage after an error.
void TelemetryFileStore_Disconnect(void);

// Writes and syncs one batch, rotating first when the 1 MiB limit requires it.
bool TelemetryFileStore_Write(const LogRecord_t* records, uint32_t record_count);

typedef enum
{
    TELEMETRY_READ_RECORD = 0,
    TELEMETRY_READ_END = 1,
    TELEMETRY_READ_ERROR = 2
} TelemetryReadResult_t;

// Starts an inclusive time-range search. Volume 0 is payload and volume 1 is housekeeping.
bool TelemetryFileStore_BeginFetch(uint8_t volume, uint32_t from_epoch_s, uint32_t to_epoch_s);

// Reads up to capacity sequential records after the binary search found the first match.
TelemetryReadResult_t TelemetryFileStore_ReadChunk(
        LogRecord_t* records, uint32_t capacity, uint32_t* record_count);

// Returns only binary-search midpoint reads, not sequential records sent to the GS.
uint16_t TelemetryFileStore_GetFetchProbeCount(void);

// Ends the snapshot read and closes its read-only file when one is open.
void TelemetryFileStore_EndFetch(void);
