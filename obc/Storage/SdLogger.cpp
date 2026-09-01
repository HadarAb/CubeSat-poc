// FreeRTOS telemetry queue consumer and SD logger task loop.

#include "SdLogger.hpp"

#include "TelemetryFileStore.hpp"
#include "cmsis_os.h"

#include "../../common/crc32.h"
#include "../../common/log_record.h"
#include "../Communication/ScheduleApi.hpp"
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

// Current flush period in ticks, taken from the schedule when it is available
uint32_t flush_timeout_ticks(void)
{
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

static_assert((LOG_RECORDS_PER_SECTOR * sizeof(LogRecord_t)) == LOG_SECTOR_SIZE_BYTES, "One logger batch must occupy exactly one sector");

// Status values read by GroundComm without giving it access to FatFs.
volatile SdLoggerState_t logger_state = SD_LOGGER_INITIALIZING;
volatile uint32_t logger_error_count = 0u;

// One-sector RAM buffer filled from q_telemetry before an SD write
LogRecord_t batch[LOG_RECORDS_PER_SECTOR] = {};
uint32_t batch_count = 0u;
uint32_t flush_deadline = 0u;
uint32_t next_retry_tick = 0u;
volatile uint32_t flush_count = 0u;

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

/* Writes one boot marker so the ground station can see reboot gaps in the
 * telemetry stream. It goes through TelemetryFileStore_Write like any other
 * record, so the decoder needs no special case. Routed to the EPS directory
 * because a reboot is a housekeeping event, not payload data.
 */
void write_boot_marker(void)
{
    LogRecord_t record = {};

    const uint32_t boot_count = RTC_get_boot_count();

    record.epoch_s = RTC_get_epoch();
    record.sensor_id = SENSOR_ID_BOOT;
    record.type = LOG_RECORD_TYPE_BOOT;
    record.len = sizeof(boot_count);
    std::memcpy(record.value, &boot_count, sizeof(boot_count));
    record.crc32 = Protocol_Crc32(
        reinterpret_cast<const uint8_t*>(&record), LOG_RECORD_CRC_SIZE);

    (void)TelemetryFileStore_Write(&record, 1u);
}

// Attempts to mount the card, recover session state, and open a new file.
bool try_bring_online(uint32_t now)
{
    if (!TelemetryFileStore_Connect()) {
        go_offline(now);
        return false;
    }

    logger_state = SD_LOGGER_READY;

    // Record the reboot only after storage is confirmed writable.
    write_boot_marker();

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

    if (!TelemetryFileStore_Write(batch, batch_count)) {
        go_offline(now);
        return false;
    }

    ++flush_count;
    batch_count = 0u;
    return true;
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

// Runs forever as Task_SD_Logger, every helper FatFs call executes from here.
extern "C" void SdLogger_Task(void* argument)
{
    (void)argument;

    // check our queue each position is the correct size of one record
    // checks each msg size is record size .
    if ((q_telemetryHandle == nullptr) || (osMessageQueueGetMsgSize(q_telemetryHandle) != sizeof(LogRecord_t))) {
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


        LogRecord_t record = {};
        // we try to take a record from the queue .
        //what queue,where to store the record,priority,how much time to wait
        const osStatus_t queue_result = osMessageQueueGet(q_telemetryHandle, &record, nullptr, QueueWaitTicks);
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
