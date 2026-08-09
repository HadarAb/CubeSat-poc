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

/* Loads the newest valid SESSION.BIN slot for a specific volume (e.g., "0:/"). */
bool SessionStore_Load(const char* vol_path, SessionMetadata_t* metadata, bool* valid);

/* Writes the metadata to the older slot in the specific volume's SESSION.BIN. */
bool SessionStore_Save(const char* vol_path, SessionMetadata_t* metadata);
