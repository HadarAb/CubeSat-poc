/* Shared USART2 buffering, frame parsing, CRC checking, and transmission. */
#pragma once

#include "../crc32.h"
#include "uart_protocol.h"

#include "stm32l4xx_hal.h"

#include <cstring>


class UartTransport
{
public:
    /* Enables interrupt-backed USART2 reception after CubeMX initializes it. */
    void Init(void)
    {
        rx_head = 0u;
        rx_tail = 0u;
        rx_overflow_count = 0u;
        crc_error_count = 0u;
        ResetFrameParser();

        USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        SET_BIT(USART2->CR1, USART_CR1_RXNEIE);

        HAL_NVIC_SetPriority(USART2_IRQn, 5u, 0u);
        HAL_NVIC_EnableIRQ(USART2_IRQn);
    }


    /*
     * Drains queued bytes and returns 1 when one complete CRC-valid message
     * is available. Call repeatedly until it returns 0.
     */
    uint8_t TryReceive(UartReceivedFrame_t* out)
    {
        if (out == nullptr)
        {
            return 0u;
        }

        while (rx_tail != rx_head)
        {
            const uint8_t byte = rx_queue[rx_tail];
            rx_tail = static_cast<uint16_t>((rx_tail + 1u) % RxQueueSize);

            if (ConsumeByte(byte, out) != 0u)
            {
                return 1u;
            }
        }

        return 0u;
    }


    /* Sends one complete binary frame. Returns 1 on success, otherwise 0. */
    uint8_t SendFrame(uint8_t msg_type, uint16_t sequence,
                      const void* payload, uint16_t payload_length)
    {
        if ((payload_length > UART_MAX_PAYLOAD_SIZE)
            || ((payload_length > 0u) && (payload == nullptr)))
        {
            return 0u;
        }

        // add header to the frame
        uint8_t frame[UART_MAX_FRAME_SIZE] = {};
        const UartFrameHeader_t header = {
            UART_FRAME_START,
            msg_type,
            sequence,
            HAL_GetTick(),
            payload_length
        };

        std::memcpy(frame, &header, sizeof(header));

        // add the payload to the frame
        if (payload_length > 0u)
        {
            std::memcpy(&frame[UART_FRAME_HEADER_SIZE], payload, payload_length);
        }

        const uint16_t crc_offset = UART_FRAME_HEADER_SIZE + payload_length;
        const uint32_t crc = Protocol_Crc32(frame, crc_offset);
        std::memcpy(&frame[crc_offset], &crc, sizeof(crc));

        WriteBytes(frame, static_cast<uint16_t>(crc_offset + UART_FRAME_CRC_SIZE));
        return 1u;
    }


    /* Handles USART errors and queues one received byte without blocking. */
    void HandleInterrupt(void)
    {
        const uint32_t status = USART2->ISR;

        if ((status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u)
        {
            USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        }

        if ((status & USART_ISR_RXNE) != 0u)
        {
            const uint8_t byte = static_cast<uint8_t>(USART2->RDR);
            const uint16_t next_head =
                static_cast<uint16_t>((rx_head + 1u) % RxQueueSize);

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


private:
    static constexpr uint16_t RxQueueSize = 256u;

    // the buffer that the interrupt throws raw bytes
    volatile uint8_t rx_queue[RxQueueSize] = {};
    // where to put new data in the buffer
    volatile uint16_t rx_head = 0u;
    // data that can be processed
    volatile uint16_t rx_tail = 0u;
    volatile uint32_t rx_overflow_count = 0u;
    uint32_t crc_error_count = 0u;

    // frame where you construct the msg
    uint8_t frame_buffer[UART_MAX_FRAME_SIZE] = {};
    uint16_t frame_count = 0u;
    uint16_t expected_frame_size = 0u;


    /* Clears the partially collected UART frame. */
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
    // send bytes threw UART
    void WriteBytes(const uint8_t* bytes_arr, uint16_t size)
    {
        for (uint16_t index = 0u; index < size; ++index)
        {
            // check uart transmit register is empty
            // wait till transmit is empty
            while ((USART2->ISR & USART_ISR_TXE) == 0u)
            {
            }
            // access transmit register and write our byte
            //put bytes into transmiter
            USART2->TDR = bytes_arr[index];
        }

        // wait until all bytes have passed .
        //wait till uart finished to send all bytes
        while ((USART2->ISR & USART_ISR_TC) == 0u)
        {
        }
    }


    /*
     * Find start bytes, collect header ,learn expected payload size ,collect the rest
     * ,read received CRC ,calculate CRC ,compare them ,return the message type and sequence if valid
     * */
    uint8_t ConsumeByte(uint8_t byte, UartReceivedFrame_t* out)
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
            else
            {
                // check if its the first part of the starting bit again
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
            if ((header.start != UART_FRAME_START)
                || (header.payload_length > UART_MAX_PAYLOAD_SIZE))
            {
                BeginFrameWithByte(byte);
                return 0u;
            }

            expected_frame_size = static_cast<uint16_t>(
                UART_FRAME_HEADER_SIZE + header.payload_length + UART_FRAME_CRC_SIZE);
        }

        if ((expected_frame_size == 0u) || (frame_count < expected_frame_size))
        {
            return 0u;
        }

        UartFrameHeader_t header = {};
        uint32_t received_crc = 0u;
        std::memcpy(&header, frame_buffer, sizeof(header));
        std::memcpy(&received_crc,
                    &frame_buffer[expected_frame_size - UART_FRAME_CRC_SIZE],
                    sizeof(received_crc));

        const uint32_t calculated_crc =
            Protocol_Crc32(frame_buffer, expected_frame_size - UART_FRAME_CRC_SIZE);

        if (received_crc != calculated_crc)
        {
            ++crc_error_count;
            ResetFrameParser();
            return 0u;
        }

        out->msg_type = header.msg_type;
        out->sequence = header.sequence;
        out->payload_length = header.payload_length;

        // the header check above already rejected payload_length > UART_MAX_PAYLOAD_SIZE
        if (header.payload_length > 0u)
        {
            std::memcpy(out->payload,
                        &frame_buffer[UART_FRAME_HEADER_SIZE],
                        header.payload_length);
        }

        ResetFrameParser();
        return 1u;
    }
};
