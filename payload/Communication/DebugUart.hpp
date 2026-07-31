#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Transmit-only USART2 debug console for the NUCLEO-L476RG ST-LINK
 * virtual COM port: PA2, 115200 baud, 8 data bits, no parity, 1 stop bit.
 */
void DebugUart_Init(void);

/*
 * Generic debug messages that may be called from normal application code.
 *
 * Examples:
 *   DebugUart_Print("Payload simulation updated\r\n");
 *   DebugUart_Printf("Register=0x%02X count=%lu\r\n", reg, count);
 *
 * Do not call these blocking functions directly from an interrupt callback.
 */
void DebugUart_Print(const char* text);
void DebugUart_Printf(const char* format, ...);

#ifdef __cplusplus
}
#endif
