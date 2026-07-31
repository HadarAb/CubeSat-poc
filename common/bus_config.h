#ifndef BUS_CONFIG_H
#define BUS_CONFIG_H

#include "stdint.h"

// the real address (7 bit) of our first slave
#define NODE_ADDR_PAYLOAD_7BIT 0x20U

#define I2C_ADDR8(a7) ((uint16_t)((a7) << 1))

// the shifted address the hal needs (0x40)
#define NODE_ADDR_PAYLOAD_8BIT I2C_ADDR8(NODE_ADDR_PAYLOAD_7BIT)

#endif