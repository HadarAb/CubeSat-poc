// Implements EPS SIM_SET/GET/LIST over the shared framed UART transport.
#include "EpsUart.hpp"

#include "../../common/uart/uart_protocol.h"
#include "../../common/uart/uart_transport.hpp"
#include "../../common/vtable/vtable.h"
#include "stm32l4xx_hal.h"

#include <cstring>


namespace
{
// instant of common uart
UartTransport uart_transport;

//just checks if the name is valid
bool name_is_valid(const char name[VT_NAME_LEN])
{
    if (name[0] == '\0')
    {
        return false;
    }

    bool reached_end = false;
    for (uint8_t index = 0u; index < VT_NAME_LEN; ++index)
    {
        if (reached_end && (name[index] != '\0'))
        {
            return false;
        }
        if (name[index] == '\0')
        {
            reached_end = true;
        }
    }
    return true;
}


bool type_and_length_are_valid(uint8_t type, uint8_t len)
{
    if ((type > static_cast<uint8_t>(VT_TYPE_BYTES))
        || (len == 0u) || (len > VT_VALUE_LEN))
    {
        return false;
    }

    return (type == static_cast<uint8_t>(VT_TYPE_BYTES))
        || (len == sizeof(uint32_t));
}

// turn interrupts off
uint32_t enter_critical(void)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();
    return previous_primask;
}

// turn interrupts on
void exit_critical(uint32_t previous_primask)
{
    if (previous_primask == 0u)
    {
        __enable_irq();
    }
}


bool names_match(const char left[VT_NAME_LEN], const char right[VT_NAME_LEN])
{
    return std::memcmp(left, right, VT_NAME_LEN) == 0;
}

// find the index of the name (if it exists)
uint16_t find_dense_index(const char name[VT_NAME_LEN])
{
    const uint16_t count = vtable_count();
    VtEntry_t candidate = {};

    for (uint16_t index = 0u; index < count; ++index)
    {
        if (vtable_at(index, &candidate) && names_match(candidate.name, name))
        {
            return index;
        }
    }
    return 0u;
}

// support function for full ack function
void fill_ack_value(UartSimAckPayload_t& ack, const VtEntry_t& entry)
{
    std::memcpy(ack.name, entry.name, VT_NAME_LEN);
    ack.type = entry.type;
    ack.len = entry.len;
    std::memcpy(ack.value, entry.value, VT_VALUE_LEN);
}

/*
 * full ack function sends back the name and the value of the sensor that was
 * added to the vtable
 */
void send_ack(uint16_t sequence, uint8_t request_type, uint8_t status,
             uint16_t index, uint16_t count, const VtEntry_t* entry,
             const char* requested_name = nullptr)
{
    UartSimAckPayload_t ack = {};
    ack.status = status;
    ack.request_type = request_type;
    ack.index = index;
    ack.count = count;

    if (entry != nullptr)
    {
        fill_ack_value(ack, *entry);
    }
    else if (requested_name != nullptr)
    {
        std::memcpy(ack.name, requested_name, VT_NAME_LEN);
    }

    uart_transport.send_frame(UART_MSG_SIM_ACK, sequence, &ack, sizeof(ack));
}

/*
 * sequence msg id , payload msg it self
 * you get msg from pc to set a sensor value . you set it and send back an ack msg
 * containing what was set
 */
void handle_set(uint16_t sequence, const uint8_t* payload, uint16_t payload_length)
{
	// wrong struct send error
    if (payload_length != sizeof(UartSimSetPayload_t))
    {
        send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr);
        return;
    }


    UartSimSetPayload_t request = {};
    std::memcpy(&request, payload, sizeof(request));

    //check name
    if (!name_is_valid(request.name))
    {
        send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr);
        return;
    }
    //check type
    if (request.type > static_cast<uint8_t>(VT_TYPE_BYTES))
    {
        send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_TYPE,
                0u, vtable_count(), nullptr, request.name);
        return;
    }

    //check len
    if (!type_and_length_are_valid(request.type, request.len))
    {
        send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr, request.name);
        return;
    }
    //starting to store at the table so we disable interrupts
    const uint32_t previous_primask = enter_critical();
    //set the value at the table
    const bool stored = vtable_set(request.name,static_cast<VtType_t>(request.type),
                                   request.value, request.len, HAL_GetTick());

    exit_critical(previous_primask);

    //if we failed to store ,send an error
    if (!stored)
    {
        send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_TABLE_FULL,
                0u, vtable_count(), nullptr, request.name);
        return;
    }

    //send back the sensor that was set in ack msg
    VtEntry_t entry = {};
    const uint16_t index = find_dense_index(request.name);
    (void)vtable_at(index, &entry);
    send_ack(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_OK,
            index, vtable_count(), &entry);
}

// get a value from the vtable
void handle_get(uint16_t sequence, const uint8_t* payload, uint16_t payload_length)
{
    if (payload_length != sizeof(UartSimGetPayload_t))
    {
        send_ack(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr);
        return;
    }

    UartSimGetPayload_t request = {};
    std::memcpy(&request, payload, sizeof(request));
    if (!name_is_valid(request.name))
    {
        send_ack(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr);
        return;
    }

    VtEntry_t entry = {};
    const uint32_t previous_primask = enter_critical();
    const bool found = vtable_get(request.name, &entry);
    exit_critical(previous_primask);

    if (!found)
    {
        send_ack(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_UNKNOWN_KEY,
                0u, vtable_count(), nullptr, request.name);
        return;
    }

    send_ack(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_OK,
            find_dense_index(request.name), vtable_count(), &entry);
}

// send all items that are in the vtable as ack msges
void handle_list(uint16_t sequence, uint16_t payload_length)
{
    if (payload_length != 0u)
    {
        send_ack(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_BAD_REQUEST,
                0u, vtable_count(), nullptr);
        return;
    }

    const uint16_t count = vtable_count();
    if (count == 0u)
    {
        send_ack(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_OK,
                0u, 0u, nullptr);
        return;
    }

    for (uint16_t index = 0u; index < count; ++index)
    {
        VtEntry_t entry = {};
        if (vtable_at(index, &entry))
        {
            send_ack(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_OK,
                    index, count, &entry);
        }
    }
}



void handle_frame(const UartReceivedFrame_t& frame)
{
    switch (frame.msg_type)
    {
        case UART_MSG_SIM_SET:
            handle_set(frame.sequence, frame.payload, frame.payload_length);
            break;
        case UART_MSG_SIM_GET:
            handle_get(frame.sequence, frame.payload, frame.payload_length);
            break;
        case UART_MSG_SIM_LIST:
            handle_list(frame.sequence, frame.payload_length);
            break;
        default:
            send_ack(frame.sequence, frame.msg_type, UART_SIM_STATUS_BAD_REQUEST,
                    0u, vtable_count(), nullptr);
            break;
    }
}
}


void eps_uart_init(void)
{
    uart_transport.init();
}

//receive frame and send it to handle function
void eps_uart_process(void)
{
    UartReceivedFrame_t frame = {};
    while (uart_transport.try_receive(&frame) != 0u)
    {
        handle_frame(frame);
    }
}


extern "C" void eps_uart_handle_interrupt(void)
{
    uart_transport.handle_interrupt();
}
