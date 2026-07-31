#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

void ObcController_Init(I2C_HandleTypeDef* i2c_handle);
void ObcController_Process(void);

#ifdef __cplusplus
}
#endif
