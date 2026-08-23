#ifndef CUBESAT_COMMON_CRC16_H
#define CUBESAT_COMMON_CRC16_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * CRC-16/CCITT-FALSE.
 *
 * Polynomial:    0x1021
 * Initial value: 0xFFFF
 * Reflected:     no
 * Final XOR:     0x0000
 *
 * Required test vector:
 *   Protocol_Crc16("123456789", 9) == 0x29B1
 */
static inline uint16_t Protocol_Crc16(const uint8_t* bytes, uint32_t size)
{
    uint16_t crc = 0xFFFFu;

    for (uint32_t index = 0u; index < size; ++index)
    {
        crc ^= (uint16_t)((uint16_t)bytes[index] << 8u);

        for (uint8_t bit = 0u; bit < 8u; ++bit)
        {
            if ((crc & 0x8000u) != 0u)
            {
                crc = (uint16_t)((crc << 1u) ^ 0x1021u);
            }
            else
            {
                crc = (uint16_t)(crc << 1u);
            }
        }
    }

    return crc;
}

#ifdef __cplusplus
}
#endif

#endif
