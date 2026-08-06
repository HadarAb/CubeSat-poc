/* Public OBC interface for reading the payload through the shared I2C register protocol. */
#pragma once

#include "../../common/protocol.h"
#include "../../common/legacy_payload_data.h"
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Gives the module access to the I2C context and stores it in i2c_handle. */
void I2CMaster_Init(I2C_HandleTypeDef* i2c_handle);

/* Send REG_WHOAMI to the slave address Read 1 byte back Store that byte inside node_id */
HAL_StatusTypeDef I2CMaster_ReadWhoAmI(uint16_t slave_address, uint8_t* node_id);

/* Send REG_DATA to the slave address Read the whole PayloadData_t struct
 *  ,Store it inside payload_data ,Calculate the CRC*/
HAL_StatusTypeDef I2CMaster_ReadPayloadData(uint16_t slave_address, PayloadData_t* payload_data,
                                            uint8_t* crc_valid, uint32_t* calculated_crc);

#ifdef __cplusplus
}
#endif
