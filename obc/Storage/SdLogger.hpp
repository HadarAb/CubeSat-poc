// Single-owner FatFs telemetry logger used by Task_SD_Logger.
#pragma once

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

#ifdef __cplusplus
}
#endif
