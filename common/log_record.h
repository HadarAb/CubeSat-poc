#ifndef CUBESAT_COMMON_LOG_RECORD_H
#define CUBESAT_COMMON_LOG_RECORD_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

/* Record types written to the telemetry files. */
#define LOG_RECORD_TYPE_TELEMETRY 0u
#define LOG_RECORD_TYPE_BOOT 1u
#define LOG_RECORD_TYPE_EVENT 2u
#define LOG_RECORD_TYPE_SURVIVAL 3u

#define LOG_RECORDS_PER_SECTOR 32u
#define LOG_SECTOR_SIZE_BYTES 512u

/* Reserved sensor IDs. High byte is the logical node ID, so RouteRecord
 * sends this record to the EPS directory. */
#define SENSOR_ID_BOOT 0x03FFu

	/*
	 * Fixed on-disk telemetry format.
	 *
	 * Thirty-two records fill one 512-byte FatFs sector exactly. The CRC covers
	 * bytes 0..11 and is computed by Task_SD_Logger immediately before writing.
	 */
	typedef struct __attribute__((packed))
	{
		uint32_t epoch_s;      /* 4 bytes: Unix epoch time from RTC */
        uint16_t sensor_id;    /* 2 bytes: Identifier of the sensor/data source */
        uint8_t  type;         /* 1 byte: Telemetry, boot, event, or survival */
        uint8_t  len;          /* 1 byte: Length of valid data in the value field */
        uint8_t  value[4];     /* 4 bytes: The actual telemetry data */
        uint32_t crc32;        /* 4 bytes: CRC32 of the first 12 bytes of this record */
	} LogRecord_t;

#define LOG_RECORD_CRC_SIZE ((uint32_t)offsetof(LogRecord_t, crc32))

#ifdef __cplusplus
}

static_assert(sizeof(LogRecord_t) == 16u,
              "LogRecord_t must stay 16 bytes");
static_assert(offsetof(LogRecord_t, crc32) == 12u,
              "LogRecord_t CRC must stay at offset 12");
#else
_Static_assert(sizeof(LogRecord_t) == 16u,
               "LogRecord_t must stay 16 bytes");
_Static_assert(offsetof(LogRecord_t, crc32) == 12u,
               "LogRecord_t CRC must stay at offset 12");
#endif

#endif
