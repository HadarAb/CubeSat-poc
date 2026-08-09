/* Selects the EPS identity for the shared I2C VTable slave. */
#include "I2CSlave.hpp"

#include "../../common/i2c/bus_config.h"
#include "../../common/i2c/i2c_slave.hpp"
#include "../../common/i2c/protocol.h"


HAL_StatusTypeDef I2CSlave_Init(I2C_HandleTypeDef* i2c_handle)
{
    return CommonI2CSlave_Init(
        i2c_handle,
        EPS_I2C_ADDRESS_HAL,
        EPS_NODE_ID);
}
