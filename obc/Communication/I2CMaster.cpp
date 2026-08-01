/* Implements OBC-side I2C register reads and payload CRC validation. */
#include "I2CMaster.hpp"

#include "../../common/crc32.h"

namespace
{
constexpr uint32_t I2cTimeoutMs = 100u;
I2C_HandleTypeDef* master_i2c = nullptr;

/*
 * Reads data from a selected register of an I2C slave.
 *
 * slave_address: address of the slave device.
 * register_address: the command
 * output: buffer where the received data is stored.
 *
 * Returns the HAL communication status.
 */
HAL_StatusTypeDef ReadRegister(uint16_t slave_address, uint8_t register_address,
                               uint8_t* output, uint16_t output_size)
{
    return HAL_I2C_Mem_Read(master_i2c, slave_address, register_address,
                            I2C_MEMADD_SIZE_8BIT, output, output_size, I2cTimeoutMs);
}
}

void I2CMaster_Init(I2C_HandleTypeDef* i2c_handle)
{
    // stores i2c context in master_i2c
    master_i2c = i2c_handle;
}

HAL_StatusTypeDef I2CMaster_ReadWhoAmI(uint16_t slave_address, uint8_t* node_id)
{
    if ((master_i2c == nullptr) || (node_id == nullptr))
    {
        return HAL_ERROR;
    }

    return ReadRegister(slave_address, REG_WHOAMI, node_id, 1u);
}

/* Reads a complete payload sample and compares its stored CRC with a new calculation. */
HAL_StatusTypeDef I2CMaster_ReadPayloadData(uint16_t slave_address, PayloadData_t* payload_data,
                                            uint8_t* crc_valid, uint32_t* calculated_crc)
{
    if ((master_i2c == nullptr) || (payload_data == nullptr) || (crc_valid == nullptr))
    {
        return HAL_ERROR;
    }
    // set defult at the start
    *crc_valid = 0u;

    // recive data set crc and the data , saves it inside payload_data
    const HAL_StatusTypeDef status = ReadRegister(slave_address, REG_DATA,
                                                   reinterpret_cast<uint8_t*>(payload_data),
                                                   PAYLOAD_DATA_WIRE_SIZE);

    if (status != HAL_OK)
    {
        return status;
    }

    //calculate ur own crc
    const uint32_t crc = Protocol_Crc32(reinterpret_cast<const uint8_t*>(payload_data),
                                        PAYLOAD_DATA_CRC_SIZE);

    if (calculated_crc != nullptr)
    {
        *calculated_crc = crc;
    }
    //check if crc codes match
    *crc_valid = (crc == payload_data->crc32) ? 1u : 0u;

    return HAL_OK;
}
