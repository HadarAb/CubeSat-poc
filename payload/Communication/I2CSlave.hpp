#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

HAL_StatusTypeDef I2CSlave_Init(I2C_HandleTypeDef* i2c_handle);

#ifdef __cplusplus
}
#endif
