// Owns telemetry files and calls SessionStore for persistent metadata.
#include "TelemetryFileStore.hpp"

#include "SessionStore.hpp"
#include "fatfs.h"

#include "../../common/crc32.h"
#include "../../common/log_record.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

static FATFS sd_filesystem;

namespace
{
constexpr uint32_t TelemetryFileSizeBytes = 1024u * 1024u;
constexpr uint32_t MinimumFreeBytes = 2u * 1024u * 1024u;
constexpr uint32_t SpaceRequiredForNewFile = TelemetryFileSizeBytes + MinimumFreeBytes;
constexpr uint32_t MaximumFileIndex = 9999u;

/*
 * Context structure for one telemetry directory.
 * Bundles all state needed to manage files inside a single directory on the
 * shared filesystem. This lets both directories reuse one implementation.
 */
struct DirectoryCtx_t {
	const char* directory_path; // Directory name, e.g. "PAYLOAD" or "EPS"
	FIL active_file; // The currently open file object
	bool file_is_open; // True if active_file is open and ready to write
	bool session_initialized; // True if we have loaded metadata from SESSION.BIN
	uint32_t current_file_index; // The index (XXXX) of the currently open TLMXXXX.BIN
	uint32_t current_file_bytes; // How many bytes are currently written to active_file
	uint32_t lowest_file_index; // Lowest telemetry file found during recovery
	uint32_t highest_file_index; // Highest telemetry file created or found during recovery
	SessionMetadata_t session; // Metadata for this specific directory
};

// Instantiate two independent directory contexts for Payload and EPS.
DirectoryCtx_t dir_payload = { "PAYLOAD", {}, false, false, 0u, 0u, 0u, 0u, {} };
DirectoryCtx_t dir_eps     = { "EPS",     {}, false, false, 0u, 0u, 0u, 0u, {} };

struct FetchCursor_t {
    DirectoryCtx_t* directory;
    FIL reader;
    bool active;
    bool reader_is_open;
    bool reader_uses_active_file;
    bool first_match_found;
    uint32_t next_file_index;
    uint32_t highest_file_index;
    uint32_t open_file_index;
    uint32_t next_record_index;
    uint32_t record_count;
    uint32_t snapshot_file_index;
    uint32_t snapshot_record_count;
    uint32_t from_epoch_s;
    uint32_t to_epoch_s;
    uint16_t probe_count;
};

FetchCursor_t fetch_cursor = {};

enum class OpenRangeFileResult : uint8_t {
    Found,
    End,
    Error
};

// Accepts only the exact 8.3 filename TLM0001.BIN through TLM9999.BIN.
bool is_telemetry_filename(const char* name, uint32_t* index)
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
 * creates a file name just a sstring
 * Builds a telemetry filename WITH the directory prefix.
 * e.g. If ctx->directory_path is "PAYLOAD", it formats as "PAYLOAD/TLM0001.BIN".
 * Note: size must be at least 20 to accommodate the directory prefix.
 *
 */
void make_telemetry_filename(DirectoryCtx_t* ctx, uint32_t index, char* filename, size_t size)
{
    std::snprintf(filename, size, "%s/TLM%04lu.BIN", ctx->directory_path, static_cast<unsigned long>(index));
}

/*
 * Finds the oldest and newest telemetry file indexes in ONE directory.
 * there are two folders inside each folder there are bin files in each
 * there are records
 * Requires the context pointer to know which directory to scan.
 */
bool scan_file_indexes(DirectoryCtx_t* ctx, uint32_t* lowest_index, uint32_t* highest_index)
{
    if ((lowest_index == nullptr) || (highest_index == nullptr)) {
        return false;
    }

    *lowest_index = 0u;
    *highest_index = 0u;
    ctx->lowest_file_index = 0u;
    ctx->highest_file_index = 0u;

    DIR directory = {};
    FILINFO info = {};

	// Open the directory of the specific directory (Payload or EPS)
    // and put it on directory verb on ram
	FRESULT result = f_opendir(&directory, ctx->directory_path);
    if (result != FR_OK) {
        return false;
    }

    for (;;) {
    	// take the next bin file
        result = f_readdir(&directory, &info);
        if ((result != FR_OK) || (info.fname[0] == '\0')) {
            break;
        }

        uint32_t index = 0u;
        // take its name
        if (!is_telemetry_filename(info.fname, &index)) {
            continue;
        }

        //save lowest index
        if ((*lowest_index == 0u) || (index < *lowest_index)) {
            *lowest_index = index;
        }

        //save highest index
        if (index > *highest_index) {
            *highest_index = index;
        }

        if (info.fsize > 0u) {
            if ((ctx->lowest_file_index == 0u) || (index < ctx->lowest_file_index)) {
                ctx->lowest_file_index = index;
            }
            if (index > ctx->highest_file_index) {
                ctx->highest_file_index = index;
            }
        }
    }

    const FRESULT close_result = f_closedir(&directory);
    return (result == FR_OK) && (close_result == FR_OK);
}
/*
 * Called when FETCH is done reading the current file.
 * If it was a normal read-only file, close the reader.
 * If it was also the active writer file, do not close it.
 * Move the shared file position back to the end so new data can be appended.
 */
bool close_fetch_reader(void)
{
	// is there a cursor to a file we already read ?
    if (!fetch_cursor.reader_is_open) {
        return true;
    }

    FRESULT result = FR_OK;
    if (fetch_cursor.reader_uses_active_file) {
        // Check if the file used for reading is still the current writer file.
        const bool snapshot_is_still_active = fetch_cursor.directory->file_is_open &&
                (fetch_cursor.directory->current_file_index == fetch_cursor.open_file_index);
        // Restore the shared cursor to the end so the next write appends data.
        if (snapshot_is_still_active) {
            result = f_lseek(&fetch_cursor.directory->active_file,
                    f_size(&fetch_cursor.directory->active_file));
        }
    } else {
        result = f_close(&fetch_cursor.reader);
    }

    fetch_cursor.reader_is_open = false;
    fetch_cursor.reader_uses_active_file = false;
    fetch_cursor.open_file_index = 0u;
    fetch_cursor.next_record_index = 0u;
    fetch_cursor.record_count = 0u;
    return result == FR_OK;
}

// seek record by index and put it in record verb you provided
bool read_record_at(uint32_t record_index, LogRecord_t* record)
{
	// check is there a cursor
    if ((record == nullptr) || !fetch_cursor.reader_is_open) {
        return false;
    }

    // how much indexes we need to jump * record size
    const FSIZE_t offset = static_cast<FSIZE_t>(record_index)
    						* static_cast<FSIZE_t>(sizeof(LogRecord_t));

    FIL* source = &fetch_cursor.reader;
    FSIZE_t writer_end = 0u;
    //check we are on the writing file
    if (fetch_cursor.reader_uses_active_file) {
        if (!fetch_cursor.directory->file_is_open ||
                (fetch_cursor.directory->current_file_index != fetch_cursor.open_file_index)) {
            return false;
        }
        // current file we use
        source = &fetch_cursor.directory->active_file;
        // get its size so later we can get to the end of it to write again
        writer_end = f_size(source);
    }

    if (f_lseek(source, offset) != FR_OK) {
        return false;
    }

    UINT bytes_read = 0u;
    // read from the bin file exactly the file at the index
    // put what we found inside record
    const FRESULT read_result = f_read(source, record, sizeof(LogRecord_t), &bytes_read);
    if (fetch_cursor.reader_uses_active_file && (f_lseek(source, writer_end) != FR_OK)) {
        return false;
    }
    return (read_result == FR_OK) && (bytes_read == sizeof(LogRecord_t));
}

/*
 * If the snapshot file stops being the active writer file during FETCH,
 * switch cleanly to a normal read-only reader and continue from the same record.
 */
bool switch_rotated_snapshot_to_reader(void)
{
    if (!fetch_cursor.reader_is_open || !fetch_cursor.reader_uses_active_file) {
        return true;
    }
    if (fetch_cursor.directory->file_is_open &&
            (fetch_cursor.directory->current_file_index == fetch_cursor.open_file_index)) {
        return true;
    }

    char filename[32] = {};
    make_telemetry_filename(fetch_cursor.directory, fetch_cursor.open_file_index,
            filename, sizeof(filename));
    fetch_cursor.reader_is_open = false;
    fetch_cursor.reader_uses_active_file = false;
    if (f_open(&fetch_cursor.reader, filename, FA_READ) != FR_OK) {
        return false;
    }
    fetch_cursor.reader_is_open = true;

    const FSIZE_t file_size = f_size(&fetch_cursor.reader);
    const FSIZE_t snapshot_size = static_cast<FSIZE_t>(fetch_cursor.snapshot_record_count)
            * sizeof(LogRecord_t);
    if (((file_size % sizeof(LogRecord_t)) != 0u) ||
            (file_size < snapshot_size)) {
        return false;
    }
    return f_lseek(&fetch_cursor.reader, static_cast<FSIZE_t>(fetch_cursor.next_record_index)
            * sizeof(LogRecord_t)) == FR_OK;
}

// there is a global starting time (from_epoch)
// this function seeks for the first record that its time is >= from_epoch
// and start global verb and global offset to start from this file
bool seek_to_first_requested_record(void)
{
    uint32_t low = 0u;
    // how many records there are
    uint32_t high = fetch_cursor.record_count;

    while (low < high) {
        const uint32_t middle = low + ((high - low) / 2u);
        LogRecord_t record = {};
        // reads record from bin and put it in record
        if (!read_record_at(middle, &record)) {
            return false;
        }

        if (fetch_cursor.probe_count != UINT16_MAX) {
            ++fetch_cursor.probe_count;
        }
        // check if record time is what we need
        if (record.epoch_s < fetch_cursor.from_epoch_s) {
            low = middle + 1u;
        } else {
            high = middle;
        }
    }


    fetch_cursor.next_record_index = low;
    if (fetch_cursor.reader_uses_active_file) {
        return true;
    }

    const FSIZE_t offset = static_cast<FSIZE_t>(low) *
    					static_cast<FSIZE_t>(sizeof(LogRecord_t));
    return f_lseek(&fetch_cursor.reader, offset) == FR_OK;
}

// opens next bin file finds its first and last record and updates global verbs
// should be used when you get to the end of the file and you need to move to the next one
OpenRangeFileResult open_next_range_file(void)
{
	// check if next file index is still in range
    while (fetch_cursor.next_file_index <= fetch_cursor.highest_file_index) {
        const uint32_t file_index = fetch_cursor.next_file_index;
        ++fetch_cursor.next_file_index;

        // check if current file is the file that writer uses
        const bool use_active_file = (file_index == fetch_cursor.snapshot_file_index) &&
                fetch_cursor.directory->file_is_open &&
                (fetch_cursor.directory->current_file_index == file_index);
        // work with already open file
        if (use_active_file) {
            fetch_cursor.reader_is_open = true;
            fetch_cursor.reader_uses_active_file = true;
            fetch_cursor.open_file_index = file_index;
            // open connection to the index we point right now
        } else {
            char filename[32] = {};
            make_telemetry_filename(fetch_cursor.directory, file_index, filename,
                    sizeof(filename));
            const FRESULT open_result = f_open(&fetch_cursor.reader, filename, FA_READ);
            if (open_result == FR_NO_FILE) {
                continue;
            }
            if (open_result != FR_OK) {
                return OpenRangeFileResult::Error;
            }
            fetch_cursor.reader_is_open = true;
            fetch_cursor.reader_uses_active_file = false;
            fetch_cursor.open_file_index = file_index;
        }

        //checks all records are complete and not corrupted
        FSIZE_t file_size = 0u;
        if (use_active_file) {
            file_size = f_size(&fetch_cursor.directory->active_file);
        } else {
            file_size = f_size(&fetch_cursor.reader);
        }
        //can the file size be divided by log recored cleanly
        if ((file_size % sizeof(LogRecord_t)) != 0u) {
            (void)close_fetch_reader();
            return OpenRangeFileResult::Error;
        }
        // how many records corrently exist
        fetch_cursor.record_count = static_cast<uint32_t>(file_size / sizeof(LogRecord_t));
        if (file_index == fetch_cursor.snapshot_file_index) {
            if (fetch_cursor.record_count < fetch_cursor.snapshot_record_count) {
                (void)close_fetch_reader();
                return OpenRangeFileResult::Error;
            }
            fetch_cursor.record_count = fetch_cursor.snapshot_record_count;
        }
        if (fetch_cursor.record_count == 0u) {
            if (!close_fetch_reader()) {
                return OpenRangeFileResult::Error;
            }
            continue;
        }

        LogRecord_t first = {};
        LogRecord_t last = {};
        // try to fetch last and first records
        if (!read_record_at(0u, &first) ||
        		!read_record_at(fetch_cursor.record_count - 1u, &last)) {
            (void)close_fetch_reader();
            return OpenRangeFileResult::Error;
        }

        if (last.epoch_s < fetch_cursor.from_epoch_s) {
            if (!close_fetch_reader()) {
                return OpenRangeFileResult::Error;
            }
            continue;
        }

        if (first.epoch_s > fetch_cursor.to_epoch_s) {
            (void)close_fetch_reader();
            return OpenRangeFileResult::End;
        }

        if (!fetch_cursor.first_match_found) {
            if (!seek_to_first_requested_record()) {
                (void)close_fetch_reader();
                return OpenRangeFileResult::Error;
            }

            if (fetch_cursor.next_record_index == fetch_cursor.record_count) {
                if (!close_fetch_reader()) {
                    return OpenRangeFileResult::Error;
                }
                continue;
            }

            fetch_cursor.first_match_found = true;
        } else {
            fetch_cursor.next_record_index = 0u;
            if (!fetch_cursor.reader_uses_active_file && (f_lseek(&fetch_cursor.reader, 0u) != FR_OK)) {
                (void)close_fetch_reader();
                return OpenRangeFileResult::Error;
            }
        }

        return OpenRangeFileResult::Found;
    }

    return OpenRangeFileResult::End;
}

/*
 * Converts FatFs free clusters to a byte count.
 * NOTE: free space is card-wide and shared by both directories.
 */
bool get_free_bytes(DirectoryCtx_t* ctx, uint64_t* free_bytes)
{
    if (free_bytes == nullptr) {
        return false;
    }

    DWORD free_clusters = 0u;
    FATFS* filesystem = nullptr;

	// Free space is reported for the whole filesystem, not per directory
	if (f_getfree("", &free_clusters, &filesystem) != FR_OK) {
		return false;
	}

    // FatFs reports clusters, so convert clusters to sectors and then bytes.
	*free_bytes = static_cast<uint64_t>(free_clusters)
				* static_cast<uint64_t>(filesystem->csize)
				* static_cast<uint64_t>(LOG_SECTOR_SIZE_BYTES);
    return true;
}

/*
 * Deletes the oldest closed telemetry files in THIS directory until space is freed.
 * Note both directories draw on the same shared free space.
 */
bool ensure_space_for_new_file(DirectoryCtx_t* ctx)
{
    for (;;) {
        uint64_t free_bytes = 0u;
        if (!get_free_bytes(ctx, &free_bytes)) {
            return false;
        }
        if (free_bytes >= SpaceRequiredForNewFile) {
            return true;
        }

        uint32_t lowest_index = 0u;
        uint32_t highest_index = 0u;
        if (!scan_file_indexes(ctx, &lowest_index, &highest_index)) {
            return false;
        }
        (void)highest_index;

        // Never delete a file that is currently open for writing or reading.
        bool fetch_uses_oldest = false;
        if (fetch_cursor.active && (fetch_cursor.directory == ctx)) {
            fetch_uses_oldest = lowest_index <= fetch_cursor.highest_file_index;
        }

        if ((lowest_index == 0u) || (lowest_index == ctx->current_file_index)
							|| fetch_uses_oldest) {
            return false;
        }

        // Increased array size to 16 (was 13) to fit prefix like "0:/TLM0001.BIN"
        //finds the lowest file and deletes it
        char filename[16] = {};
        make_telemetry_filename(ctx, lowest_index, filename, sizeof(filename));
        if (f_unlink(filename) != FR_OK) {
            return false;
        }
    }
}

/*
 * Loads saved metadata from the directory's unique SESSION.BIN to determine next index.
 */
bool initialize_session(DirectoryCtx_t* ctx)
{
    SessionMetadata_t loaded = {};
    bool loaded_valid = false;

    // Pass the directory path so SessionStore knows which file to load
	if (!SessionStore_Load(ctx->directory_path, &loaded, &loaded_valid)) {
		return false;
	}

    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!scan_file_indexes(ctx, &lowest_index, &highest_index)) {
        return false;
    }

	// Apply metadata to this specific directory's context
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
 * Re scans file indexes on this specific directory.
 */
bool refresh_next_file_index(DirectoryCtx_t* ctx)
{
    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!scan_file_indexes(ctx, &lowest_index, &highest_index)) {
        return false;
    }

    if ((highest_index != 0u) && (ctx->session.next_file_index <= highest_index)) {
    	ctx->session.next_file_index = highest_index + 1u;
    }

    return true;
}

/*
 * Closes the previous file on this directory and creates the next one.
 */
bool open_new_telemetry_file(DirectoryCtx_t* ctx)
{
	//if file is open close it
	if (ctx->file_is_open) {
		const FRESULT close_result = f_close(&ctx->active_file);
		ctx->file_is_open = false;
		ctx->current_file_index = 0u;
		ctx->current_file_bytes = 0u;
		if (close_result != FR_OK) {
			return false;
		}
	}

	// check there is space for new file
	if (!ensure_space_for_new_file(ctx)) {
		return false;
	}
	if ((ctx->session.next_file_index == 0u) || (ctx->session.next_file_index > MaximumFileIndex)) {
		return false;
	}

	const uint32_t new_index = ctx->session.next_file_index;

	// Buffer size 32 to fit directory prefix
	char filename[32] = {};
	// create the file name
	make_telemetry_filename(ctx, new_index, filename, sizeof(filename));

	// creates the new file
	if (f_open(&ctx->active_file, filename, FA_CREATE_NEW | FA_READ | FA_WRITE) != FR_OK) {
		return false;
	}

	ctx->file_is_open = true;
	ctx->current_file_index = new_index;
	ctx->current_file_bytes = 0u;
	ctx->session.active_file_index = new_index;
	ctx->session.next_file_index = new_index + 1u;

	// Save the metadata to this specific directory's SESSION.BIN
	if (!SessionStore_Save(ctx->directory_path, &ctx->session)) {
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
 * Internal helper: prepares one directory, loads its independent
 * metadata, and opens its current telemetry file.
 */
static bool connect_directory(DirectoryCtx_t* ctx)
{
	if (ctx->session_initialized) {
		if (!refresh_next_file_index(ctx)) {
			return false;
		}
	} else if (!initialize_session(ctx)) {
		return false;
	}

	return open_new_telemetry_file(ctx);
}

/*
 * Internal helper: safely closes open files for a single directory.
 */
static void disconnect_directory(DirectoryCtx_t* ctx)
{
	if (ctx->file_is_open) {
		(void)f_close(&ctx->active_file);
	}

	ctx->file_is_open = false;
	ctx->current_file_index = 0u;
	ctx->current_file_bytes = 0u;
}


/*
 * tries to mount the sd card
 * checks if there are two directories eps and payload
 * connects to them and save the connection in global verbs*/
bool TelemetryFileStore_Connect(void)
{
    // Mount the entire SD card once
    if (f_mount(&sd_filesystem, "", 1u) != FR_OK) {
        return false;
    }

    // Create the directories
    // FR_EXIST means "it's already there", which is OK
    FRESULT r_payload = f_mkdir("PAYLOAD");
    if (r_payload != FR_OK && r_payload != FR_EXIST) {
        return false;
    }

    FRESULT r_eps = f_mkdir("EPS");
    if (r_eps != FR_OK && r_eps != FR_EXIST) {
        return false;
    }

    // Connect the contexts (this will load metadata and open files inside the directories)
    bool payload_ok = connect_directory(&dir_payload);
    bool eps_ok = connect_directory(&dir_eps);

    return (payload_ok && eps_ok);
}

/*
 * Tears down both directories completely (called on error).
 * Disconnect both connections and try to mount the sd card again
 */
void TelemetryFileStore_Disconnect(void)
{
    TelemetryFileStore_EndFetch();
	disconnect_directory(&dir_payload);
	disconnect_directory(&dir_eps);

    // Unmount the single file system
    (void)f_mount(nullptr, "", 0u);
}


/*
 * Routing table structure maps a logical Node ID to its directory context.
 */
struct RouteEntry_t {
    uint8_t node_id;
    DirectoryCtx_t* target_directory;
};

/*
 * Open Closed Compliant Routing Table:
 * To add new subsystem nodes in the future, simply add a new row here.
 * The routing logic itself is closed for modification.
 */
static const RouteEntry_t RoutingTable[] = {
    { 0x02u, &dir_payload }, // PAYLOAD_NODE_ID
    { 0x03u, &dir_eps } // EPS_NODE_ID
};

/*
 * get node id and return the directory this node sensor id came from
 */
static DirectoryCtx_t* route_record(uint16_t node_sensor_id)
{
    // Extract the Node ID (top 8 bits) from the sensor_id
    const uint8_t node_id = static_cast<uint8_t>(node_sensor_id >> 8u);
    const size_t table_size = sizeof(RoutingTable) / sizeof(RoutingTable[0]);

    for (size_t i = 0u; i < table_size; ++i) {
        if (RoutingTable[i].node_id == node_id) {
            return RoutingTable[i].target_directory;
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
 * Routes each record to its appropriate directory based on its source node.
 * Rotates files automatically when the 1 MiB limit is reached.
 */
bool TelemetryFileStore_Write(const LogRecord_t* records, uint32_t record_count)
{
    // Safety checks: null pointer, empty batch, or batch too large
    if ((records == nullptr) || (record_count == 0u) || (record_count > LOG_RECORDS_PER_SECTOR)) {
        return false;
    }

    bool eps_written = false;
    bool payload_written = false;
    bool overall_success = true;

    // Process the mixed batch record by record
    for (uint32_t i = 0u; i < record_count; ++i) {

        //checks where should we save the next record
    	DirectoryCtx_t* ctx = route_record(records[i].sensor_id);

        // Handle unknown sources: Alert via UART and skip the record
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

        // Check if the target directory is offline/disconnected
        if (!ctx->file_is_open) {
            overall_success = false;
            continue;
        }

        // File size management (Rotation)
        // Check if adding this 16-byte record will exceed the 1 MiB limit
        if ((ctx->current_file_bytes + sizeof(LogRecord_t)) > TelemetryFileSizeBytes) {
            if (!open_new_telemetry_file(ctx)) {
                overall_success = false;
                continue; // Failed to rotate file, skip this record
            }
        }

        // f_tell where are we in the file != end of the file
        // f_lseek move curser to the end
        // we try to write new records so we need to be at the end of the file
        if ((f_tell(&ctx->active_file) != f_size(&ctx->active_file)) &&
                (f_lseek(&ctx->active_file, f_size(&ctx->active_file)) != FR_OK)) {
            overall_success = false;
            continue;
        }
        UINT bytes_written = 0u;
        FRESULT result = f_write(&ctx->active_file, &records[i], sizeof(LogRecord_t),
                &bytes_written);

        if ((result == FR_OK) && (bytes_written == sizeof(LogRecord_t))) {
            if ((ctx->lowest_file_index == 0u) ||
                    (ctx->current_file_index < ctx->lowest_file_index)) {
                ctx->lowest_file_index = ctx->current_file_index;
            }
            if (ctx->current_file_index > ctx->highest_file_index) {
                ctx->highest_file_index = ctx->current_file_index;
            }
            ctx->current_file_bytes += sizeof(LogRecord_t);

            // Flag which directory actually received data so we can selectively sync later
            if (ctx == &dir_eps) {
                eps_written = true;
            } else if (ctx == &dir_payload) {
                payload_written = true;
            }
        } else {
            overall_success = false; // Disk error on write
        }
    }

    // Commit changes to physical SD Card
    // Synchronize and save session metadata ONLY for touched directories.
    // This minimizes blocking time and SD card wear.
    if (eps_written) {
        (void)f_sync(&dir_eps.active_file);
        (void)SessionStore_Save(dir_eps.directory_path, &dir_eps.session);
    }
    if (payload_written) {
        (void)f_sync(&dir_payload.active_file);
        (void)SessionStore_Save(dir_payload.directory_path, &dir_payload.session);
    }

    return overall_success;
}

/*
 * connects to the correct directory ,
 * and updates global cursor values , so it initialize search .
 */
bool TelemetryFileStore_BeginFetch(uint8_t volume, uint32_t from_epoch_s, uint32_t to_epoch_s)
{
    if (fetch_cursor.active || (from_epoch_s > to_epoch_s) || (volume > 1u)) {
        return false;
    }

    //what directory to search on
    DirectoryCtx_t* directory = nullptr;
    if (volume == 0u) {
        directory = &dir_payload;
    } else {
        directory = &dir_eps;
    }
    // Flush the writer so the fetch snapshot includes the latest saved records.
    if (!directory->file_is_open || (f_sync(&directory->active_file) != FR_OK)) {
        return false;
    }

    const FSIZE_t active_file_size = f_size(&directory->active_file);
    // A partial 16-byte record means that this telemetry file is damaged.
    if ((active_file_size % sizeof(LogRecord_t)) != 0u) {
        return false;
    }
    directory->current_file_bytes = static_cast<uint32_t>(active_file_size);

    uint32_t snapshot_file_index = 0u;
    uint32_t snapshot_record_count = 0u;
    uint32_t snapshot_highest_index = directory->highest_file_index;
    if (active_file_size > 0u) {
        snapshot_file_index = directory->current_file_index;
        snapshot_record_count = static_cast<uint32_t>(active_file_size / sizeof(LogRecord_t));
        snapshot_highest_index = snapshot_file_index;
    }

    const uint32_t lowest_index = directory->lowest_file_index;
    const uint32_t highest_index = directory->highest_file_index;

    // update cursor
    fetch_cursor = {};
    fetch_cursor.directory = directory;
    fetch_cursor.active = true;
    fetch_cursor.snapshot_file_index = snapshot_file_index;
    fetch_cursor.snapshot_record_count = snapshot_record_count;
    if ((lowest_index == 0u) || (lowest_index > snapshot_highest_index)) {
        fetch_cursor.next_file_index = 1u;
        fetch_cursor.highest_file_index = 0u;
    } else {
        fetch_cursor.next_file_index = lowest_index;
        fetch_cursor.highest_file_index = highest_index;
        if (snapshot_highest_index < highest_index) {
            fetch_cursor.highest_file_index = snapshot_highest_index;
        }
    }
    fetch_cursor.from_epoch_s = from_epoch_s;
    fetch_cursor.to_epoch_s = to_epoch_s;
    return true;
}

/*
 * checks if we finished the current file then move to the next file
 * and searches for the next valid record that is in epoch range
 * also the record that was found will be saved in record out side pointer */
TelemetryReadResult_t TelemetryFileStore_ReadChunk(
        LogRecord_t* records, uint32_t capacity, uint32_t* record_count)
{
    if (!fetch_cursor.active || (records == nullptr) || (record_count == nullptr)
            || (capacity == 0u)) {
        return TELEMETRY_READ_ERROR;
    }

    *record_count = 0u;

    for (;;) {
        if (!switch_rotated_snapshot_to_reader()) {
            return TELEMETRY_READ_ERROR;
        }

        if (!fetch_cursor.reader_is_open) {
        	// open next file
            const OpenRangeFileResult open_result = open_next_range_file();
            if (open_result == OpenRangeFileResult::End) {
                return TELEMETRY_READ_END;
            }
            if (open_result == OpenRangeFileResult::Error) {
                return TELEMETRY_READ_ERROR;
            }
        }

        if (fetch_cursor.next_record_index >= fetch_cursor.record_count) {
            if (!close_fetch_reader()) {
                return TELEMETRY_READ_ERROR;
            }
            continue;
        }

        // Read only what remains in this file and what fits in the output chunk.
        const uint32_t available = fetch_cursor.record_count - fetch_cursor.next_record_index;
        const uint32_t wanted = capacity - *record_count;

        uint32_t read_count = 0u;
        if (available < wanted) {
            read_count = available;
        } else {
            read_count = wanted;
        }

        const FSIZE_t offset = static_cast<FSIZE_t>(fetch_cursor.next_record_index)
                * sizeof(LogRecord_t);

        FIL* source = &fetch_cursor.reader;

        if (fetch_cursor.reader_uses_active_file) {
            source = &fetch_cursor.directory->active_file;
        }

        FSIZE_t writer_end = 0u;

        if (fetch_cursor.reader_uses_active_file) {
            writer_end = f_size(source);
        }

        if ((f_tell(source) != offset) && (f_lseek(source, offset) != FR_OK)) {
            return TELEMETRY_READ_ERROR;
        }

        // One FatFs read now returns several records instead of one record.
        UINT bytes_read = 0u;
        const UINT bytes_requested = static_cast<UINT>(read_count * sizeof(LogRecord_t));
        const FRESULT read_result = f_read(source, &records[*record_count],
                bytes_requested, &bytes_read);

        bool writer_restored = true;
        if (fetch_cursor.reader_uses_active_file) {
            writer_restored = f_lseek(source, writer_end) == FR_OK;
        }

        if ((read_result != FR_OK) || (bytes_read != bytes_requested) || !writer_restored) {
            return TELEMETRY_READ_ERROR;
        }

        // Save the next snapshot index before allowing normal writes again.
        const uint32_t first_read_index = *record_count;
        fetch_cursor.next_record_index += read_count;

        // Keep only records inside the inclusive requested time range.
        for (uint32_t i = 0u; i < read_count; ++i) {
            const LogRecord_t record = records[first_read_index + i];
            if (record.epoch_s < fetch_cursor.from_epoch_s) {
                continue;
            }
            if (record.epoch_s > fetch_cursor.to_epoch_s) {
                return TELEMETRY_READ_END;
            }
            records[*record_count] = record;
            ++(*record_count);
        }

        if (*record_count == capacity) {
            return TELEMETRY_READ_RECORD;
        }
    }
}

/*
 * How many records did the binary search inspect/check before finding the starting position.*/
uint16_t TelemetryFileStore_GetFetchProbeCount(void)
{
    return fetch_cursor.probe_count;
}
/*
 * reset cursor
 * */
void TelemetryFileStore_EndFetch(void)
{
    (void)close_fetch_reader();
    fetch_cursor = {};
}
