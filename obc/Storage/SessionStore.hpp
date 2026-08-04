/* CRC-protected persistent metadata stored in SESSION.BIN. */
#pragma once

#include <stdint.h>

/* Values needed by the telemetry file store between boots and file rotations. */
typedef struct
{
    uint32_t generation;
    uint32_t session_id;
    uint32_t active_file_index;
    uint32_t next_file_index;
    uint32_t last_committed_time_ms;
} SessionMetadata_t;

/* These helpers are used by TelemetryFileStore in Task_SD_Logger's context. */

/* Loads the newest valid SESSION.BIN slot. A missing file is not an error. */
bool SessionStore_Load(SessionMetadata_t* metadata, bool* valid);

/* Writes the metadata to the older of the two SESSION.BIN slots. */
bool SessionStore_Save(SessionMetadata_t* metadata);
