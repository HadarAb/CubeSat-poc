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
void sd_logger_task(void* argument);

// Read only status used by GroundComm to report SD state changes over UART.
SdLoggerState_t sd_logger_get_state(void);

uint32_t sd_logger_get_error_count(void);
uint32_t sd_logger_get_flush_count(void);

// Queues one validated FETCH request for the SD Logger task. Returns 1 when accepted.
uint8_t sd_logger_request_fetch(uint16_t sequence, const UartFetchPayload_t* request);

#ifdef __cplusplus
}
#endif
