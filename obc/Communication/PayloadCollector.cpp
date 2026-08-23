#include "PayloadCollector.hpp"
#include "I2CMaster.hpp"
#include "../../common/i2c/protocol.h" // SnapshotData_t and PAYLOAD_NODE_ID
#include "../../common/log_record.h" // LogRecord_t definition
#include "../../common/i2c/bus_config.h" // PAYLOAD_I2C_ADDRESS_HAL
#include "../../common/crc32.h" // CRC functions
#include "cmsis_os2.h" // osKernel, osDelay, osMutex
#include "../../common/vtable/vtable.h" // VTable_HashName
#include "../Core/Inc/rtc.h" // RTC_get_epoch(), RTC_get_boot_count()

#include <string.h> // memset
#include <stdint.h>
#include <stdbool.h>


// Calculates exact ticks for 500ms based on the OS frequency, avoiding hardcoded values.
static const uint32_t COLLECT_PERIOD_TICKS = 500U * osKernelGetTickFreq() / 1000U;

extern osMessageQueueId_t q_telemetryHandle; // from freertos.c the queue
extern osMutexId_t i2c_mtxHandle; // shared I2C bus mutex

struct NodeState {
    uint16_t addr; // shifted HAL address
    uint8_t node_id; // logical ID
    bool online;
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

// Create exactly one snapshot buffer per node
static Snapshot s_snapshot[NODE_COUNT];
static osMutexId_t s_snapshot_mtx;

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

enum class SnapshotField : uint8_t
{
    Temperature,
    TotalDose,
    SelCount,
    ResetCount,
    BatteryVoltage,
    StoredOnly
};

struct KeySpec
{
    const char* name;
    VtType_t expected_type;
    SnapshotField snapshot_field;
};

/* Four fixed demo reads per node. Add more EPS panel keys here when needed. */
static const KeySpec PAYLOAD_KEYS[] = {
    {"TEMP",   VT_TYPE_F32, SnapshotField::Temperature},
    {"TDOSE",  VT_TYPE_F32, SnapshotField::TotalDose},
    {"SEL",    VT_TYPE_U32, SnapshotField::SelCount},
    {"NRESET", VT_TYPE_U32, SnapshotField::ResetCount},
};

static const KeySpec EPS_KEYS[] = {
    {"VBAT",  VT_TYPE_F32, SnapshotField::BatteryVoltage},
    {"TEMP",  VT_TYPE_F32, SnapshotField::Temperature},
    {"SP0_T", VT_TYPE_F32, SnapshotField::StoredOnly},
    {"SP0_I", VT_TYPE_F32, SnapshotField::StoredOnly},
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

/*  */
static void ApplyToSnapshot(const KeySpec& spec, const VtValueWire_t& wire,
                            SnapshotData_t* snapshot)
{
    switch (spec.snapshot_field) {
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
static void QueueValue(const NodeState& node, const KeySpec& spec,const VtValueWire_t& wire)
{
    LogRecord_t record = {};
    record.epoch_s = RTC_get_epoch();

    const uint16_t node_part = static_cast<uint16_t>(static_cast<uint16_t>(node.node_id) << 8u);
    const uint16_t key_part = static_cast<uint16_t>(VTable_HashName(spec.name) & 0x00FFu);

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

/*fill the snapshot with data */
static void PublishSnapshot(const NodeState& node, const SnapshotData_t& data,
                            uint32_t now)
{
    if (osMutexAcquire(s_snapshot_mtx, 10u) == osOK) {
        Snapshot& snapshot = s_snapshot[index_of(node.node_id)];
        snapshot.data = data;
        snapshot.obc_time_ms = now;
        snapshot.valid = true;
        osMutexRelease(s_snapshot_mtx);
    }
}

/* collects data from the nodes and puts it in snapshots and in queue
 * a long part here is inside the mutex */
static void collect_from_node(NodeState& node)
{
    const KeySpec* keys = (node.node_id == PAYLOAD_NODE_ID) ? PAYLOAD_KEYS : EPS_KEYS;
    const uint8_t key_count = (node.node_id == PAYLOAD_NODE_ID)
        ? static_cast<uint8_t>(sizeof(PAYLOAD_KEYS) / sizeof(PAYLOAD_KEYS[0]))
        : static_cast<uint8_t>(sizeof(EPS_KEYS) / sizeof(EPS_KEYS[0]));

    SnapshotData_t snapshot_data = {};
    snapshot_data.timestamp_ms = osKernelGetTickCount();
    snapshot_data.node_id = node.node_id;

    bool node_responded = false;
    bool received_value = false;

    if ((i2c_mtxHandle == nullptr)
        || (osMutexAcquire(i2c_mtxHandle, osWaitForever) != osOK)) {
        ++node.err_count;
        return;
    }

    for (uint8_t index = 0u; index < key_count; ++index) {
        VtValueWire_t wire = {};
        //request from one of the nodes for data by index and name
        const I2CKeyReadResult_t result = I2CMaster_ReadKey(
        		node.addr, keys[index].name, keys[index].expected_type, &wire);

        if (result == I2C_KEY_READ_BUS_ERROR) {
            ++node.err_count;
            break;
        }

        node_responded = true;
        if (result == I2C_KEY_READ_CRC_ERROR) {
            ++node.crc_fail;
            continue;
        }
        if (result == I2C_KEY_READ_FORMAT_ERROR) {
            ++node.err_count;
            continue;
        }
        if (result == I2C_KEY_READ_MISSING) {
            continue;
        }

        received_value = true;
        //puts data in local snapshot before publishing
        ApplyToSnapshot(keys[index], wire, &snapshot_data);
        //put the data inside the queue
        QueueValue(node, keys[index], wire);
    }
    //a long mutex long for all i2c calls
    osMutexRelease(i2c_mtxHandle);

    if (node_responded) {
        node.consecutive_errors = 0u;
        node.online = true;
    } else {
        ++node.consecutive_errors;
        if (node.consecutive_errors >= 3u) {
            node.online = false;
        }
    }

    //did we recive any thing good
    if (received_value) {
    	//check crc
        snapshot_data.crc32 = Protocol_Crc32(
        		reinterpret_cast<const uint8_t*>(&snapshot_data), SNAPSHOT_DATA_CRC_SIZE);
        //publish it inside snapshot for later use
        PublishSnapshot(node, snapshot_data, snapshot_data.timestamp_ms);
    }
}

void payload_collector_run()
{
	// Boot marker generation
	LogRecord_t boot_rec;
	memset(&boot_rec, 0, sizeof(boot_rec));

	boot_rec.epoch_s = RTC_get_epoch();
	boot_rec.sensor_id = 0xFFFF; // 0xFFFF signals a System/OBC Event
	boot_rec.type = LOG_RECORD_TYPE_BOOT;
	boot_rec.len = 4; // We are sending 4 bytes of data

	uint32_t current_boot_count = RTC_get_boot_count(); // Fetch from hardware backup register
	memcpy(boot_rec.value, &current_boot_count, sizeof(current_boot_count));

	boot_rec.crc32 = Protocol_Crc32((const uint8_t*)&boot_rec,
									sizeof(LogRecord_t) - sizeof(uint32_t));

	// Push the Boot Marker to the SD queue immediately upon boot
	osMessageQueuePut(q_telemetryHandle, &boot_rec, 0U, 100U);

	uint32_t next = osKernelGetTickCount();
	while (1) {
		next += COLLECT_PERIOD_TICKS;
		// the important part where it collects the data and publishes it
		for (auto &n : s_nodes) {
			collect_from_node(n);
		}

		// tick counters wrap at 2^32, and unsigned subtraction cast to signed handles the wrap correctly
		if ((int32_t)(osKernelGetTickCount() - next) > 0) {
			++s_overruns;
		}

		osDelayUntil(next);
	}
}
