// Single-owner FatFs telemetry logger used by Task_SD_Logger.
#pragma once

#include "../../common/uart/uart_protocol.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum
{
    SD_LOGGER_INITIALIZING = 0,
    SD_LOGGER_READY = 1,
    SD_LOGGER_ERROR = 2
} SdLoggerState_t;

// FreeRTOS task entry. All helper FatFs calls execute in this task's context.
void SdLogger_Task(void* argument);

// Read only status used by GroundComm to report SD state changes over UART.
SdLoggerState_t SdLogger_GetState(void);

uint32_t SdLogger_GetErrorCount(void);
uint32_t SdLogger_GetFlushCount(void);

// Queues one validated FETCH request for the SD Logger task. Returns 1 when accepted.
uint8_t SdLogger_RequestFetch(uint16_t sequence, const UartFetchPayload_t* request);

#ifdef __cplusplus
}
#endif
