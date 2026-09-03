// FreeRTOS telemetry queue consumer and SD logger task loop.

#include "SdLogger.hpp"

#include "TelemetryFileStore.hpp"
#include "cmsis_os.h"

#include "../../common/crc32.h"
#include "../../common/log_record.h"
#include "../Communication/ScheduleApi.hpp"
#include "../Communication/UartProtocol.hpp"
#include "../Power/task_watch.hpp"
#include "rtc.h"


#include <cstring>
#include <cstdint>

#if defined(__GNUC__)
extern "C" SatState_t PowerState_Get(void) __attribute__((weak));
extern "C" uint32_t Schedule_GetPeriodMs(ScheduleItemId_t item, SatState_t state) __attribute__((weak));
#endif

// the queue from where we will get our records
extern "C" osMessageQueueId_t q_telemetryHandle;

namespace {

// Fallback flush period used before the schedule module is linked
constexpr uint32_t default_flush_timeout_ticks = 2000u;


// when to flush the SD card
// checks current mode if mode is unavailable use defult time
// returns only the frequency
uint32_t flush_timeout_ticks(void)
{
	// if some state missing use default time
    if ((PowerState_Get == nullptr) || (Schedule_GetPeriodMs == nullptr)) {
        return default_flush_timeout_ticks;
    }

    const SatState_t state = PowerState_Get();

    if (static_cast<uint32_t>(state) >= static_cast<uint32_t>(SAT_STATE_COUNT)) {
        return default_flush_timeout_ticks;
    }

    const uint32_t period = Schedule_GetPeriodMs(SCHEDULE_ITEM_SD_FLUSH, state);

    // period of 0 indicates the item is disabled or an invalid parameter was passed, fall back to the default
    if (period == 0u) {
    	return default_flush_timeout_ticks;
	}

	return period;
}

constexpr uint32_t RetryDelayTicks = 5000u;
constexpr uint32_t QueueWaitTicks = 250u;
constexpr uint32_t FetchQueueWaitTicks = 5u;
constexpr uint32_t FetchFramePeriodTicks = 10u;
constexpr uint32_t FetchRecordsPerFrame = UART_MAX_PAYLOAD_SIZE / sizeof(LogRecord_t);

static_assert((LOG_RECORDS_PER_SECTOR * sizeof(LogRecord_t)) == LOG_SECTOR_SIZE_BYTES, "One logger batch must occupy exactly one sector");
static_assert(FetchRecordsPerFrame == 4u, "One UART fetch frame must contain four log records");

// Status values read by GroundComm without giving it access to FatFs.
volatile SdLoggerState_t logger_state = SD_LOGGER_INITIALIZING;
volatile uint32_t logger_error_count = 0u;

// ram memory field with records before saving them in the memory
LogRecord_t batch[LOG_RECORDS_PER_SECTOR] = {};
uint32_t batch_count = 0u;
uint32_t flush_deadline = 0u;
uint32_t next_retry_tick = 0u;
volatile uint32_t flush_count = 0u;
bool boot_records_written = false;

typedef struct
{
    uint32_t from_epoch_s;
    uint32_t to_epoch_s;
    uint16_t max_records;
    uint16_t sequence;
    uint8_t volume;
} FetchRequestMessage_t;

// full fetch state as in how much sent from how much .
struct FetchState_t {
    bool active;
    bool search_finished;
    bool frame_ready;
    uint16_t sequence;
    uint16_t records_sent;
    uint16_t probe_count;
    uint8_t final_status;
    uint8_t frame_type;
    uint16_t frame_length;
    uint8_t frame_payload[UART_MAX_PAYLOAD_SIZE];
    uint32_t next_send_tick;
    uint32_t from_epoch_s;
    uint32_t to_epoch_s;
    uint16_t max_records;
    uint8_t volume;
};

osMessageQueueId_t fetch_request_queue = nullptr;
FetchState_t fetch_state = {};

// check if we have to flush
bool deadline_reached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

// Publishes an error state and counts how many storage operations failed.
void set_logger_error(void)
{
    ++logger_error_count;
    logger_state = SD_LOGGER_ERROR;
}

// Leaves storage safely and schedules another mount attempt in five seconds.
void go_offline(uint32_t now)
{
    // Throw away only the incomplete RAM batch. Already synced records remain safe.
    TelemetryFileStore_Disconnect();
    batch_count = 0u;
    next_retry_tick = now + RetryDelayTicks;
    set_logger_error();
}

// read boot/restart flag(reason) save it on the SD and reset the register(flag) for later .
// also turns true global verb that indicates that boot record was saved .
// saves boot count and boot reason , so two records .
bool write_boot_records(void)
{
    LogRecord_t records[2] = {};
    // RTC == clock
    const uint32_t epoch = RTC_get_epoch();
    const uint32_t boot_count = RTC_get_boot_count();
    const uint32_t reset_flags = RTC_get_reset_flags();

    //saves the boot count
    records[0].epoch_s = epoch;
    records[0].sensor_id = SENSOR_ID_BOOT;
    records[0].type = LOG_RECORD_TYPE_BOOT;
    records[0].len = sizeof(boot_count);
    std::memcpy(records[0].value, &boot_count, sizeof(boot_count));
    records[0].crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&records[0]), LOG_RECORD_CRC_SIZE);
    //saves the boot reason
    records[1].epoch_s = epoch;
    records[1].sensor_id = SENSOR_ID_RESET_CAUSE;
    records[1].type = LOG_RECORD_TYPE_BOOT;
    records[1].len = sizeof(reset_flags);
    std::memcpy(records[1].value, &reset_flags, sizeof(reset_flags));
    records[1].crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&records[1]), LOG_RECORD_CRC_SIZE);

    if (!TelemetryFileStore_Write(records, 2u)) {
        return false;
    }

    boot_records_written = true;
    return true;
}

// Attempts to mount the card, recover session state, and open a new file.
bool try_bring_online(uint32_t now)
{
    if (!TelemetryFileStore_Connect()) {
        go_offline(now);
        return false;
    }

    logger_state = SD_LOGGER_READY;

    // tries to write boot record if it false it will return false .
    // so some thing went wrong and you cant save it on SD .
    if (!boot_records_written && !write_boot_records()) {
        go_offline(now);
        return false;
    }

    return true;
}

// Writes the current RAM batch and enters recovery mode if the write fails
bool flush_batch(uint32_t now)
{
    if (batch_count == 0u) {
        return true;
    }

    if (logger_state != SD_LOGGER_READY) {
        batch_count = 0u;
        return false;
    }

    // tries to write the batch on the SD
    if (!TelemetryFileStore_Write(batch, batch_count)) {
        go_offline(now);
        return false;
    }

    ++flush_count;
    batch_count = 0u;
    return true;
}

/*
 * indicate a search was finished update what is needed */
void finish_fetch_search(uint8_t status)
{
    fetch_state.probe_count = TelemetryFileStore_GetFetchProbeCount();
    fetch_state.final_status = status;
    fetch_state.search_finished = true;
    TelemetryFileStore_EndFetch();
}

/*
 * Prepares response for the uart
 * updates how much records will be sent and other usefull data */
void prepare_fetch_end_frame(void)
{
    UartFetchEndPayload_t response = {};
    response.status = fetch_state.final_status;
    response.record_count = fetch_state.records_sent;
    response.probe_count = fetch_state.probe_count;

    std::memcpy(fetch_state.frame_payload, &response, sizeof(response));
    fetch_state.frame_type = UART_MSG_FETCH_END;
    fetch_state.frame_length = sizeof(response);
    fetch_state.frame_ready = true;
}

/*
 * keeps reading valid records, packs as many as will fit into one UART fetch frame,
 *  stores them in fetch_state.frame_payload, and marks the frame ready for sending later.*/
void prepare_fetch_data_frame(void)
{

    if (!fetch_state.active || fetch_state.frame_ready) {
        return;
    }

    if (fetch_state.search_finished) {
        prepare_fetch_end_frame();
        return;
    }

    LogRecord_t records[FetchRecordsPerFrame] = {};
    uint32_t record_count = 0u;

    while (record_count < FetchRecordsPerFrame) {
    	// how many more records can i pack into uart frame
        const uint32_t total_count = static_cast<uint32_t>(fetch_state.records_sent)
        														+ record_count;

        const bool request_limit_reached = (fetch_state.max_records != 0u) &&
        									(total_count >= fetch_state.max_records);

        if (request_limit_reached || (total_count >= UINT16_MAX)) {
            finish_fetch_search(UART_STATUS_OK);
            break;
        }

        // checks is the next record we need is in the current file or next one
        const TelemetryReadResult_t read_result =
        		TelemetryFileStore_ReadNext(&records[record_count]);

        if (read_result == TELEMETRY_READ_RECORD) {
            ++record_count;
            continue;
        }

        if (read_result == TELEMETRY_READ_END) {
        	uint8_t status = UART_STATUS_OK;

        	if (total_count == 0u) {
        	    status = UART_STATUS_NOT_FOUND;
        	} else {
        	    status = UART_STATUS_OK;
        	}
            finish_fetch_search(status);
        } else {
            finish_fetch_search(UART_STATUS_STORAGE_ERROR);
        }
        break;
    }

    if (record_count == 0u) {
        prepare_fetch_end_frame();
        return;
    }

    fetch_state.frame_type = UART_MSG_FETCH_DATA;
    fetch_state.frame_length = static_cast<uint16_t>(record_count * sizeof(LogRecord_t));
    std::memcpy(fetch_state.frame_payload, records, fetch_state.frame_length);
    fetch_state.frame_ready = true;
}

/*
 * Prepares fetch frames and sends them to the GS.
 * Updates how many records were sent and schedules when the next frame can be sent.
 * When the FETCH_END frame is sent, it clears the fetch state and finishes the fetch.
 */
void process_fetch(uint32_t now)
{
    if (!fetch_state.active || !deadline_reached(now, fetch_state.next_send_tick)) {
        return;
    }

    prepare_fetch_data_frame();
    if (!fetch_state.frame_ready) {
        return;
    }

    // send the frame
    if (UartProtocol_SendFrame(fetch_state.frame_type, fetch_state.sequence,
    							fetch_state.frame_payload, fetch_state.frame_length) == 0u) {

        fetch_state.next_send_tick = now + 1u;
        return;
    }

    if (fetch_state.frame_type == UART_MSG_FETCH_DATA) {
        fetch_state.records_sent = static_cast<uint16_t>(fetch_state.records_sent +
        							(fetch_state.frame_length / sizeof(LogRecord_t)));

        fetch_state.frame_ready = false;
        fetch_state.next_send_tick = now + FetchFramePeriodTicks;
        return;
    }

    fetch_state = {};
}

/*
 * Starts a new fetch request if one is waiting in the queue.
 * Copies the request into fetch_state, checks that the SD logger is ready,
 * flushes recent buffered records to the SD, and starts the file search.
 * If anything fails, the fetch is finished with a storage error.
 */
void start_fetch_if_requested(uint32_t now)
{
    if (fetch_state.active || (fetch_request_queue == nullptr)) {
        return;
    }

    FetchRequestMessage_t message = {};
    //check if there is a fetch request if ye copy it to msg
    if (osMessageQueueGet(fetch_request_queue, &message, nullptr, 0u) != osOK) {
        return;
    }

    //start the global fetch state update it to start working
    fetch_state = {};
    fetch_state.active = true;
    fetch_state.sequence = message.sequence;
    fetch_state.from_epoch_s = message.from_epoch_s;
    fetch_state.to_epoch_s = message.to_epoch_s;
    fetch_state.max_records = message.max_records;
    fetch_state.volume = message.volume;
    fetch_state.next_send_tick = now;

    if (logger_state != SD_LOGGER_READY) {
        finish_fetch_search(UART_STATUS_STORAGE_ERROR);
        return;
    }

    if (!flush_batch(now) || !TelemetryFileStore_BeginFetch(message.volume, message.from_epoch_s, message.to_epoch_s)) {
        finish_fetch_search(UART_STATUS_STORAGE_ERROR);
    }
}

// Updates current record got from queue. correct time stamp and updates crc then adds it to buffer
void buffer_record(LogRecord_t record, uint32_t now)
{
    // Calculate CRC after changing time so the CRC matches the final stored bytes.
    record.crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&record), LOG_RECORD_CRC_SIZE);

    if (batch_count == 0u) {
    	flush_deadline = now + flush_timeout_ticks();
    }

    batch[batch_count] = record;
    ++batch_count;
}
}

// Returns the current logger state without accessing FatFs.
extern "C" SdLoggerState_t SdLogger_GetState(void)
{
    return logger_state;
}

// Returns the number of storage failures seen since boot.
extern "C" uint32_t SdLogger_GetErrorCount(void)
{
    return logger_error_count;
}

extern "C" uint8_t SdLogger_RequestFetch(uint16_t sequence, const UartFetchPayload_t* request)
{
    if ((fetch_request_queue == nullptr) || (request == nullptr)) {
        return 0u;
    }

    FetchRequestMessage_t message = {};
    message.from_epoch_s = request->from_epoch_s;
    message.to_epoch_s = request->to_epoch_s;
    message.max_records = request->max_records;
    message.sequence = sequence;
    message.volume = request->volume;
    return (osMessageQueuePut(fetch_request_queue, &message, 0u, 0u) == osOK) ? 1u : 0u;
}

// Runs forever as Task_SD_Logger, every helper FatFs call executes from here.
extern "C" void SdLogger_Task(void* argument)
{
    (void)argument;

    fetch_request_queue = osMessageQueueNew(2u, sizeof(FetchRequestMessage_t), nullptr);

    // check our queue each position is the correct size of one record
    // checks each msg size is record size .
    if ((q_telemetryHandle == nullptr) || (osMessageQueueGetMsgSize(q_telemetryHandle) != sizeof(LogRecord_t)) || (fetch_request_queue == nullptr)) {
        set_logger_error();
        for (;;) {
            osDelay(RetryDelayTicks);
        }
    }

    // The startup SD test already ran, but this task mounts again for recovery.
    uint32_t now = osKernelGetTickCount();
    (void)try_bring_online(now);

    for (;;) {
        //checks if we are connected if not check when we need to try to connect again
        now = osKernelGetTickCount();
        if ((logger_state != SD_LOGGER_READY) && deadline_reached(now, next_retry_tick)) {
            (void)try_bring_online(now);
        }

        start_fetch_if_requested(now);
        process_fetch(now);

        LogRecord_t record = {};
        // we try to take a record from the queue .
        //what queue,where to store the record,priority,how much time to wait
        const uint32_t queue_wait_ticks = fetch_state.active ? FetchQueueWaitTicks : QueueWaitTicks;
        const osStatus_t queue_result = osMessageQueueGet(q_telemetryHandle, &record, nullptr, queue_wait_ticks);
        now = osKernelGetTickCount();

        if (queue_result == osOK) {
            if (logger_state == SD_LOGGER_READY) {
            	//update record
                buffer_record(record, now);
                //if 16 records are present write them to the SD
                if (batch_count == LOG_RECORDS_PER_SECTOR) {
                    (void)flush_batch(now);
                }
            }
            // When offline, discard the record so the collector never gets blocked.
        }

        // if 2 seconds passed flush .
        if ((batch_count != 0u) && deadline_reached(now, flush_deadline)) {
            (void)flush_batch(now);
        }

        // Report only after the queue/storage iteration made forward progress.
        task_watch_checkin(TASK_WATCH_SD_LOGGER);
    }
}

extern "C" uint32_t SdLogger_GetFlushCount(void)
{
    return flush_count;
}
