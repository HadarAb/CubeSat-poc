/* Implements OBC-side I2C register and VTable key reads. */
#include "I2CMaster.hpp"

#include "../../common/i2c/crc16.h"

#include <cstring>

namespace
{
constexpr uint32_t I2cTimeoutMs = 20u;
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

/*request the other side id */
HAL_StatusTypeDef I2CMaster_ReadWhoAmI(uint16_t slave_address, uint8_t* node_id)
{
    if ((master_i2c == nullptr) || (node_id == nullptr))
    {
        return HAL_ERROR;
    }

    return ReadRegister(slave_address, REG_WHOAMI, node_id, 1u);
}

/* request value by name */
I2CKeyReadResult_t I2CMaster_ReadKey(uint16_t slave_address,const char* key,
                                     VtType_t expected_type, VtValueWire_t* value)
{
	//check starting verbs are set
    if ((master_i2c == nullptr) || (key == nullptr) || (value == nullptr)) {
        return I2C_KEY_READ_FORMAT_ERROR;
    }

    //creats space for the command and the name of the key
    uint8_t select_request[1u + VT_NAME_LEN] = {};
    //first byte puts the command (select)
    select_request[0] = REG_VT_SELECT;

    uint8_t name_length = 0u;
    //puts the name byte by byte
    while ((name_length < VT_NAME_LEN) && (key[name_length] != '\0')) {
        select_request[1u + name_length] = static_cast<uint8_t>(key[name_length]);
        ++name_length;
    }
    if ((name_length == 0u)
        || ((name_length == VT_NAME_LEN) && (key[name_length] != '\0'))) {
        return I2C_KEY_READ_FORMAT_ERROR;
    }

    //transmit the request of the sensor we want a value of
    HAL_StatusTypeDef status = HAL_I2C_Master_Transmit(master_i2c,slave_address,
    								select_request, sizeof(select_request),I2cTimeoutMs);

    if (status != HAL_OK) {
        return I2C_KEY_READ_BUS_ERROR;
    }

    //clear value
    std::memset(value, 0, sizeof(*value));

    //here we actually get the value of the sensor we sent the name before .
    status = ReadRegister(slave_address,REG_VT_VALUE,reinterpret_cast<uint8_t*>(value),
    								VT_VALUE_WIRE_SIZE);
    if (status != HAL_OK) {
        return I2C_KEY_READ_BUS_ERROR;
    }

    //calculate crc
    const uint16_t calculated_crc = Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(value), VT_VALUE_CRC_SIZE);

    if (calculated_crc != value->crc16) {
        return I2C_KEY_READ_CRC_ERROR;
    }

    if (value->len == 0u) {
        return I2C_KEY_READ_MISSING;
    }

    if ((value->type != static_cast<uint8_t>(expected_type))
        || (value->len != sizeof(uint32_t))) {
        return I2C_KEY_READ_FORMAT_ERROR;
    }

    return I2C_KEY_READ_OK;
}
