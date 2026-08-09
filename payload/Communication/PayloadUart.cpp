/* Implements Payload SIM_SET/GET/LIST over the shared framed UART transport. */
#include "PayloadUart.hpp"

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
bool NameIsValid(const char name[VT_NAME_LEN])
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


bool TypeAndLengthAreValid(uint8_t type, uint8_t len)
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
uint32_t EnterCritical(void)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();
    return previous_primask;
}

// turn interrupts on
void ExitCritical(uint32_t previous_primask)
{
    if (previous_primask == 0u)
    {
        __enable_irq();
    }
}


bool NamesMatch(const char left[VT_NAME_LEN], const char right[VT_NAME_LEN])
{
    return std::memcmp(left, right, VT_NAME_LEN) == 0;
}

// find the index of the name (if it exists)
uint16_t FindDenseIndex(const char name[VT_NAME_LEN])
{
    const uint16_t count = VTable_Count();
    VtEntry_t candidate = {};

    for (uint16_t index = 0u; index < count; ++index)
    {
        if (VTable_At(index, &candidate) && NamesMatch(candidate.name, name))
        {
            return index;
        }
    }
    return 0u;
}

// support function for full ack function
void FillAckValue(UartSimAckPayload_t& ack, const VtEntry_t& entry)
{
    std::memcpy(ack.name, entry.name, VT_NAME_LEN);
    ack.type = entry.type;
    ack.len = entry.len;
    std::memcpy(ack.value, entry.value, VT_VALUE_LEN);
}

// full ack function sends back the name and the value of the sensor that was
// added to the vtable
void SendAck(uint16_t sequence, uint8_t request_type, uint8_t status,
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
        FillAckValue(ack, *entry);
    }
    else if (requested_name != nullptr)
    {
        std::memcpy(ack.name, requested_name, VT_NAME_LEN);
    }

    uart_transport.SendFrame(UART_MSG_SIM_ACK, sequence, &ack, sizeof(ack));
}

// sequence msg id , payload msg it self
// you get msg from pc to set a sensor value . you set it and send back an ack msg
// containing what was set
void HandleSet(uint16_t sequence, const uint8_t* payload, uint16_t payload_length)
{
	// wrong struct send error
    if (payload_length != sizeof(UartSimSetPayload_t))
    {
        SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr);
        return;
    }


    UartSimSetPayload_t request = {};
    std::memcpy(&request, payload, sizeof(request));

    //check name
    if (!NameIsValid(request.name))
    {
        SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr);
        return;
    }
    //check type
    if (request.type > static_cast<uint8_t>(VT_TYPE_BYTES))
    {
        SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_TYPE,
                0u, VTable_Count(), nullptr, request.name);
        return;
    }

    //check len
    if (!TypeAndLengthAreValid(request.type, request.len))
    {
        SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr, request.name);
        return;
    }
    //starting to store at the table so we disable interrupts
    const uint32_t previous_primask = EnterCritical();
    //set the value at the table
    const bool stored = VTable_Set(request.name,static_cast<VtType_t>(request.type),
                                   request.value, request.len, HAL_GetTick());

    ExitCritical(previous_primask);

    //if we failed to store ,send an error
    if (!stored)
    {
        SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_TABLE_FULL,
                0u, VTable_Count(), nullptr, request.name);
        return;
    }

    //send back the sensor that was set in ack msg
    VtEntry_t entry = {};
    const uint16_t index = FindDenseIndex(request.name);
    (void)VTable_At(index, &entry);
    SendAck(sequence, UART_MSG_SIM_SET, UART_SIM_STATUS_OK,
            index, VTable_Count(), &entry);
}

// get a value from the vtable
void HandleGet(uint16_t sequence, const uint8_t* payload, uint16_t payload_length)
{
    if (payload_length != sizeof(UartSimGetPayload_t))
    {
        SendAck(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr);
        return;
    }

    UartSimGetPayload_t request = {};
    std::memcpy(&request, payload, sizeof(request));
    if (!NameIsValid(request.name))
    {
        SendAck(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr);
        return;
    }

    VtEntry_t entry = {};
    const uint32_t previous_primask = EnterCritical();
    const bool found = VTable_Get(request.name, &entry);
    ExitCritical(previous_primask);

    if (!found)
    {
        SendAck(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_UNKNOWN_KEY,
                0u, VTable_Count(), nullptr, request.name);
        return;
    }

    SendAck(sequence, UART_MSG_SIM_GET, UART_SIM_STATUS_OK,
            FindDenseIndex(request.name), VTable_Count(), &entry);
}

// send all items that are in the vtable as ack msges
void HandleList(uint16_t sequence, uint16_t payload_length)
{
    if (payload_length != 0u)
    {
        SendAck(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_BAD_REQUEST,
                0u, VTable_Count(), nullptr);
        return;
    }

    const uint16_t count = VTable_Count();
    if (count == 0u)
    {
        SendAck(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_OK,
                0u, 0u, nullptr);
        return;
    }

    for (uint16_t index = 0u; index < count; ++index)
    {
        VtEntry_t entry = {};
        if (VTable_At(index, &entry))
        {
            SendAck(sequence, UART_MSG_SIM_LIST, UART_SIM_STATUS_OK,
                    index, count, &entry);
        }
    }
}



void HandleFrame(const UartReceivedFrame_t& frame)
{
    switch (frame.msg_type)
    {
        case UART_MSG_SIM_SET:
            HandleSet(frame.sequence, frame.payload, frame.payload_length);
            break;
        case UART_MSG_SIM_GET:
            HandleGet(frame.sequence, frame.payload, frame.payload_length);
            break;
        case UART_MSG_SIM_LIST:
            HandleList(frame.sequence, frame.payload_length);
            break;
        default:
            SendAck(frame.sequence, frame.msg_type, UART_SIM_STATUS_BAD_REQUEST,
                    0u, VTable_Count(), nullptr);
            break;
    }
}
}


void PayloadUart_Init(void)
{
    uart_transport.Init();
}

//recive frame and send it to handle function
void PayloadUart_Process(void)
{
    UartReceivedFrame_t frame = {};
    while (uart_transport.TryReceive(&frame) != 0u)
    {
        HandleFrame(frame);
    }
}


extern "C" void PayloadUart_HandleInterrupt(void)
{
    uart_transport.HandleInterrupt();
}
