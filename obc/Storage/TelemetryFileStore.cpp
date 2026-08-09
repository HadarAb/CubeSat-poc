/* Owns telemetry files and calls SessionStore for persistent metadata. */
#include "TelemetryFileStore.hpp"

#include "SessionStore.hpp"
#include "fatfs.h"

#include "../../common/crc32.h"
#include "../../common/log_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

/*
 * FatFs Multi-Partition Map:
 * FatFs requires this array when _MULTI_PARTITION is enabled.
 * It maps logical volumes ("0:", "1:") to physical drives and partitions.
 */
extern "C" {
	PARTITION VolToPart[2] = {
			{0, 1}, /* Logical drive "0:" -> Payload */
			{0, 2}  /* Logical drive "1:" -> EPS */
	};
}

namespace
{
constexpr uint32_t TelemetryFileSizeBytes = 1024u * 1024u;
constexpr uint32_t MinimumFreeBytes = 2u * 1024u * 1024u;
constexpr uint32_t SpaceRequiredForNewFile = TelemetryFileSizeBytes + MinimumFreeBytes;
constexpr uint32_t MaximumFileIndex = 9999u;

/*
 * Context structure for a single FatFs volume.
 * Bundles all state variables required to manage files on a specific partition.
 * This allows us to manage multiple drives without duplicating code.
 */
struct VolumeCtx_t {
	const char* volume_path;      /* Mount path, e.g., "0:/" or "1:/" */
	FATFS filesystem;             /* FatFs workspace for this specific volume */
	FIL active_file;              /* The currently open file object */
	bool file_is_open;            /* True if active_file is open and ready to write */
	bool session_initialized;     /* True if we have loaded metadata from SESSION.BIN */
	uint32_t current_file_index;  /* The index (XXXX) of the currently open TLMXXXX.BIN */
	uint32_t current_file_bytes;  /* How many bytes are currently written to active_file */
	SessionMetadata_t session;    /* Metadata for this specific volume */
};

/* Instantiate two independent volume contexts for Payload and EPS. */
VolumeCtx_t vol_payload = { "0:/", {}, {}, false, false, 0u, 0u, {} };
VolumeCtx_t vol_eps     = { "1:/", {}, {}, false, false, 0u, 0u, {} };

/* Accepts only the exact 8.3 filename TLM0001.BIN through TLM9999.BIN. */
bool IsTelemetryFilename(const char* name, uint32_t* index)
{
    if ((name == nullptr)
    		|| (std::strlen(name) != 11u)
			|| (name[0] != 'T')
			|| (name[1] != 'L')
			|| (name[2] != 'M')
			|| (name[7] != '.')
			|| (name[8] != 'B')
			|| (name[9] != 'I')
			|| (name[10] != 'N')) {
        return false;
    }

    // Convert the four filename digits to an integer without sscanf().
    uint32_t parsed = 0u;
    for (uint32_t offset = 3u; offset <= 6u; ++offset) {
        if ((name[offset] < '0') || (name[offset] > '9')) {
            return false;
        }

        parsed = (parsed * 10u) + static_cast<uint32_t>(name[offset] - '0');
    }

    if (parsed == 0u) {
        return false;
    }

    if (index != nullptr) {
        *index = parsed;
    }

    return true;
}

/*
 * Builds a telemetry filename WITH the volume path prefix.
 * e.g., If ctx->volume_path is "0:/", it formats as "0:/TLM0001.BIN".
 * Note: size must be at least 16 to accommodate the path prefix.
 */
void MakeTelemetryFilename(VolumeCtx_t* ctx, uint32_t index, char* filename, size_t size)
{
    std::snprintf(filename, size, "%sTLM%04lu.BIN", ctx->volume_path, static_cast<unsigned long>(index));
}

/*
 * Finds the oldest and newest telemetry file indexes on a SPECIFIC volume.
 * Requires the context pointer to know which partition to scan.
 */
bool ScanFileIndexes(VolumeCtx_t* ctx, uint32_t* lowest_index, uint32_t* highest_index)
{
    if ((lowest_index == nullptr) || (highest_index == nullptr)) {
        return false;
    }

    *lowest_index = 0u;
    *highest_index = 0u;

    DIR directory = {};
    FILINFO info = {};

	// Open the directory of the specific volume (Payload or EPS)
	FRESULT result = f_opendir(&directory, ctx->volume_path);
    if (result != FR_OK) {
        return false;
    }

    for (;;) {
        result = f_readdir(&directory, &info);
        if ((result != FR_OK) || (info.fname[0] == '\0')) {
            break;
        }

        uint32_t index = 0u;
        if (!IsTelemetryFilename(info.fname, &index)) {
            continue;
        }

        if ((*lowest_index == 0u) || (index < *lowest_index)) {
            *lowest_index = index;
        }

        if (index > *highest_index) {
            *highest_index = index;
        }
    }

    const FRESULT close_result = f_closedir(&directory);
    return (result == FR_OK) && (close_result == FR_OK);
}

/*
 * Converts FatFs free clusters on a SPECIFIC volume to a byte count.
 * Prevents one full partition from reporting the other as full.
 */
bool GetFreeBytes(VolumeCtx_t* ctx, uint64_t* free_bytes)
{
    if (free_bytes == nullptr) {
        return false;
    }

    DWORD free_clusters = 0u;
    FATFS* filesystem = nullptr;

	// Check free space exclusively on the requested volume
	if (f_getfree(ctx->volume_path, &free_clusters, &filesystem) != FR_OK) {
		return false;
	}

    // FatFs reports clusters, so convert clusters to sectors and then bytes.
	*free_bytes = static_cast<uint64_t>(free_clusters)
				* static_cast<uint64_t>(filesystem->csize)
				* static_cast<uint64_t>(LOG_SECTOR_SIZE_BYTES);
    return true;
}

/*
 * Deletes the oldest closed telemetry files on THIS volume until space is freed.
 * Operates strictly within the isolated partition context.
 */
bool EnsureSpaceForNewFile(VolumeCtx_t* ctx)
{
    for (;;) {
        uint64_t free_bytes = 0u;
        if (!GetFreeBytes(ctx, &free_bytes)) {
            return false;
        }
        if (free_bytes >= SpaceRequiredForNewFile) {
            return true;
        }

        uint32_t lowest_index = 0u;
        uint32_t highest_index = 0u;
        if (!ScanFileIndexes(ctx, &lowest_index, &highest_index)) {
            return false;
        }
        (void)highest_index;

        // Never delete the file that is currently open for writing on THIS context.
        if ((lowest_index == 0u) || (lowest_index == ctx->current_file_index)) {
            return false;
        }

        // Increased array size to 16 (was 13) to fit prefix like "0:/TLM0001.BIN"
        char filename[16] = {};
        MakeTelemetryFilename(ctx, lowest_index, filename, sizeof(filename));
        if (f_unlink(filename) != FR_OK) {
            return false;
        }
    }
}

/*
 * Loads saved metadata from the volume's unique SESSION.BIN to determine next index.
 */
bool InitializeSession(VolumeCtx_t* ctx)
{
    SessionMetadata_t loaded = {};
    bool loaded_valid = false;

    // Pass the volume path so SessionStore knows which file to load
	if (!SessionStore_Load(ctx->volume_path, &loaded, &loaded_valid)) {
		return false;
	}

    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!ScanFileIndexes(ctx, &lowest_index, &highest_index)) {
        return false;
    }
    (void)lowest_index;

	// Apply metadata to this specific volume's context
	if (loaded_valid) {
		ctx->session = loaded;
		ctx->session.session_id = loaded.session_id + 1u;
	} else {
		ctx->session = {};
		ctx->session.session_id = 1u;
	}

	ctx->session.active_file_index = 0u;

    uint32_t scanned_next = 1u;
    if (highest_index != 0u) {
        scanned_next = highest_index + 1u;
    }

    if (!loaded_valid || (ctx->session.next_file_index < scanned_next)) {
    	ctx->session.next_file_index = scanned_next;
	}

    if (ctx->session.next_file_index == 0u) {
    	ctx->session.next_file_index = 1u;
	}

	ctx->session_initialized = true;
    return true;
}

/*
 * Re-scans file indexes on this specific volume.
 */
bool RefreshNextFileIndex(VolumeCtx_t* ctx)
{
    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!ScanFileIndexes(ctx, &lowest_index, &highest_index)) {
        return false;
    }

    (void)lowest_index;

    if ((highest_index != 0u) && (ctx->session.next_file_index <= highest_index)) {
    	ctx->session.next_file_index = highest_index + 1u;
    }

    return true;
}

/*
 * Closes the previous file on this volume and creates the next one.
 */
bool OpenNewTelemetryFile(VolumeCtx_t* ctx)
{
	if (ctx->file_is_open) {
		const FRESULT close_result = f_close(&ctx->active_file);
		ctx->file_is_open = false;
		ctx->current_file_index = 0u;
		ctx->current_file_bytes = 0u;
		if (close_result != FR_OK) {
			return false;
		}
	}

	if (!EnsureSpaceForNewFile(ctx) || !RefreshNextFileIndex(ctx)) {
		return false;
	}
	if ((ctx->session.next_file_index == 0u) || (ctx->session.next_file_index > MaximumFileIndex)) {
		return false;
	}

	const uint32_t new_index = ctx->session.next_file_index;

	// Buffer size 16 to fit volume prefix
	char filename[16] = {};
	MakeTelemetryFilename(ctx, new_index, filename, sizeof(filename));

	if (f_open(&ctx->active_file, filename, FA_CREATE_NEW | FA_WRITE) != FR_OK) {
		return false;
	}

	ctx->file_is_open = true;
	ctx->current_file_index = new_index;
	ctx->current_file_bytes = 0u;
	ctx->session.active_file_index = new_index;
	ctx->session.next_file_index = new_index + 1u;

	// Save the metadata to this specific volume's SESSION.BIN
	if (!SessionStore_Save(ctx->volume_path, &ctx->session)) {
		(void)f_close(&ctx->active_file);
		ctx->file_is_open = false;
		ctx->current_file_index = 0u;
		ctx->current_file_bytes = 0u;
		return false;
	}

    return true;
}
}

/*
 * Internal helper: mounts a single FatFs partition, loads its independent
 * metadata, and opens its current telemetry file.
 */
static bool ConnectVolume(VolumeCtx_t* ctx)
{
	if (f_mount(&ctx->filesystem, ctx->volume_path, 1u) != FR_OK) {
		return false;
	}

	if (ctx->session_initialized) {
		if (!RefreshNextFileIndex(ctx)) {
			return false;
		}
	} else if (!InitializeSession(ctx)) {
		return false;
	}

	return OpenNewTelemetryFile(ctx);
}

/*
 * Internal helper: safely closes open files and unmounts a single partition.
 */
static void DisconnectVolume(VolumeCtx_t* ctx)
{
	if (ctx->file_is_open) {
		(void)f_close(&ctx->active_file);
	}

	ctx->file_is_open = false;
	ctx->current_file_index = 0u;
	ctx->current_file_bytes = 0u;

	// Passing nullptr unmounts the volume in FatFs
	(void)f_mount(nullptr, ctx->volume_path, 0u);
}

/*
 * Mounts both the Payload (0:/) and EPS (1:/) logical volumes.
 * Returns true only if both partitions successfully mount and prepare files.
 */
bool TelemetryFileStore_Connect(void)
{
	bool payload_ok = ConnectVolume(&vol_payload);
	bool eps_ok = ConnectVolume(&vol_eps);

	return (payload_ok && eps_ok);
}

/*
 * Tears down both volumes completely (called on error).
 */
void TelemetryFileStore_Disconnect(void)
{
	DisconnectVolume(&vol_payload);
	DisconnectVolume(&vol_eps);
}

/*
 * Routing table structure maps a logical Node ID to its physical volume context.
 */
struct RouteEntry_t {
    uint8_t node_id;
    VolumeCtx_t* target_volume;
};

/*
 * Open-Closed Compliant Routing Table:
 * To add new subsystem nodes in the future, simply add a new row here.
 * The routing logic itself is closed for modification.
 */
static const RouteEntry_t RoutingTable[] = {
    { 0x02u, &vol_payload }, /* PAYLOAD_NODE_ID */
    { 0x03u, &vol_eps }      /* EPS_NODE_ID */
};

/*
 * Internal helper: Resolves the target volume context based on the sensor_id.
 * Extracts the Node ID from the high byte of the 16-bit sensor_id (defined in log_record.h).
 * Returns nullptr if the node is unknown.
 */
static VolumeCtx_t* RouteRecord(uint16_t sensor_id)
{
    // Extract the Node ID (top 8 bits) from the sensor_id
    const uint8_t node_id = static_cast<uint8_t>(sensor_id >> 8u);
    const size_t table_size = sizeof(RoutingTable) / sizeof(RoutingTable[0]);

    for (size_t i = 0u; i < table_size; ++i) {
        if (RoutingTable[i].node_id == node_id) {
            return RoutingTable[i].target_volume;
        }
    }
    // Unknown source, discard securely
    return nullptr;
}

/*
 * Gain access to the UART handler defined by CubeMX in usart.c
 * used for debug warnings when routing fails.
 */
extern UART_HandleTypeDef huart2;

/*
 * Writes a mixed batch of records to the SD card.
 * Routes each record to its appropriate volume based on its source node.
 * Rotates files automatically when the 1 MiB limit is reached.
 */
bool TelemetryFileStore_Write(const LogRecord_t* records, uint32_t record_count)
{
    // 1. Safety checks: null pointer, empty batch, or batch too large
    if ((records == nullptr) || (record_count == 0u) || (record_count > LOG_RECORDS_PER_SECTOR)) {
        return false;
    }

    bool eps_written = false;
    bool payload_written = false;
    bool overall_success = true;

    // 2. Process the mixed batch record by record
    for (uint32_t i = 0u; i < record_count; ++i) {

        // RouteRecord examines the top 8 bits to resolve the target volume (Payload or EPS)
        VolumeCtx_t* ctx = RouteRecord(records[i].sensor_id);

        // 3. Handle unknown sources: Alert via UART and skip the record
        if (ctx == nullptr) {
            char warn_msg[64] = {};
            // Format the warning string into the RAM buffer
            int len = std::snprintf(warn_msg, sizeof(warn_msg),
                                    "SD Router Warn: Unknown record! sensor_id: 0x%04X\r\n",
                                    records[i].sensor_id);

            // Transmit the warning over UART with a 100ms timeout
            if (len > 0) {
                HAL_UART_Transmit(&huart2, reinterpret_cast<uint8_t*>(warn_msg), static_cast<uint16_t>(len), 100);
            }
            continue; // Skip writing this unknown record
        }

        // 4. Check if the target volume is offline/disconnected
        if (!ctx->file_is_open) {
            continue;
        }

        // 5. File size management (Rotation)
        // Check if adding this 16-byte record will exceed the 1 MiB limit
        if ((ctx->current_file_bytes + sizeof(LogRecord_t)) > TelemetryFileSizeBytes) {
            if (!OpenNewTelemetryFile(ctx)) {
                overall_success = false;
                continue; // Failed to rotate file, skip this record
            }
        }

        // 6. Write exactly one record to the FatFs RAM buffer
        UINT bytes_written = 0u;
        FRESULT result = f_write(&ctx->active_file, &records[i], sizeof(LogRecord_t), &bytes_written);

        if ((result == FR_OK) && (bytes_written == sizeof(LogRecord_t))) {
            ctx->current_file_bytes += sizeof(LogRecord_t);

            // Flag which volume actually received data so we can selectively sync later
            if (ctx == &vol_eps) {
                eps_written = true;
            } else if (ctx == &vol_payload) {
                payload_written = true;
            }
        } else {
            overall_success = false; // Disk error on write
        }
    }

    // 7. Commit changes to physical SD Card
    // Synchronize and save session metadata ONLY for touched volumes.
    // This minimizes blocking time and SD card wear.
    if (eps_written) {
        (void)f_sync(&vol_eps.active_file);
        (void)SessionStore_Save(vol_eps.volume_path, &vol_eps.session);
    }
    if (payload_written) {
        (void)f_sync(&vol_payload.active_file);
        (void)SessionStore_Save(vol_payload.volume_path, &vol_payload.session);
    }

    return overall_success;
}
