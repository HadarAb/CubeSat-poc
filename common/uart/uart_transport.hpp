/* Shared USART2 interrupt buffering, frame parsing, CRC checking, and transmission. */
#pragma once

#include "../crc32.h"
#include "uart_protocol.h"

#include "stm32l4xx_hal.h"

#include <cstring>


class UartTransport
{
public:
    /* init uart clears every thing , and makes ready to transmit interrupt work only
     * when there are actual bytes to send  */
    void Init(void)
    {
        rx_head = 0u;
        rx_tail = 0u;
        rx_overflow_count = 0u;
        tx_head = 0u;
        tx_tail = 0u;
        tx_overflow_count = 0u;
        crc_error_count = 0u;
        ResetFrameParser();

        // TXE is enabled only when bytes are waiting in the software TX queue.
        // disable ready to send interrupt
        CLEAR_BIT(USART2->CR1, USART_CR1_TXEIE);
        // clears all flags
        USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        // enable ready to send interrupt when there are bytes to send
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


    /* Builds and queues one complete frame. Returns 0 if the TX queue has no room. */
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

        // Copy the complete frame into the TX queue before this stack buffer disappears
        const uint16_t frame_size = static_cast<uint16_t>(crc_offset + UART_FRAME_CRC_SIZE);
        return QueueBytes(frame, frame_size);
    }

    /* This function is getting triggered every UART interrupt and handles
     * sending or receiving bytes.
     * So when UART is ready for TX, it will trigger an interrupt and this function
     * will get triggered
     * it will send the next byte.
     * If we got a byte on RX, the interrupt will trigger and this function
     * will read the byte and save it.
     */
    void HandleInterrupt(void)
    {
    	//read uart status register (is uart ready , any errors , byte arrived)
        const uint32_t status = USART2->ISR;

        //checks for errors
        if ((status & (USART_ISR_ORE | USART_ISR_FE | USART_ISR_NE)) != 0u)
        {
        	//clears the errors
            USART2->ICR = USART_ICR_ORECF | USART_ICR_FECF | USART_ICR_NCF;
        }
        // did we recive a new byte
        if ((status & USART_ISR_RXNE) != 0u)
        {
        	//if ye read the byte
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
        // is uart ready to send anouther byte is TX interrupt enabled
        // handles the sending part
        if (((status & USART_ISR_TXE) != 0u)
            && ((USART2->CR1 & USART_CR1_TXEIE) != 0u))
        {
            if (tx_tail != tx_head)
            {
                // Writing TDR clears TXE. Hardware interrupts again when it is ready.
                USART2->TDR = tx_queue[tx_tail];
                tx_tail = static_cast<uint16_t>((tx_tail + 1u) % TxQueueSize);
            }

            if (tx_tail == tx_head)
            {
                // No bytes remain, so stop TX interrupts until another frame is queued.
                CLEAR_BIT(USART2->CR1, USART_CR1_TXEIE);
            }
        }
    }


private:
    static constexpr uint16_t RxQueueSize = 256u;
    static constexpr uint16_t TxQueueSize = 2048u;

    /* Raw bytes received by the UART interrupt wait in this ring. */
    volatile uint8_t rx_queue[RxQueueSize] = {};
    /* The interrupt writes new RX bytes at rx_head. */
    volatile uint16_t rx_head = 0u;
    /* TryReceive reads pending RX bytes at rx_tail. */
    volatile uint16_t rx_tail = 0u;
    volatile uint32_t rx_overflow_count = 0u;

    /* Complete frames wait here while USART2 sends one byte at a time. */
    volatile uint8_t tx_queue[TxQueueSize] = {};
    /* The task writes at tx_head; the UART interrupt reads at tx_tail. */
    volatile uint16_t tx_head = 0u;
    volatile uint16_t tx_tail = 0u;
    volatile uint32_t tx_overflow_count = 0u;

    uint32_t crc_error_count = 0u;

    /* Parser storage for one frame being received. */
    uint8_t frame_buffer[UART_MAX_FRAME_SIZE] = {};
    uint16_t frame_count = 0u;
    uint16_t expected_frame_size = 0u;


    /* Clears the partially collected UART frame. */
    void ResetFrameParser(void)
    {
        frame_count = 0u;
        expected_frame_size = 0u;
    }


    /* Resets the parser and treats byte as a possible first start byte. */
    void BeginFrameWithByte(uint8_t byte)
    {
        ResetFrameParser();

        if (byte == static_cast<uint8_t>(UART_FRAME_START & 0xFFu))
        {
            frame_buffer[0] = byte;
            frame_count = 1u;
        }
    }


    /* Restores the exact interrupt state that existed before a critical section. */
    static void ExitCritical(uint32_t previous_primask)
    {
        __set_PRIMASK(previous_primask);
    }


    /* Copies one whole frame to the TX ring and starts TXE interrupts. */
    uint8_t QueueBytes(const uint8_t* bytes_arr, uint16_t size)
    {
        if ((bytes_arr == nullptr) || (size == 0u) || (size >= TxQueueSize))
        {
            return 0u;
        }

        // save current interrupts state , and disable them
        const uint32_t previous_primask = __get_PRIMASK();
        __disable_irq();

        // for the tx head tail calculations
        uint16_t used = 0u;
        if (tx_head >= tx_tail)
        {
            used = static_cast<uint16_t>(tx_head - tx_tail);
        }
        else
        {
            used = static_cast<uint16_t>(TxQueueSize - (tx_tail - tx_head));
        }

        // calculate free space
        const uint16_t free_space = static_cast<uint16_t>(TxQueueSize - used - 1u);
        //if we cant copy full frame we wont do it
        if (size > free_space)
        {
            ++tx_overflow_count;
            ExitCritical(previous_primask);
            return 0u;
        }

        // full frame is coppied to the TX byte by byte
        for (uint16_t index = 0u; index < size; ++index)
        {
            tx_queue[tx_head] = bytes_arr[index];
            tx_head = static_cast<uint16_t>((tx_head + 1u) % TxQueueSize);
        }

        // enable interrupts again
        SET_BIT(USART2->CR1, USART_CR1_TXEIE);
        ExitCritical(previous_primask);
        return 1u;
    }


    /*
     * Find start bytes, collect header ,learn expected payload size ,collect the rest
     * ,read received CRC ,calculate CRC,compare them ,
     *  return the message type and sequence if valid
     */
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
