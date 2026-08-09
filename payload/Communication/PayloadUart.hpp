/* Framed PC-simulator UART interface for the Payload node. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Enables interrupt for uart after cobeMX function MX_USART2_UART_Init(). */
void PayloadUart_Init(void);

/* Drains received frames, updates the VTable, and sends SIM_ACK responses. */
void PayloadUart_Process(void);

/* fast function that works on an interrupt grabs and saves data . */
void PayloadUart_HandleInterrupt(void);

#ifdef __cplusplus
}
#endif
