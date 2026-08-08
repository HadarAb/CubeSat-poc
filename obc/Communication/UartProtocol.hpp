/* Public interface for CRC-protected UART frames used by the OBC ground station. */
#pragma once

#include "../../common/uart_protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Moves received USART2 bytes into the protocol receive queue. */
void UartProtocol_HandleInterrupt(void);

/* USART2 on the NUCLEO ST-LINK virtual COM port, 115200 baud, 8N1. */
void UartProtocol_Init(void);

/*
 * One decoded, CRC-valid inbound request. The payload is copied out of the
 * parser, so it stays valid after the parser moves on to the next frame.
 */
typedef UartReceivedFrame_t UartRequest_t;

/*Reads UART messages from the queue and returns 1 when a full valid message is ready */
uint8_t UartProtocol_TryReceiveRequest(UartRequest_t* out);

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
