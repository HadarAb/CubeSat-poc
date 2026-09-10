// Framed PC-simulator UART interface for the EPS node.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Enables interrupt for uart after CubeMX function MX_USART2_UART_Init().
void eps_uart_init(void);

// Drains received frames, updates the VTable, and sends SIM_ACK responses.
void eps_uart_process(void);

// fast function that works on an interrupt grabs and saves data .
void eps_uart_handle_interrupt(void);

#ifdef __cplusplus
}
#endif
