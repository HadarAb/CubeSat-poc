/* Reads and writes the redundant CRC-protected SESSION.BIN metadata. */

#include "SessionStore.hpp"

#include "fatfs.h"

#include "../../common/crc32.h"

#include <cstddef>
#include <cstdint>
#include <cstdio> // std::snprintf

namespace
{
constexpr uint32_t SessionMagic = 0x53534553u; /* "SESS" in little endian. */
constexpr uint16_t SessionVersion = 1u;
/*
 * Filenames are generated per directory (e.g. "PAYLOAD/SESSION.BIN").
 */
//constexpr char SessionFilename[] = "SESSION.BIN";

/* Exact 32-byte format of one metadata copy inside SESSION.BIN. */
typedef struct __attribute__((packed))
{
    uint32_t magic;                  /* Identifies this as our session format. */
    uint16_t version;                /* Allows the format to change later. */
    uint16_t size;                   /* Detects a wrong structure layout. */
    uint32_t generation;             /* Increases after every metadata save. */
    uint32_t session_id;             /* Increases once for every OBC boot. */
    uint32_t active_file_index;      /* Number in the current TLMxxxx.BIN. */
    uint32_t next_file_index;        /* File number to use for the next file. */
    uint32_t last_committed_time_ms; /* Time of the last safely synced record. */
    uint32_t crc32;                  /* Protects the first 28 metadata bytes. */
} SessionSlot_t;

static_assert(sizeof(SessionSlot_t) == 32u, "SESSION.BIN slots must stay 32 bytes");
static_assert(offsetof(SessionSlot_t, crc32) == 28u, "SESSION.BIN CRC must stay at offset 28");


/* in session file there are 2 slots of the sd memory
 * when you connect to the sd card you check that the current slot you load is correct
 * Checks the format markers and CRC before metadata is trusted. */
bool SessionSlotIsValid(const SessionSlot_t& slot)
{
    //checks that the SD card data is valid for our format and not corrupted
    if ((slot.magic != SessionMagic) || (slot.version != SessionVersion) || (slot.size != sizeof(SessionSlot_t)))
    {
        return false;
    }

    // Recalculate the first 28 bytes and compare them with the saved CRC.
    const uint32_t expected_crc = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&slot), static_cast<uint32_t>(offsetof(SessionSlot_t, crc32)));
    return expected_crc == slot.crc32;
}

/* Compares two slots numbers who is newer  generation counters safely even after the 32-bit value wraps. */
bool GenerationIsNewer(uint32_t candidate, uint32_t reference)
{
    return static_cast<int32_t>(candidate - reference) > 0;
}


/* Copies the ondisk/slot representation into the RAM as a structure. */
void CopySlotToMetadata(const SessionSlot_t& slot, SessionMetadata_t* metadata)
{
    metadata->generation = slot.generation;
    metadata->session_id = slot.session_id;
    metadata->active_file_index = slot.active_file_index;
    metadata->next_file_index = slot.next_file_index;
    metadata->last_committed_time_ms = slot.last_committed_time_ms;
}
}

/* Loads the newest valid SESSION.BIN slot for a specific volume. A missing file is not an error. */
bool SessionStore_Load(const char* dir_path, SessionMetadata_t* metadata, bool* valid)
{
	if ((dir_path == nullptr) || (metadata == nullptr) || (valid == nullptr)) {
		return false;
	}

    *metadata = {};
    *valid = false;

	/*
	 * Dynamically build the filename using the provided volume path.
	 * For example, if dir_path is "PAYLOAD", filename becomes "PAYLOAD/SESSION.BIN".
	 */
	char filename[32] = {};
	std::snprintf(filename, sizeof(filename), "%s/SESSION.BIN", dir_path);

	FIL file = {};
	// Open the dynamically named file
	FRESULT result = f_open(&file, filename, FA_READ);

    if (result == FR_NO_FILE) {
        return true;
    }

    if (result != FR_OK) {
        return false;
    }

    // SESSION.BIN has two copies. A reset may damage one while leaving the other valid.
    SessionSlot_t slots[2] = {};
    bool slot_valid[2] = {false, false};
    for (uint32_t slot_index = 0u; slot_index < 2u; ++slot_index) {
        UINT bytes_read = 0u;
        result = f_read(&file, &slots[slot_index], sizeof(SessionSlot_t), &bytes_read);
        if (result != FR_OK) {
            break;
        }

        slot_valid[slot_index] = (bytes_read == sizeof(SessionSlot_t)) && SessionSlotIsValid(slots[slot_index]);
    }

    const FRESULT close_result = f_close(&file);
    if ((result != FR_OK) || (close_result != FR_OK)) {
        return false;
    }

    // If both copies are valid, select the one with the newest generation number.
    if (slot_valid[0] && slot_valid[1])
    {
        if (GenerationIsNewer(slots[1].generation, slots[0].generation)) {
            CopySlotToMetadata(slots[1], metadata);
        } else {
            CopySlotToMetadata(slots[0], metadata);
        }
        *valid = true;
    } else if (slot_valid[0]) {
        CopySlotToMetadata(slots[0], metadata);
        *valid = true;
    } else if (slot_valid[1]) {
        CopySlotToMetadata(slots[1], metadata);
        *valid = true;
    }

    return true;
}

/* Writes the metadata to the older slot of the specified volume's SESSION.BIN. */
bool SessionStore_Save(const char* dir_path, SessionMetadata_t* metadata)
{
	if ((dir_path == nullptr) || (metadata == nullptr)) {
		return false;
	}

    SessionSlot_t next = {};
    next.magic = SessionMagic;
    next.version = SessionVersion;
    next.size = sizeof(SessionSlot_t);
    next.generation = metadata->generation + 1u;
    next.session_id = metadata->session_id;
    next.active_file_index = metadata->active_file_index;
    next.next_file_index = metadata->next_file_index;
    next.last_committed_time_ms = metadata->last_committed_time_ms;
    next.crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&next), static_cast<uint32_t>(offsetof(SessionSlot_t, crc32)));

	/*
	 * Build the target path from the directory context.
	 * Prevents Payload (0:/) and EPS (1:/) from overwriting each other's metadata.
	 */
	char filename[32] = {};
	std::snprintf(filename, sizeof(filename), "%s/SESSION.BIN", dir_path);

	FIL file = {};
	// Using the dynamically created filename
	FRESULT result = f_open(&file, filename, FA_OPEN_ALWAYS | FA_READ | FA_WRITE);

	if (result != FR_OK) {
		return false;
	}

    // Even generations use slot 0 and odd generations use slot 1.
    //in simple words , witch slot to write 0 or 1
    const uint32_t slot_index = next.generation & 1u;
    // open the currect file
    result = f_lseek(&file, static_cast<FSIZE_t>(slot_index * sizeof(SessionSlot_t)));

    UINT bytes_written = 0u;
    if (result == FR_OK)
    {
    	//write next into the file
        result = f_write(&file, &next, sizeof(next), &bytes_written);
    }
    if ((result == FR_OK) && (bytes_written != sizeof(next)))
    {
        result = FR_DISK_ERR;
    }
    if (result == FR_OK)
    {
        result = f_sync(&file);
    }

    const FRESULT close_result = f_close(&file);
    if ((result != FR_OK) || (close_result != FR_OK))
    {
        return false;
    }

    // Update RAM only after the new slot is safely stored.
    metadata->generation = next.generation;
    return true;
}
