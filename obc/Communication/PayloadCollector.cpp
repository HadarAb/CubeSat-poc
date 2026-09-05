#include "PayloadCollector.hpp"
#include "I2CMaster.hpp"
#include "ScheduleApi.hpp"
#include "../../common/i2c/protocol.h" // SnapshotData_t and PAYLOAD_NODE_ID
#include "../../common/log_record.h" // LogRecord_t definition
#include "../../common/i2c/bus_config.h" // PAYLOAD_I2C_ADDRESS_HAL
#include "../../common/crc32.h" // CRC functions
#include "cmsis_os2.h" // osKernel, osDelay, osMutex
#include "../../common/vtable/vtable.h" // VTable_HashName
#include "../Core/Inc/rtc.h" // RTC_get_epoch(), RTC_get_boot_count()
#include "../Power/task_watch.hpp"

#include <string.h> // memset
#include <stdint.h>
#include <stdbool.h>


static const uint32_t SCHEDULE_CHECK_PERIOD_MS = 10u;
static const uint32_t LEGACY_COLLECT_PERIOD_MS = 500u;

/*
 * Dev 1 owns these functions. Weak references preserve the old 500 ms polling
 * behavior until the real state and schedule implementation is merged.
 */
#if defined(__GNUC__)
extern "C" SatState_t PowerState_Get(void) __attribute__((weak));
extern "C" void Schedule_Init(void) __attribute__((weak));
extern "C" bool Schedule_TryTakeDue(ScheduleItemId_t item, SatState_t state, uint32_t now_ticks) __attribute__((weak));
#endif

extern osMessageQueueId_t q_telemetryHandle; // from freertos.c the queue
extern osMutexId_t i2c_mtxHandle; // shared I2C bus mutex

// struct for each payload/node about its status
struct NodeState {
    uint16_t addr; // shifted HAL address
    uint8_t node_id; // logical ID
    bool online;	// is it working
    uint32_t err_count;
    uint32_t crc_fail;
    uint8_t consecutive_errors; // tracks successive failures to avoid false offline alerts
};

//the devices that the OBC knows exist (EPS and PAYLOAD )
//arr of there status
static NodeState s_nodes[] = {
    {PAYLOAD_I2C_ADDRESS_HAL, PAYLOAD_NODE_ID, false, 0, 0, 0},
    {EPS_I2C_ADDRESS_HAL, EPS_NODE_ID, false, 0, 0, 0},
};

// calculate the number of nodes in the array (avoid hard coding)
static const uint8_t NODE_COUNT = sizeof(s_nodes) / sizeof(s_nodes[0]);

// Create exactly one snapshot buffer per module
static Snapshot s_snapshot[NODE_COUNT];
static osMutexId_t s_snapshot_mtx;
static PayloadCollectorStatus_t s_status = {};

static uint32_t s_dropped_frames = 0;
static uint32_t s_overruns = 0;

// Helper to find index in snapshot array based on ID
static uint8_t index_of(uint8_t node_id)
{
	for (uint8_t i = 0; i < NODE_COUNT; ++i) {
		if (s_nodes[i].node_id == node_id) {
			return i;
		}
	}

	return 0;
}

// Initializes the Mutex. called in freertos.c before the scheduler starts
void PayloadCollector_Init(void)
{
	// Create mutex with Priority Inheritance to prevent Priority Inversion
	static const osMutexAttr_t attr = { "snapshot_mtx", osMutexPrioInherit, NULL, 0U };
	s_snapshot_mtx = osMutexNew(&attr);
}

// thread safe function for GroundComm to read the latest data
// snapshot will be filled with data , threw this function you grab it into out
bool PayloadCollector_GetSnapshot(uint8_t node_id, Snapshot *out)
{
	// Wait max 10 ticks for the mutex
	if (osMutexAcquire(s_snapshot_mtx, 10U) != osOK) {
		return false;
	}

	*out = s_snapshot[index_of(node_id)];
	osMutexRelease(s_snapshot_mtx);

	return out->valid;
}

/* safe/mutex gatter for status of all payloads  */
bool PayloadCollector_GetStatus(PayloadCollectorStatus_t* out)
{
    if ((out == nullptr) || (osMutexAcquire(s_snapshot_mtx, 10u) != osOK))
    {
        return false;
    }

    *out = s_status;
    osMutexRelease(s_snapshot_mtx);
    return out->valid;
}

/* Publishes one coherent copy of the latest collector and node health. */
static void PublishCollectorStatus()
{
    if (osMutexAcquire(s_snapshot_mtx, 10u) != osOK)
    {
        return;
    }

    //start updating status
    s_status.valid = true;
    s_status.dropped_frames = s_dropped_frames;
    s_status.overruns = s_overruns;

    for (const NodeState& node : s_nodes)
    {
    	//find each module and update its status
        if (node.node_id == PAYLOAD_NODE_ID)
        {
            s_status.payload_online = node.online;
            s_status.payload_i2c_errors = node.err_count;
            s_status.payload_crc_failures = node.crc_fail;
        }
        else if (node.node_id == EPS_NODE_ID)
        {
            s_status.eps_online = node.online;
            s_status.eps_i2c_errors = node.err_count;
            s_status.eps_crc_failures = node.crc_fail;
        }
    }

    osMutexRelease(s_snapshot_mtx);
}

enum class SnapshotField : uint8_t
{
    Temperature,
    TotalDose,
    SelCount,
    ResetCount,
    BatteryVoltage,
    StoredOnly
};

//struct for the task that collects seonsors data
struct ScheduledSensor
{
    ScheduleItemId_t schedule_item;
    uint8_t node_id;
    const char* key;
    VtType_t expected_type;
    SnapshotField snapshot_field;
};

/*
 * This fixed table tells the collector where every known sensor lives, how to
 * read it, which schedule item controls it, and where its latest value belongs.
 */
static const ScheduledSensor SENSORS[] = {
    {SCHEDULE_ITEM_PAYLOAD_TEMP, PAYLOAD_NODE_ID, "TEMP", VT_TYPE_F32, SnapshotField::Temperature},
    {SCHEDULE_ITEM_PAYLOAD_TDOSE, PAYLOAD_NODE_ID, "TDOSE", VT_TYPE_F32, SnapshotField::TotalDose},
    {SCHEDULE_ITEM_PAYLOAD_SEL, PAYLOAD_NODE_ID, "SEL", VT_TYPE_U32, SnapshotField::SelCount},
    {SCHEDULE_ITEM_PAYLOAD_NRESET, PAYLOAD_NODE_ID, "NRESET", VT_TYPE_U32, SnapshotField::ResetCount},
    {SCHEDULE_ITEM_EPS_VBAT, EPS_NODE_ID, "VBAT", VT_TYPE_F32, SnapshotField::BatteryVoltage},
    {SCHEDULE_ITEM_EPS_TEMP, EPS_NODE_ID, "TEMP", VT_TYPE_F32, SnapshotField::Temperature},
    {SCHEDULE_ITEM_EPS_SP0_TEMP, EPS_NODE_ID, "SP0_T", VT_TYPE_F32, SnapshotField::StoredOnly},
    {SCHEDULE_ITEM_EPS_SP0_CURRENT, EPS_NODE_ID, "SP0_I", VT_TYPE_F32, SnapshotField::StoredOnly},
};

static float DecodeF32(const VtValueWire_t& wire)
{
    float value = 0.0f;
    memcpy(&value, wire.value, sizeof(value));
    return value;
}

static uint32_t DecodeU32(const VtValueWire_t& wire)
{
    uint32_t value = 0u;
    memcpy(&value, wire.value, sizeof(value));
    return value;
}

static int16_t ToTenths(float value)
{
    float scaled = value * 10.0f;
    if (!(scaled == scaled)) {
        return 0;
    }
    if (scaled > 32767.0f) {
        return 32767;
    }
    if (scaled < -32768.0f) {
        return -32768;
    }
    scaled += (scaled >= 0.0f) ? 0.5f : -0.5f;
    return static_cast<int16_t>(scaled);
}

static uint16_t ToU16(float value)
{
    if (!(value == value) || (value <= 0.0f)) {
        return 0u;
    }
    if (value >= 65535.0f) {
        return 65535u;
    }
    return static_cast<uint16_t>(value + 0.5f);
}

/* Temporary compatibility mapping for the unchanged GS battery-percent field. */
static uint8_t BatteryVoltageToPercent(float voltage)
{
    constexpr float EmptyVoltage = 3.30f;
    constexpr float FullVoltage = 4.20f;

    if (!(voltage == voltage) || (voltage <= EmptyVoltage)) {
        return 0u;
    }
    if (voltage >= FullVoltage) {
        return 100u;
    }
    return static_cast<uint8_t>(
        (((voltage - EmptyVoltage) * 100.0f) / (FullVoltage - EmptyVoltage)) + 0.5f);
}

/* saves data inside the snapshot
 * spec what value it is and where to save it
 * wire the value it self in numbers
 * snapshot is the cashed space where we will save the data  */
static void ApplyToSnapshot(const ScheduledSensor& sensor, const VtValueWire_t& wire, SnapshotData_t* snapshot)
{
    switch (sensor.snapshot_field) {
        case SnapshotField::Temperature:
            snapshot->temperature_c_x10 = ToTenths(DecodeF32(wire));
            break;

        case SnapshotField::TotalDose:
            /* Compatibility only: the unchanged GS labels this field radiation_cps. */
            snapshot->radiation_cps = ToU16(DecodeF32(wire));
            break;

        case SnapshotField::SelCount:
            if (DecodeU32(wire) != 0u) {
                snapshot->flags |= PAYLOAD_FLAG_SEU_INJECTED;
            }
            break;

        case SnapshotField::BatteryVoltage:
            snapshot->battery_pct = BatteryVoltageToPercent(DecodeF32(wire));
            break;

        case SnapshotField::ResetCount:
        case SnapshotField::StoredOnly:
            /* These values are preserved in LogRecord_t but have no old GS field. */
            break;
    }
}

/* grabing data from node into a record to later store it on the sd */
static void QueueValue(const ScheduledSensor& sensor, const VtValueWire_t& wire)
{
    LogRecord_t record = {};
    record.epoch_s = RTC_get_epoch();

    const uint16_t node_part = static_cast<uint16_t>(static_cast<uint16_t>(sensor.node_id) << 8u);
    const uint16_t key_part = static_cast<uint16_t>(VTable_HashName(sensor.key) & 0x00FFu);

    record.sensor_id = static_cast<uint16_t>(node_part | key_part);
    record.type = LOG_RECORD_TYPE_TELEMETRY;
    record.len = wire.len;
    memcpy(record.value, wire.value, wire.len);
    record.crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&record),
    									LOG_RECORD_CRC_SIZE);

    if ((q_telemetryHandle == nullptr)
        || (osMessageQueuePut(q_telemetryHandle, &record, 0u, 0u) != osOK)) {
        ++s_dropped_frames;
    }
}

/* Return the runtime state object for one logical node. */
static NodeState* FindNode(uint8_t node_id)
{
    for (NodeState& node : s_nodes)
    {
        if (node.node_id == node_id)
        {
            return &node;
        }
    }

    return nullptr;
}

/* Record a successful reply from a node, even when its value is unusable. */
static void MarkNodeResponded(NodeState& node)
{
    node.consecutive_errors = 0u;
    node.online = true;
}

/* Mark a node offline only after three consecutive bus-level failures. */
static void MarkNodeBusError(NodeState& node)
{
    ++node.err_count;
    ++node.consecutive_errors;

    if (node.consecutive_errors >= 3u)
    {
        node.online = false;
    }
}

/*
 * Update only the newly read field in the existing snapshot. Other sensor
 * values stay untouched, so sensors with different periods accumulate safely.
 */
static void UpdateSnapshot(const ScheduledSensor& sensor,
							const VtValueWire_t& wire, uint32_t now_ticks)
{
    if ((sensor.snapshot_field == SnapshotField::ResetCount)
    		|| (sensor.snapshot_field == SnapshotField::StoredOnly))
    {
        // These keys are stored on SD, but the legacy live payload has no field for them.
        return;
    }

    if (osMutexAcquire(s_snapshot_mtx, 10u) != osOK)
    {
        return;
    }

    Snapshot& snapshot = s_snapshot[index_of(sensor.node_id)];

    // This is initialization only later updates preserve all other fields.
    if (!snapshot.valid)
    {
        memset(&snapshot.data, 0, sizeof(snapshot.data));
        snapshot.data.node_id = sensor.node_id;
    }

    ApplyToSnapshot(sensor, wire, &snapshot.data);

    if (sensor.snapshot_field == SnapshotField::BatteryVoltage)
    {
        snapshot.battery_valid = true;
    }

    snapshot.data.timestamp_ms = now_ticks;
    snapshot.data.crc32 = Protocol_Crc32(reinterpret_cast<const uint8_t*>(&snapshot.data),
    									 SNAPSHOT_DATA_CRC_SIZE);
    snapshot.obc_time_ms = now_ticks;
    snapshot.valid = true;

    osMutexRelease(s_snapshot_mtx);
}

/* Read one due sensor, queue its record, and update its existing snapshot field. */
static void ReadScheduledSensor(const ScheduledSensor& sensor)
{
    NodeState* node = FindNode(sensor.node_id);

    if (node == nullptr)
    {
        return;
    }

    if ((i2c_mtxHandle == nullptr) || (osMutexAcquire(i2c_mtxHandle, osWaitForever) != osOK))
    {
        ++node->err_count;
        return;
    }

    VtValueWire_t wire = {};
    const I2CKeyReadResult_t result =
    		I2CMaster_ReadKey(node->addr, sensor.key, sensor.expected_type, &wire);
    osMutexRelease(i2c_mtxHandle);

    if (result == I2C_KEY_READ_BUS_ERROR)
    {
        MarkNodeBusError(*node);
        return;
    }

    // CRC, format, and missing key replies still prove that the node answered.
    MarkNodeResponded(*node);

    if (result == I2C_KEY_READ_CRC_ERROR)
    {
        ++node->crc_fail;
        return;
    }

    if (result == I2C_KEY_READ_FORMAT_ERROR)
    {
        ++node->err_count;
        return;
    }

    if (result == I2C_KEY_READ_MISSING)
    {
        return;
    }

    // Storage failure never blocks the live snapshot update.
    QueueValue(sensor, wire);
    UpdateSnapshot(sensor, wire, osKernelGetTickCount());
}

/* Return true only when Dev 1's complete schedule API is linked. */
static bool SensorScheduleIsAvailable()
{
#if defined(__GNUC__)
    return (PowerState_Get != nullptr) && (Schedule_Init != nullptr) && (Schedule_TryTakeDue != nullptr);
#else
    return true;
#endif
}

/* Tick-wrap-safe deadline comparison used only by the legacy fallback. */
static bool DeadlineReached(uint32_t now, uint32_t deadline)
{
    return static_cast<int32_t>(now - deadline) >= 0;
}

//the big task that checks and collects data from payloads
void payload_collector_run()
{
	uint32_t check_period_ticks = SCHEDULE_CHECK_PERIOD_MS * osKernelGetTickFreq() / 1000u;
	if (check_period_ticks == 0u)
	{
		check_period_ticks = 1u;
	}

	uint32_t next_check_ticks = osKernelGetTickCount();
	uint32_t legacy_due_ms = HAL_GetTick();

	//main loop when system is working
	for (;;)
	{
		next_check_ticks += check_period_ticks;
		const bool schedule_available = SensorScheduleIsAvailable();
		const uint32_t now_ms = HAL_GetTick();
		bool legacy_cycle_due = false;

		if (!schedule_available && DeadlineReached(now_ms, legacy_due_ms))
		{
			// Preserve the working 500 ms polling until Dev 1's module is merged.
			legacy_cycle_due = true;
			legacy_due_ms = now_ms + LEGACY_COLLECT_PERIOD_MS;
		}

		if (schedule_available || legacy_cycle_due)
		{
			SatState_t state = SAT_STATE_NORMAL;
			bool state_valid = true;

			if (schedule_available)
			{
				state = PowerState_Get();
				state_valid = static_cast<uint32_t>(state)
								< static_cast<uint32_t>(SAT_STATE_COUNT);
			}

			if (state_valid)
			{
				for (const ScheduledSensor& sensor : SENSORS)
				{
					if (schedule_available &&
							!Schedule_TryTakeDue(sensor.schedule_item, state, now_ms))
					{
						continue;
					}

					ReadScheduledSensor(sensor);
				}
			}
		}

		const uint32_t loop_end_ticks = osKernelGetTickCount();
		if (static_cast<int32_t>(loop_end_ticks - next_check_ticks) > 0)
		{
			++s_overruns;
			next_check_ticks = loop_end_ticks;
		}

		PublishCollectorStatus();
		// Report only after scheduling, sensor reads, and status publication finish.
		task_watch_checkin(TASK_WATCH_COLLECTOR);
		osDelayUntil(next_check_ticks);
	}
}
