#include "UartProtocol.hpp"

#include "../../common/crc32.h"
#include "stm32l4xx_hal.h"

#include <cstring>

namespace
{
constexpr uint32_t UartBaudRate = 115200u;
constexpr uint16_t RxQueueSize = 128u;

volatile uint8_t rx_queue[RxQueueSize] = {};
volatile uint16_t rx_head = 0u;
volatile uint16_t rx_tail = 0u;
volatile uint32_t rx_overflow_count = 0u;

uint8_t frame_buffer[UART_MAX_FRAME_SIZE] = {};
uint16_t frame_count = 0u;
uint16_t expected_frame_size = 0u;
uint16_t debug_sequence = 0u;

void ResetFrameParser(void)
{
    frame_count = 0u;
    expected_frame_size = 0u;
}

//check if first byte is start byte FF
void BeginFrameWithByte(uint8_t byte)
{
    ResetFrameParser();

    if (byte == static_cast<uint8_t>(UART_FRAME_START & 0xFFu))
    {
        frame_buffer[0] = byte;
        frame_count = 1u;
    }
}


/*transmit a msg to the other end . */
void WriteBytes(const uint8_t* bytes_arr, uint16_t size)
{
    for (uint16_t index = 0u; index < size; ++index)
    {
    	// check uart transmit register is empty
        while ((USART2->ISR & USART_ISR_TXE) == 0u)
        {
        }
        // access transmit register and write our byte
        USART2->TDR = bytes_arr[index];
    }

    // wait until all bytes have passed .
    while ((USART2->ISR & USART_ISR_TC) == 0u)
    {
    }
}

/*
 * Find start bytes, collect header ,learn expected payload size ,collect the rest
 * ,read received CRC ,calculate CRC ,compare them ,return the message type and sequence if valid
 * */
uint8_t ConsumeByte(uint8_t byte, uint8_t* msg_type, uint16_t* sequence)
{
	// get first 8 and 16 bits to check is it a start of a msg
    const uint8_t start_low = static_cast<uint8_t>(UART_FRAME_START);
    const uint8_t start_high = static_cast<uint8_t>(UART_FRAME_START >> 8u);

    // if its the first byte we still didnt read
    if (frame_count == 0u)
    {
    	// check if we are at the start of the frame
        if (byte == start_low)
        {
        	//write it to the buffer
            frame_buffer[0] = byte;
            frame_count = 1u;
        }

        return 0u;
    }

    //need to check is it the second start byte
    if (frame_count == 1u)
    {
        if (byte == start_high)
        {
            frame_buffer[1] = byte;
            frame_count = 2u;
        }
        // check if its the first part of the starting bit again
        else
        {
            BeginFrameWithByte(byte);
        }

        return 0u;
    }

    //if frame buffer is full resets and checks is it a new msg
    if (frame_count >= sizeof(frame_buffer))
    {
        BeginFrameWithByte(byte);
        return 0u;
    }

    // write new byte to buffer
    frame_buffer[frame_count] = byte;
    ++frame_count;

    //we just finished with uart header frame
    if (frame_count == UART_FRAME_HEADER_SIZE)
    {
        UartFrameHeader_t header = {};
        std::memcpy(&header, frame_buffer, sizeof(header));

        //checks start is correct and payload is correct size
        if ((header.start != UART_FRAME_START) || (header.payload_length > UART_MAX_PAYLOAD_SIZE))
        {
            BeginFrameWithByte(byte);
            return 0u;
        }

        expected_frame_size = static_cast<uint16_t>(UART_FRAME_HEADER_SIZE+ header.payload_length
        												+ UART_FRAME_CRC_SIZE);
    }
    if ((expected_frame_size == 0u) || (frame_count < expected_frame_size))
    {
        return 0u;
    }

    UartFrameHeader_t header = {};
    uint32_t received_crc = 0u;
    std::memcpy(&header, frame_buffer, sizeof(header));
    std::memcpy(&received_crc,&frame_buffer[expected_frame_size - UART_FRAME_CRC_SIZE],
        sizeof(received_crc));

    const uint32_t calculated_crc = Protocol_Crc32(frame_buffer,expected_frame_size - UART_FRAME_CRC_SIZE);
    uint8_t frame_is_valid = 0u;

	if(received_crc == calculated_crc) {
    	frame_is_valid = 1u;
    }
    else {
    	frame_is_valid = 0u;
    }


    if (frame_is_valid != 0u)
    {
        if (header.payload_length == 0u)
        {
            *msg_type = header.msg_type;
            *sequence = header.sequence;
            ResetFrameParser();
            return 1u;
        }

        UartPayload_t error = {};
        error.status = UART_STATUS_BAD_REQUEST;
        UartProtocol_SendFrame(UART_MSG_ERROR, header.sequence, &error,sizeof(error));
    }

    ResetFrameParser();
    return 0u;
}
}

void UartProtocol_Init(void)
{
    __HAL_RCC_USART2_CLK_ENABLE();

    USART2->CR1 = 0u;
    USART2->CR2 = 0u;
    USART2->CR3 = 0u;
    USART2->BRR = (HAL_RCC_GetPCLK1Freq() + (UartBaudRate / 2u)) / UartBaudRate;
    USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF| USART_ICR_NCF;
    USART2->CR1 = USART_CR1_TE | USART_CR1_RE| USART_CR1_RXNEIE | USART_CR1_UE;

    HAL_NVIC_SetPriority(USART2_IRQn, 5u, 0u);
    HAL_NVIC_EnableIRQ(USART2_IRQn);
}

/* check if we got a full message already  */
uint8_t UartProtocol_TrygetMessage(uint8_t* msg_type,uint16_t* sequence)
{
    if ((msg_type == nullptr) || (sequence == nullptr))
    {
        return 0u;
    }

    while (rx_tail != rx_head)
    {
        const uint8_t byte = rx_queue[rx_tail];
        rx_tail = static_cast<uint16_t>((rx_tail + 1u) % RxQueueSize);

        if (ConsumeByte(byte, msg_type, sequence) != 0u)
        {
            return 1u;
        }
    }

    return 0u;
}

/* helps you send a full frame threw UART */
uint8_t UartProtocol_SendFrame(uint8_t msg_type,uint16_t sequence, const void* payload,
								uint16_t payload_length)
{
    if ((payload_length > UART_MAX_PAYLOAD_SIZE) || ((payload_length > 0u) && (payload == nullptr)))
    {
        return 0u;
    }
    // add header to the frame
    uint8_t frame[UART_MAX_FRAME_SIZE] = {};
    const UartFrameHeader_t header = {UART_FRAME_START,msg_type,sequence,HAL_GetTick(),payload_length};

    std::memcpy(frame, &header, sizeof(header));

    // add the payload to the frame
    if (payload_length > 0u)
    {
        std::memcpy(&frame[UART_FRAME_HEADER_SIZE],payload,payload_length);
    }


    const uint16_t crc_offset = UART_FRAME_HEADER_SIZE + payload_length;
    const uint32_t crc = Protocol_Crc32(frame, crc_offset);
    std::memcpy(&frame[crc_offset], &crc, sizeof(crc));

    WriteBytes(frame, static_cast<uint16_t>(crc_offset + UART_FRAME_CRC_SIZE));
    return 1u;
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
    UartProtocol_SendFrame(UART_MSG_DEBUG_TEXT,debug_sequence,text,length);
}

extern "C" void USART2_IRQHandler(void)
{
    const uint32_t status = USART2->ISR;

    if ((status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u)
    {
        USART2->ICR = USART_ICR_ORECF| USART_ICR_FECF| USART_ICR_NCF;
    }

    if ((status & USART_ISR_RXNE) != 0u)
    {
        const uint8_t byte = static_cast<uint8_t>(USART2->RDR);
        const uint16_t next_head =static_cast<uint16_t>((rx_head + 1u) % RxQueueSize);

        if (next_head != rx_tail)
        {
            rx_queue[rx_head] = byte;
            rx_head = next_head;
        }
        else
        {
            ++rx_overflow_count;
        }
    }
}
