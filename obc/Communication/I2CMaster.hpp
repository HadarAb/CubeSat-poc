/* Public OBC interface for reading the payload through the shared I2C register protocol. */
#pragma once

#include "../../common/i2c/protocol.h"
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gives the module access to the I2C context and stores it in i2c_handle. */
void I2CMaster_Init(I2C_HandleTypeDef* i2c_handle);

/* Send REG_WHOAMI to the slave address Read 1 byte back Store that byte inside node_id */
HAL_StatusTypeDef I2CMaster_ReadWhoAmI(uint16_t slave_address, uint8_t* node_id);

typedef enum
{
    I2C_KEY_READ_OK = 0,
    I2C_KEY_READ_MISSING,
    I2C_KEY_READ_BUS_ERROR,
    I2C_KEY_READ_CRC_ERROR,
    I2C_KEY_READ_FORMAT_ERROR
} I2CKeyReadResult_t;

/* Selects one VTable key, reads REG_VT_VALUE, and validates its wire contract. */
I2CKeyReadResult_t I2CMaster_ReadKey(uint16_t slave_address, const char* key,
                                     VtType_t expected_type, VtValueWire_t* value);

#ifdef __cplusplus
}
#endif
