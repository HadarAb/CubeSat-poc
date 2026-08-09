#include "PayloadCollector.hpp"
#include "../../common/i2c/protocol.h" // PayloadData_t and PAYLOAD_NODE_ID
#include "../../common/log_record.h" // LogRecord_t definition
#include "../../common/i2c/bus_config.h" // PAYLOAD_I2C_ADDRESS_HAL
#include "../../common/crc32.h" // CRC functions
#include "cmsis_os2.h" // osKernel, osDelay, osMutex
#include "i2c.h" // hi2c1 and HAL functions of I2C
#include "../../common/vtable.h" // VTable_HashName
#include "../Core/Inc/rtc.h" // RTC_get_epoch(), RTC_get_boot_count()

#include <string.h> // memset
#include <stdint.h>
#include <stdbool.h>


// 20ms timeout is long enough for the data to arrive, but short enough to keep the system responsive.
static const uint32_t I2C_TIMEOUT_MS = 20U;

// Calculates exact ticks for 500ms based on the OS frequency, avoiding hardcoded values.
static const uint32_t COLLECT_PERIOD_TICKS = 500U * osKernelGetTickFreq() / 1000U;

extern osMessageQueueId_t q_telemetryHandle; // from freertos.c
extern osMutexId_t i2c_mtxHandle; // shared I2C bus mutex

struct NodeState {
    uint16_t addr; // shifted HAL address
    uint8_t node_id; // logical ID
    bool online;
    uint32_t err_count;
    uint32_t crc_fail;
    uint8_t consecutive_errors; // tracks successive failures to avoid false offline alerts
    uint16_t seq; // sequence number for the log record
};

// Array of all I2C devices. To add a new sensor later, just add a line here.
// Keeps track of the hardware address and current health status for each I2C node.
static NodeState s_nodes[] = {
    {PAYLOAD_I2C_ADDRESS_HAL, PAYLOAD_NODE_ID, false, 0, 0, 0, 0},
    {EPS_I2C_ADDRESS_HAL, EPS_NODE_ID, false, 0, 0, 0, 0}, // Day 6
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

/*
 * Semi-Hardcoded Sensor IDs for the MVP.
 * Top 8 bits: Node ID (CRITICAL for SD routing).
 * Bottom 8 bits: VTable Hash of the exact keys requested by the lecturer.
 */
#define SENSOR_ID_PAYLOAD_TEMP     ((uint16_t)(PAYLOAD_NODE_ID << 8) | (VTable_HashName("Temp")     & 0xFF))
#define SENSOR_ID_PAYLOAD_HUMIDITY ((uint16_t)(PAYLOAD_NODE_ID << 8) | (VTable_HashName("Humidity") & 0xFF))
#define SENSOR_ID_PAYLOAD_RAD      ((uint16_t)(PAYLOAD_NODE_ID << 8) | (VTable_HashName("RadFET")   & 0xFF))
#define SENSOR_ID_EPS_BATTERY      ((uint16_t)(EPS_NODE_ID     << 8) | (VTable_HashName("VBat")     & 0xFF))

/* Helper structure for the publish fan-out loop */
struct Measurement {
    uint16_t sensor_id;
    int32_t value;
};

// Publishes data to both the SD queue (as thin records) and the live Snapshot (fat record)
static void publish(NodeState &n, const PayloadData_t &raw)
{
    uint32_t now = osKernelGetTickCount();

    // Break the fat PayloadData_t into individual 16-byte measurements
    Measurement measurements[4];
    uint8_t m_count = 0;

    if (n.node_id == PAYLOAD_NODE_ID) {
        // Extract 3 sensors from the Payload board
        measurements[0] = { SENSOR_ID_PAYLOAD_TEMP, (int32_t)raw.temperature_c_x10 };
        measurements[1] = { SENSOR_ID_PAYLOAD_HUMIDITY, (int32_t)raw.humidity_pct_x10 };
        measurements[2] = { SENSOR_ID_PAYLOAD_RAD, (int32_t)raw.radiation_cps };
        m_count = 3;
    } else if (n.node_id == EPS_NODE_ID) {
        // Extract 1 sensor from the EPS board (Battery)
        measurements[0] = { SENSOR_ID_EPS_BATTERY, (int32_t)raw.battery_pct };
        m_count = 1;
    }

    // Loop over extracted measurements and send each as a separate LogRecord_t to the SD
    for (uint8_t i = 0; i < m_count; ++i) {
        LogRecord_t rec;
        memset(&rec, 0, sizeof(rec)); // Clear memory

        rec.epoch_s = RTC_get_epoch();
        rec.sensor_id = measurements[i].sensor_id;
        rec.type = LOG_RECORD_TYPE_TELEMETRY;
        rec.len = 4; // We are sending 4 bytes of data (int32_t)

        // Safely copy the 4 bytes of the actual measurement value into the payload array
        memcpy(rec.value, &measurements[i].value, sizeof(int32_t));

        // Calculate CRC for this specific 16-byte slot (adjust LOG_RECORD_CRC_SIZE if needed)
        rec.crc32 = Protocol_Crc32((const uint8_t*)&rec, sizeof(LogRecord_t) - sizeof(uint32_t));

        // Put in queue with 0 timeout. If the queue is full, count it as dropped.
        if (osMessageQueuePut(q_telemetryHandle, &rec, 0U, 0U) != osOK) {
            ++s_dropped_frames;
        }
    }

    // Update the live snapshot for GroundComm (UNCHANGED!)
    // The Python GS still needs the fat PayloadData_t structure.
    if (osMutexAcquire(s_snapshot_mtx, 10U) == osOK) {
        Snapshot &sn = s_snapshot[index_of(n.node_id)];
        sn.data = raw;
        sn.obc_time_ms = now;
        sn.valid = true;
        osMutexRelease(s_snapshot_mtx);
    }
}

static void collect_from_node(NodeState &n)
{
	PayloadData_t raw;
	HAL_StatusTypeDef i2c_status = HAL_ERROR; // default value

	if (osMutexAcquire(i2c_mtxHandle, osWaitForever) == osOK) {
		// read 17 bytes from the payload via I2C
		i2c_status = HAL_I2C_Mem_Read(
			&hi2c1,
			n.addr,
			REG_DATA,
			I2C_MEMADD_SIZE_8BIT,
			(uint8_t *)&raw,
			sizeof(raw),
			I2C_TIMEOUT_MS
		);

		osMutexRelease(i2c_mtxHandle);
	}

	if (i2c_status != HAL_OK) {
		++n.err_count;
		++n.consecutive_errors;

		// mark as offline only after 3 consecutive failures
		if (n.consecutive_errors >= 3) {
			n.online = false;
		}
		// skip processing and try again next cycle
		return;
	}
	// success, reset the consecutive error counter
	n.consecutive_errors = 0;

	// validate data integrity (crc32)
	uint32_t computed_crc = Protocol_Crc32((const uint8_t *)&raw, sizeof(raw) - sizeof(raw.crc32));

	if (computed_crc != raw.crc32) {
		 // link corruption. drop the frame but stay online
		++n.crc_fail;
		return;
	}

	n.online = true;
	publish(n, raw);
}

void payload_collector_run()
{
	// Boot marker generation
	LogRecord_t boot_rec;
	memset(&boot_rec, 0, sizeof(boot_rec));

	boot_rec.epoch_s = RTC_get_epoch();
	boot_rec.sensor_id = 0xFFFF; // 0xFFFF signals a System/OBC Event
	boot_rec.type = 0x02; // 0x02 represents LOG_RECORD_TYPE_BOOT
	boot_rec.len = 4; // We are sending 4 bytes of data

	uint32_t current_boot_count = RTC_get_boot_count(); // Fetch from hardware backup register
	memcpy(boot_rec.value, &current_boot_count, sizeof(current_boot_count));

	boot_rec.crc32 = Protocol_Crc32((const uint8_t*)&boot_rec, sizeof(LogRecord_t) - sizeof(uint32_t));

	// Push the Boot Marker to the SD queue immediately upon boot
	osMessageQueuePut(q_telemetryHandle, &boot_rec, 0U, 100U);

	uint32_t next = osKernelGetTickCount();
	while (1) {
		next += COLLECT_PERIOD_TICKS;
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
