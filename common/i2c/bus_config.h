#ifndef BUS_CONFIG_H
#define BUS_CONFIG_H

#include <stdint.h>

/*
 * Physical I2C bus addresses. Legal 7-bit range is 0x08-0x77 —
 * 0x00-0x07 and 0x78-0x7F are reserved by the I2C spec (NXP UM10204, Table 4)
 * and must never be used here, even on a private bus.
 */

#define I2C_ADDR8(a7) ((uint16_t)((a7) << 1))

#define PAYLOAD_I2C_ADDRESS_7BIT   0x20U
#define PAYLOAD_I2C_ADDRESS_HAL    I2C_ADDR8(PAYLOAD_I2C_ADDRESS_7BIT)

#define EPS_I2C_ADDRESS_7BIT       0x21U
#define EPS_I2C_ADDRESS_HAL        I2C_ADDR8(EPS_I2C_ADDRESS_7BIT)

#endif
