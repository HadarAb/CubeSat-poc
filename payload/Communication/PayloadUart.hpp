// Framed PC-simulator UART interface for the Payload node.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Enables interrupt for uart after CubeMX function MX_USART2_UART_Init().
void payload_uart_init(void);

// Drains received frames, updates the VTable, and sends SIM_ACK responses.
void payload_uart_process(void);

// fast function that works on an interrupt grabs and saves data .
void payload_uart_handle_interrupt(void);

#ifdef __cplusplus
}
#endif
