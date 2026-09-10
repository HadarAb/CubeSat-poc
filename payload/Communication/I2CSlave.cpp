// Selects the Payload identity for the shared I2C VTable slave.
#include "I2CSlave.hpp"

#include "../../common/i2c/bus_config.h"
#include "../../common/i2c/i2c_slave.hpp"
#include "../../common/i2c/protocol.h"


HAL_StatusTypeDef i2c_slave_init(I2C_HandleTypeDef* i2c_handle)
{
    return common_i2c_slave_init(
        i2c_handle,
        PAYLOAD_I2C_ADDRESS_HAL,
        PAYLOAD_NODE_ID);
}
