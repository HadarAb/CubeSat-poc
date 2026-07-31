#pragma once

#include "../../common/uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* USART2 on the NUCLEO ST-LINK virtual COM port, 115200 baud, 8N1. */
void UartProtocol_Init(void);

/*
 * Drains the interrupt-backed receive queue and returns 1 when one complete,
 * CRC-valid request is available. Requests have a header and no payload.
 * Call repeatedly until it returns 0.
 */
uint8_t UartProtocol_TryReceiveMessage(
    uint8_t* msg_type,
    uint16_t* sequence);

/* Sends one complete binary frame. Returns 1 on success, otherwise 0. */
uint8_t UartProtocol_SendFrame(uint8_t msg_type, uint16_t sequence,
								const void* payload, uint16_t payload_length);

/*
 * Sends a null-terminated debug string as a CRC-protected DEBUG_TEXT frame.
 * It is safe to use on the same UART as ground-station commands.
 */
void SendUartMsg(const char* text);

#ifdef __cplusplus
}
#endif
