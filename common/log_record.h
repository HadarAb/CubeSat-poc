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

    /*
     * Fixed on-disk telemetry format.
     *
     * Sixteen records fill one 512-byte FatFs sector exactly. The CRC covers
     * bytes 0..27 and is computed by Task_SD_Logger immediately before writing.
     */
    typedef struct __attribute__((packed))
    {
        uint32_t obc_time_ms;      /* Persistent OBC time used to sort records. */
        uint32_t node_time_ms;     /* Node's own boot-relative diagnostic time. */
        uint8_t node_id;           /* Logical node ID, for example Payload 0x02. */
        uint8_t rec_type;          /* Telemetry, boot, event, or survival record. */
        uint8_t state;             /* Current satellite power-state value. */
        uint8_t flags;             /* Error/event bits copied from the collector. */
        int16_t temperature_c_x10; /* 243 means 24.3 degrees Celsius. */
        uint16_t humidity_pct_x10; /* 512 means 51.2 percent humidity. */
        uint16_t radiation_cps;    /* Radiation detector counts per second. */
        uint16_t seq;              /* Sequence number used to detect lost data. */
        uint8_t battery_pct;       /* Battery charge from 0 to 100 percent. */
        uint8_t reserved[7];       /* Kept empty for future fields. */
        uint32_t crc32;            /* CRC of the first 28 bytes of this record. */
    } LogRecord_t;

#define LOG_RECORD_CRC_SIZE ((uint32_t)offsetof(LogRecord_t, crc32))

#ifdef __cplusplus
}

static_assert(sizeof(LogRecord_t) == 32u,
              "LogRecord_t must stay 32 bytes");
static_assert(offsetof(LogRecord_t, crc32) == 28u,
              "LogRecord_t CRC must stay at offset 28");
#else
_Static_assert(sizeof(LogRecord_t) == 32u,
               "LogRecord_t must stay 32 bytes");
_Static_assert(offsetof(LogRecord_t, crc32) == 28u,
               "LogRecord_t CRC must stay at offset 28");
#endif

#endif