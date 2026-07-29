#pragma once

#include "../../common/protocol.h"
#include "stm32l4xx_hal.h"

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Minimal transmit-only USART2 debug console for the NUCLEO-L476RG ST-LINK
 * virtual COM port: PA2, 115200 baud, 8 data bits, no parity, 1 stop bit.
 */
void DebugUart_Init(void);
void DebugUart_ReportWhoAmI(HAL_StatusTypeDef i2c_status,uint8_t node_id);
void DebugUart_ReportPayload(HAL_StatusTypeDef i2c_status,const PayloadData_t* payload_data,
								uint8_t crc_valid,uint32_t calculated_crc);

#ifdef __cplusplus
}
#endif
