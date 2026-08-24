/* Public OBC controller interface for requested and automatic UART traffic. */
#pragma once

#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Initializes the OBC I2C master and framed UART protocol. */
void ObcController_Init(I2C_HandleTypeDef* i2c_handle);

/* Processes received UART commands and sends scheduled automatic status. */
void ObcController_Process(void);

#ifdef __cplusplus
}
#endif
