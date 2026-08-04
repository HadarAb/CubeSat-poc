/* Owns telemetry files and calls SessionStore for persistent metadata. */
#include "TelemetryFileStore.hpp"

#include "SessionStore.hpp"
#include "fatfs.h"

#include "../../common/crc32.h"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace
{
constexpr uint32_t SectorSizeBytes = 512u;
constexpr uint32_t RecordsPerSector = 16u;
constexpr uint32_t TelemetryFileSizeBytes = 1024u * 1024u;
constexpr uint32_t MinimumFreeBytes = 2u * 1024u * 1024u;
constexpr uint32_t SpaceRequiredForNewFile = TelemetryFileSizeBytes + MinimumFreeBytes;
constexpr uint32_t MaximumFileIndex = 9999u;

static_assert((RecordsPerSector * sizeof(LogRecord_t)) == SectorSizeBytes, "One logger batch must occupy exactly one sector");

FIL telemetry_file = {};
bool telemetry_file_open = false;
bool session_initialized = false;
uint32_t current_file_index = 0u;
uint32_t current_file_bytes = 0u;
uint32_t time_base_ms = 0u;
SessionMetadata_t session = {};

/* Accepts only the exact 8.3 filename TLM0001.BIN through TLM9999.BIN. */
bool IsTelemetryFilename(const char* name, uint32_t* index)
{
    if ((name == nullptr) || (std::strlen(name) != 11u) || (name[0] != 'T') || (name[1] != 'L') || (name[2] != 'M') || (name[7] != '.') || (name[8] != 'B') || (name[9] != 'I') || (name[10] != 'N'))
    {
        return false;
    }

    // Convert the four filename digits to an integer without sscanf().
    uint32_t parsed = 0u;
    for (uint32_t offset = 3u; offset <= 6u; ++offset)
    {
        if ((name[offset] < '0') || (name[offset] > '9'))
        {
            return false;
        }
        parsed = (parsed * 10u) + static_cast<uint32_t>(name[offset] - '0');
    }

    if (parsed == 0u)
    {
        return false;
    }

    if (index != nullptr)
    {
        *index = parsed;
    }
    return true;
}

/* Builds an 8.3 telemetry filename from its numeric index. */
void MakeTelemetryFilename(uint32_t index, char* filename, size_t size)
{
    std::snprintf(filename, size, "TLM%04lu.BIN", static_cast<unsigned long>(index));
}

/* Finds the oldest and newest telemetry file indexes on the mounted card. */
bool ScanFileIndexes(uint32_t* lowest_index, uint32_t* highest_index)
{
    if ((lowest_index == nullptr) || (highest_index == nullptr))
    {
        return false;
    }

    *lowest_index = 0u;
    *highest_index = 0u;

    DIR directory = {};
    FILINFO info = {};
    FRESULT result = f_opendir(&directory, USERPath);
    if (result != FR_OK)
    {
        return false;
    }

    for (;;)
    {
        result = f_readdir(&directory, &info);
        if ((result != FR_OK) || (info.fname[0] == '\0'))
        {
            break;
        }

        uint32_t index = 0u;
        if (!IsTelemetryFilename(info.fname, &index))
        {
            continue;
        }

        if ((*lowest_index == 0u) || (index < *lowest_index))
        {
            *lowest_index = index;
        }
        if (index > *highest_index)
        {
            *highest_index = index;
        }
    }

    const FRESULT close_result = f_closedir(&directory);
    return (result == FR_OK) && (close_result == FR_OK);
}

/* Checks a telemetry record before its timestamp is used for reboot recovery. */
bool RecordCrcIsValid(const LogRecord_t& record)
{
    const uint32_t expected_crc = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&record), LOG_RECORD_CRC_SIZE);
    return expected_crc == record.crc32;
}

/* Reads backwards through the final sector to find the newest complete record. */
bool ReadLastValidTimestamp(uint32_t file_index, uint32_t* timestamp, bool* found)
{
    if ((timestamp == nullptr) || (found == nullptr))
    {
        return false;
    }

    *found = false;
    char filename[13] = {};
    MakeTelemetryFilename(file_index, filename, sizeof(filename));

    FIL file = {};
    FRESULT result = f_open(&file, filename, FA_READ);
    if (result != FR_OK)
    {
        return false;
    }

    const uint32_t record_count = static_cast<uint32_t>(f_size(&file) / sizeof(LogRecord_t));
    uint32_t attempts = record_count;
    if (attempts > RecordsPerSector)
    {
        attempts = RecordsPerSector;
    }

    for (uint32_t attempt = 0u; attempt < attempts; ++attempt)
    {
        // Attempt 0 reads the last record, attempt 1 the record before it, etc.
        const uint32_t record_index = record_count - 1u - attempt;
        const FSIZE_t offset = static_cast<FSIZE_t>(record_index) * sizeof(LogRecord_t);
        result = f_lseek(&file, offset);
        if (result != FR_OK)
        {
            break;
        }

        LogRecord_t record = {};
        UINT bytes_read = 0u;
        result = f_read(&file, &record, sizeof(record), &bytes_read);
        if (result != FR_OK)
        {
            break;
        }

        if ((bytes_read == sizeof(record)) && RecordCrcIsValid(record))
        {
            *timestamp = record.obc_time_ms;
            *found = true;
            break;
        }
    }

    const FRESULT close_result = f_close(&file);
    return (result == FR_OK) && (close_result == FR_OK);
}

/* Converts FatFs free clusters to a byte count. */
bool GetFreeBytes(uint64_t* free_bytes)
{
    if (free_bytes == nullptr)
    {
        return false;
    }

    DWORD free_clusters = 0u;
    FATFS* filesystem = nullptr;
    if (f_getfree(USERPath, &free_clusters, &filesystem) != FR_OK)
    {
        return false;
    }

    // FatFs reports clusters, so convert clusters to sectors and then bytes.
    *free_bytes = static_cast<uint64_t>(free_clusters) * static_cast<uint64_t>(filesystem->csize) * static_cast<uint64_t>(SectorSizeBytes);
    return true;
}

/* Deletes oldest closed telemetry files until a new 1 MiB file can be created safely. */
bool EnsureSpaceForNewFile(void)
{
    for (;;)
    {
        uint64_t free_bytes = 0u;
        if (!GetFreeBytes(&free_bytes))
        {
            return false;
        }
        if (free_bytes >= SpaceRequiredForNewFile)
        {
            return true;
        }

        uint32_t lowest_index = 0u;
        uint32_t highest_index = 0u;
        if (!ScanFileIndexes(&lowest_index, &highest_index))
        {
            return false;
        }
        (void)highest_index;

        // Never delete the file that is currently open for writing.
        if ((lowest_index == 0u) || (lowest_index == current_file_index))
        {
            return false;
        }

        char filename[13] = {};
        MakeTelemetryFilename(lowest_index, filename, sizeof(filename));
        if (f_unlink(filename) != FR_OK)
        {
            return false;
        }
    }
}

/* Loads saved metadata and reconstructs anything newer from telemetry files. */
bool InitializeSession(void)
{
    SessionMetadata_t loaded = {};
    bool loaded_valid = false;
    if (!SessionStore_Load(&loaded, &loaded_valid))
    {
        return false;
    }

    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!ScanFileIndexes(&lowest_index, &highest_index))
    {
        return false;
    }
    (void)lowest_index;

    uint32_t recovered_time = 0u;
    if (loaded_valid)
    {
        recovered_time = loaded.last_committed_time_ms;
    }

    if (highest_index != 0u)
    {
        uint32_t file_time = 0u;
        bool file_time_valid = false;
        if (!ReadLastValidTimestamp(highest_index, &file_time, &file_time_valid))
        {
            return false;
        }
        if (file_time_valid && (file_time > recovered_time))
        {
            recovered_time = file_time;
        }
    }

    if (loaded_valid)
    {
        session = loaded;
        session.session_id = loaded.session_id + 1u;
    }
    else
    {
        session = {};
        session.session_id = 1u;
    }

    session.active_file_index = 0u;
    session.last_committed_time_ms = recovered_time;

    uint32_t scanned_next = 1u;
    if (highest_index != 0u)
    {
        scanned_next = highest_index + 1u;
    }
    if (!loaded_valid || (session.next_file_index < scanned_next))
    {
        session.next_file_index = scanned_next;
    }
    if (session.next_file_index == 0u)
    {
        session.next_file_index = 1u;
    }

    // New stored time equals this recovered base plus the current boot's tick.
    time_base_ms = recovered_time;
    session_initialized = true;
    return true;
}

/* Re-scans file indexes after card reinsertion to prevent overwriting a file. */
bool RefreshNextFileIndex(void)
{
    uint32_t lowest_index = 0u;
    uint32_t highest_index = 0u;
    if (!ScanFileIndexes(&lowest_index, &highest_index))
    {
        return false;
    }
    (void)lowest_index;

    if ((highest_index != 0u) && (session.next_file_index <= highest_index))
    {
        session.next_file_index = highest_index + 1u;
    }
    return true;
}

/* Closes the previous file if needed and creates the next numbered telemetry file. */
bool OpenNewTelemetryFile(void)
{
    if (telemetry_file_open)
    {
        const FRESULT close_result = f_close(&telemetry_file);
        telemetry_file_open = false;
        current_file_index = 0u;
        current_file_bytes = 0u;
        if (close_result != FR_OK)
        {
            return false;
        }
    }

    if (!EnsureSpaceForNewFile() || !RefreshNextFileIndex())
    {
        return false;
    }
    if ((session.next_file_index == 0u) || (session.next_file_index > MaximumFileIndex))
    {
        return false;
    }

    const uint32_t new_index = session.next_file_index;
    char filename[13] = {};
    MakeTelemetryFilename(new_index, filename, sizeof(filename));
    if (f_open(&telemetry_file, filename, FA_CREATE_NEW | FA_WRITE) != FR_OK)
    {
        return false;
    }

    telemetry_file_open = true;
    current_file_index = new_index;
    current_file_bytes = 0u;
    session.active_file_index = new_index;
    session.next_file_index = new_index + 1u;

    // Save the selected filename immediately so a reboot cannot reuse it.
    if (!SessionStore_Save(&session))
    {
        (void)f_close(&telemetry_file);
        telemetry_file_open = false;
        current_file_index = 0u;
        current_file_bytes = 0u;
        return false;
    }
    return true;
}
}

/* Mounts storage, recovers session state, and creates a new telemetry file. */
bool TelemetryFileStore_Connect(void)
{
    if (f_mount(&USERFatFS, USERPath, 1u) != FR_OK)
    {
        return false;
    }

    if (session_initialized)
    {
        if (!RefreshNextFileIndex())
        {
            return false;
        }
    }
    else if (!InitializeSession())
    {
        return false;
    }

    return OpenNewTelemetryFile();
}

/* Closes the current telemetry file and unmounts storage after an error. */
void TelemetryFileStore_Disconnect(void)
{
    if (telemetry_file_open)
    {
        (void)f_close(&telemetry_file);
    }

    telemetry_file_open = false;
    current_file_index = 0u;
    current_file_bytes = 0u;
    (void)f_mount(nullptr, USERPath, 0u);
}

/* Returns the previous boot's final time, which is added to the current tick. */
uint32_t TelemetryFileStore_GetTimeBaseMs(void)
{
    return time_base_ms;
}

/* Writes and syncs one batch, rotating first when the 1 MiB limit requires it. */
bool TelemetryFileStore_Write(const LogRecord_t* records, uint32_t record_count)
{
    if ((records == nullptr) || (record_count == 0u) || (record_count > RecordsPerSector) || !telemetry_file_open)
    {
        return false;
    }

    const uint32_t bytes_to_write = record_count * static_cast<uint32_t>(sizeof(LogRecord_t));
    if ((current_file_bytes + bytes_to_write) > TelemetryFileSizeBytes)
    {
        if (!OpenNewTelemetryFile())
        {
            return false;
        }
    }

    UINT bytes_written = 0u;
    FRESULT result = f_write(&telemetry_file, records, bytes_to_write, &bytes_written);
    if ((result == FR_OK) && (bytes_written != bytes_to_write))
    {
        result = FR_DISK_ERR;
    }
    if (result == FR_OK)
    {
        result = f_sync(&telemetry_file);
    }
    if (result != FR_OK)
    {
        return false;
    }

    current_file_bytes += bytes_to_write;
    session.last_committed_time_ms = records[record_count - 1u].obc_time_ms;
    return SessionStore_Save(&session);
}
