/* Shared Payload/EPS I2C VTable register protocol and HAL callbacks. */
#include "i2c_slave.hpp"

#include "crc16.h"
#include "protocol.h"
#include "../vtable/vtable.h"

#include <cstring>


namespace
{
enum class ReceiveState : uint8_t
{
    Register,
    Key,
    Index
};

/* The storage is static: this module performs no heap allocation. */
// context of i2c
I2C_HandleTypeDef* slave_i2c = nullptr;
uint8_t node_id = 0u;

uint8_t selected_register = REG_WHOAMI;
//received command
uint8_t received_register = REG_WHOAMI;

char selected_key[VT_NAME_LEN] = {};
bool selected_key_valid = false;

uint8_t received_index[sizeof(uint16_t)] = {};
uint16_t selected_index = 0u;
bool selected_index_valid = false;
ReceiveState receive_state = ReceiveState::Register;

// Static transmit buffers must stay alive until interrupt transmission ends.
uint8_t transmit_node_id = 0u;
uint8_t transmit_error = 0xFFu;
uint16_t transmit_count = 0u;
VtValueWire_t transmit_value = {};
VtEntryWire_t transmit_entry = {};


/* prepare transmit buffer , check if entity exists in the vtable by name  */
void PrepareValueResponse(void)
{
    //cleaning the buffer updating its type
    std::memset(&transmit_value, 0, sizeof(transmit_value));
    transmit_value.type = VT_TYPE_BYTES;

    VtEntry_t entry = {};

    //check that we got a key and we can transmit
    if (selected_key_valid && VTable_Get(selected_key, &entry))
    {
        transmit_value.type = entry.type;
        transmit_value.len = entry.len;
        std::memcpy(transmit_value.value, entry.value, VT_VALUE_LEN);
    }

    // len == 0 with a valid CRC means the selected key was not found.
    transmit_value.crc16 = Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&transmit_value), VT_VALUE_CRC_SIZE);
}


/* prepare transmit buffer , check if entity exists in the vtable by index */
void PrepareEntryResponse(void)
{
    //clean buffer
    std::memset(&transmit_entry, 0, sizeof(transmit_entry));

    VtEntry_t entry = {};
    if (selected_index_valid && VTable_At(selected_index, &entry))
    {
        std::memcpy(transmit_entry.name, entry.name, VT_NAME_LEN);
        transmit_entry.type = entry.type;
        transmit_entry.len = entry.len;
        std::memcpy(transmit_entry.value, entry.value, VT_VALUE_LEN);
    }

    transmit_entry.crc16 = Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&transmit_entry), VT_ENTRY_CRC_SIZE);
}


/*
 * Starts an interrupt based I2C transmission from the slave to the master.
 * hi2c : I2C slave context
 * bytes: buffer containing the data to send
 * size : number of bytes to send
 */
void StartTransmit(I2C_HandleTypeDef* hi2c, uint8_t* bytes, uint16_t size)
{
    (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, bytes, size, I2C_LAST_FRAME);
}
}


HAL_StatusTypeDef CommonI2CSlave_Init(I2C_HandleTypeDef* i2c_handle,
                                      uint16_t own_address_hal,
                                      uint8_t logical_node_id)
{
    if (i2c_handle == nullptr)
    {
        return HAL_ERROR;
    }

    slave_i2c = i2c_handle;
    node_id = logical_node_id;

    if (HAL_I2C_DeInit(slave_i2c) != HAL_OK)
    {
        return HAL_ERROR;
    }

    //receive slave address from the Payload/EPS wrapper
    slave_i2c->Init.OwnAddress1 = own_address_hal;
    // now resolved from bus_config.h, not protocol.h

    if (HAL_I2C_Init(slave_i2c) != HAL_OK)
    {
        return HAL_ERROR;
    }

    selected_register = REG_WHOAMI;
    received_register = REG_WHOAMI;
    receive_state = ReceiveState::Register;

    std::memset(selected_key, 0, sizeof(selected_key));
    selected_key_valid = false;

    std::memset(received_index, 0, sizeof(received_index));
    selected_index = 0u;
    selected_index_valid = false;

    return HAL_I2C_EnableListen_IT(slave_i2c);
}


/* first function that gets called from interrupt
 * Called automatically when an I2C master contacts this slave address.
 * Checks whether the master wants to write data to the slave or read data from it.
 * hi2c : context of the slave you want to write
 * address_match_code to who this msg
 */
extern "C" void HAL_I2C_AddrCallback(I2C_HandleTypeDef* hi2c,
                                     uint8_t transfer_direction,
                                     uint16_t address_match_code)
{
    (void)address_match_code;

    if (hi2c != slave_i2c)
    {
        return;
    }

    //check is master transmiting to us
    if (transfer_direction == I2C_DIRECTION_TRANSMIT)
    {
        //prepares the slave to receive one byte from the master
        //the first byte is the command
        receive_state = ReceiveState::Register;
        (void)HAL_I2C_Slave_Seq_Receive_IT(
            hi2c, &received_register, 1u, I2C_FIRST_FRAME);
        // here it returns and HAL calls the next function RX
        return;
    }

    //from the command we know what to do
    switch (selected_register)
    {
        case REG_WHOAMI:
            transmit_node_id = node_id;
            // send back slave ID
            StartTransmit(hi2c, &transmit_node_id, sizeof(transmit_node_id));
            break;

        case REG_VT_COUNT:
            transmit_count = VTable_Count();
            StartTransmit(hi2c,
                          reinterpret_cast<uint8_t*>(&transmit_count),
                          sizeof(transmit_count));
            break;

        case REG_VT_VALUE:
            PrepareValueResponse();
            StartTransmit(hi2c,
                          reinterpret_cast<uint8_t*>(&transmit_value),
                          VT_VALUE_WIRE_SIZE);
            break;

        case REG_VT_ENTRY:
            PrepareEntryResponse();
            StartTransmit(hi2c,
                          reinterpret_cast<uint8_t*>(&transmit_entry),
                          VT_ENTRY_WIRE_SIZE);
            break;

        default:
            // sends error
            StartTransmit(hi2c, &transmit_error, sizeof(transmit_error));
            break;
    }
}


/* second function that gets called
 * Called automatically after the slave finishes receiving bytes.
 * The first byte is the register command. REG_VT_SELECT then receives an
 * eight-byte key, and REG_VT_AT receives a two-byte little-endian index.
 */
extern "C" void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (hi2c != slave_i2c)
    {
        return;
    }

    //if there is a command already
    if (receive_state == ReceiveState::Register)
    {
        selected_register = received_register;

        //command select by name
        if (selected_register == REG_VT_SELECT)
        {
            selected_key_valid = false;
            std::memset(selected_key, 0, sizeof(selected_key));
            receive_state = ReceiveState::Key;

            //recive next bytes from i2c get exactly name bytes and put it in global verb
            (void)HAL_I2C_Slave_Seq_Receive_IT(
                hi2c,
                reinterpret_cast<uint8_t*>(selected_key),
                VT_NAME_LEN,
                I2C_LAST_FRAME);
        }
        //command select by index
        else if (selected_register == REG_VT_AT)
        {
            selected_index_valid = false;
            std::memset(received_index, 0, sizeof(received_index));
            receive_state = ReceiveState::Index;

            //receive bytes representing the index
            (void)HAL_I2C_Slave_Seq_Receive_IT(
                hi2c,
                received_index,
                sizeof(received_index),
                I2C_LAST_FRAME);
        }

        return;
    }

    if (receive_state == ReceiveState::Key)
    {
        selected_key_valid = true;
    }
    else if (receive_state == ReceiveState::Index)
    {
        selected_index = static_cast<uint16_t>(received_index[0])
            | static_cast<uint16_t>(
                static_cast<uint16_t>(received_index[1]) << 8u);
        selected_index_valid = true;
    }

    receive_state = ReceiveState::Register;
}


/*
 * Called automatically when an I2C listening transaction is complete.
 * Resets the receive state and enables listening again for the next request.
 */
extern "C" void HAL_I2C_ListenCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == slave_i2c)
    {
        receive_state = ReceiveState::Register;
        (void)HAL_I2C_EnableListen_IT(hi2c);
    }
}


/*
 * Called automatically when an I2C error occurs.
 * Resets the receive state and enables listening again
 * so the slave can continue receiving requests.
 */
extern "C" void HAL_I2C_ErrorCallback(I2C_HandleTypeDef* hi2c)
{
    if (hi2c == slave_i2c)
    {
        receive_state = ReceiveState::Register;
        (void)HAL_I2C_EnableListen_IT(hi2c);
    }
}
