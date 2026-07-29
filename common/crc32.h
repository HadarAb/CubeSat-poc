#ifndef CUBESAT_COMMON_CRC32_H
#define CUBESAT_COMMON_CRC32_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CRC-32/ISO-HDLC (the same result as zlib.crc32).
 *
 * Required test vector:
 *   Protocol_Crc32("123456789", 9) == 0xCBF43926
 */
static inline uint32_t Protocol_Crc32(
    const uint8_t* bytes,
    uint32_t size)
{
    uint32_t crc = 0xFFFFFFFFu;

    for (uint32_t index = 0; index < size; ++index)
    {
        crc ^= bytes[index];

        for (uint8_t bit = 0; bit < 8u; ++bit)
        {
            if ((crc & 1u) != 0u)
            {
                crc = (crc >> 1u) ^ 0xEDB88320u;
            }
            else
            {
                crc >>= 1u;
            }
        }
    }

    return ~crc;
}

#ifdef __cplusplus
}
#endif

#endif
