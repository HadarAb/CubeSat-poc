/* Connects OBC-specific UART functions to the shared framed UART transport. */
#include "UartProtocol.hpp"

#include "../../common/uart/uart_transport.hpp"


namespace
{
UartTransport uart_transport;
uint16_t debug_sequence = 0u;
}


void UartProtocol_Init(void)
{
    debug_sequence = 0u;
    uart_transport.Init();
}


/* check if we got a full message already  */
extern "C" uint8_t UartProtocol_TryReceiveRequest(UartRequest_t* out)
{
    return uart_transport.TryReceive(out);
}


/* helps you send a full frame threw UART */
uint8_t UartProtocol_SendFrame(uint8_t msg_type, uint16_t sequence,
                               const void* payload, uint16_t payload_length)
{
    return uart_transport.SendFrame(msg_type, sequence, payload, payload_length);
}


/* debug text to send threw UART */
void SendUartMsg(const char* text)
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
    UartProtocol_SendFrame(UART_MSG_DEBUG_TEXT, debug_sequence, text, length);
}


extern "C" void UartProtocol_HandleInterrupt(void)
{
    uart_transport.HandleInterrupt();
}
