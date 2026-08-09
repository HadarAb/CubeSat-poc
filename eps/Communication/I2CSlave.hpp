/* Public interface for the EPS-side I2C register slave. */
#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Configures the EPS address and starts interrupt-based I2C listening. */
HAL_StatusTypeDef I2CSlave_Init(I2C_HandleTypeDef* i2c_handle);

#ifdef __cplusplus
}
#endif
