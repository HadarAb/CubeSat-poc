// Connects OBC-specific UART functions to the shared framed UART transport.
#include "UartProtocol.hpp"

#include "../../common/uart/uart_transport.hpp"


namespace
{
UartTransport uart_transport;
uint16_t debug_sequence = 0u;
}


void uart_protocol_init(void)
{
    debug_sequence = 0u;
    uart_transport.init();
}


// check if we got a full message already
extern "C" uint8_t uart_protocol_try_receive_request(UartRequest_t* out)
{
    return uart_transport.try_receive(out);
}


// helps you send a full frame through UART
uint8_t uart_protocol_send_frame(uint8_t msg_type, uint16_t sequence,
                               const void* payload, uint16_t payload_length)
{
    return uart_transport.send_frame(msg_type, sequence, payload, payload_length);
}


// debug text to send through UART
void send_uart_msg(const char* text)
{
    if (text == nullptr)
    {
        return;
    }

    uint16_t length = 0u;
    while ((text[length] != '\0') && (length < UART_MAX_PAYLOAD_SIZE))
    {
        ++length;
    }

    ++debug_sequence;
    uart_protocol_send_frame(UART_MSG_DEBUG_TEXT, debug_sequence, text, length);
}


extern "C" void uart_protocol_handle_interrupt(void)
{
    uart_transport.handle_interrupt();
}
