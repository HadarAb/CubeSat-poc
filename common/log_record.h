#ifndef LOG_RECORD_H
#define LOG_RECORD_H

#include <stdint.h>

#ifdef __cplusplus
extern "C"
{
#endif

    /* common/log_record.h -- 32 B. Distinct from PayloadData_t (17 B, the I2C wire format). */
    typedef struct __attribute__((packed))
    {
        uint32_t obc_time_ms;      /* 0  OBC clock. THE sort key for FETCH. */
        uint32_t node_time_ms;     /* 4  the node's own tick. Diagnostic only.*/
        uint8_t node_id;           /* 8  logical ID: 0x02 / 0x03 */
        uint8_t rec_type;          /* 9  0=TELEM 1=BOOT 2=EVENT 3=SURVIVAL */
        uint8_t state;             /* 10 SatState_t */
        uint8_t flags;             /* 11 bit0 SEU injected, bit1 link CRC fail*/
        int16_t temperature_c_x10; /* 12 */
        uint16_t humidity_pct_x10; /* 14 */
        uint16_t radiation_cps;    /* 16 */
        uint16_t seq;              /* 18 per-node sequence, detects gaps */
        uint8_t battery_pct;       /* 20 */
        uint8_t reserved[7];       /* 21..27 future EPS fields */
        uint32_t crc32;            /* 28 over bytes 0..27 */
    } LogRecord_t;

/* C/C++ cross-compatible Static Assert */
#ifdef __cplusplus
    static_assert(sizeof(LogRecord_t) == 32, "LogRecord_t must stay 32 bytes");
#else
_Static_assert(sizeof(LogRecord_t) == 32, "LogRecord_t must stay 32 bytes");
#endif

#ifdef __cplusplus
}
#endif

#endif // LOG_RECORD_H
