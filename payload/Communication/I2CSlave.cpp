/* Implements the payload-side I2C register protocol and HAL callbacks. */
#include "I2CSlave.hpp"

#include "../../common/protocol.h"
#include "../Simulation/PayloadSim.hpp"

namespace
{
enum class ReceiveState : uint8_t
{
    Register,
    BatteryValue
};

// context of i2c
I2C_HandleTypeDef* slave_i2c = nullptr;
uint8_t selected_register = REG_WHOAMI;
uint8_t received_byte = 0u;
//current id
uint8_t transmit_node_id = PAYLOAD_NODE_ID;
PayloadData_t transmit_data = {};
ReceiveState receive_state = ReceiveState::Register;
}

HAL_StatusTypeDef I2CSlave_Init(I2C_HandleTypeDef* i2c_handle)
{
    if (i2c_handle == nullptr)
    {
        return HAL_ERROR;
    }

    slave_i2c = i2c_handle;

    //issue need to check payload address later
    if (HAL_I2C_DeInit(slave_i2c) != HAL_OK)
    {
        return HAL_ERROR;
    }

    //recive slave address
    slave_i2c->Init.OwnAddress1 = PAYLOAD_I2C_ADDRESS_HAL;

    if (HAL_I2C_Init(slave_i2c) != HAL_OK)
    {
        return HAL_ERROR;
    }

    selected_register = REG_WHOAMI;
    receive_state = ReceiveState::Register;

    return HAL_I2C_EnableListen_IT(slave_i2c);
}

/*
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

    if (transfer_direction == I2C_DIRECTION_TRANSMIT)
    {
        //prepares the slave to receive one byte from the master
        receive_state = ReceiveState::Register;
        (void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &received_byte, 1u, I2C_FIRST_FRAME);
        return;
    }

    if (selected_register == REG_WHOAMI)
    {
        transmit_node_id = PAYLOAD_NODE_ID;

        // send back slave ID
        (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &transmit_node_id,
                                            sizeof(transmit_node_id), I2C_LAST_FRAME);
    }
    else if (selected_register == REG_DATA)
    {
        // send back data
        PayloadSim_PrepareTransmitData(&transmit_data);
        (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, reinterpret_cast<uint8_t*>(&transmit_data),
                                           PAYLOAD_DATA_WIRE_SIZE, I2C_LAST_FRAME);
    }
    else
    {
        // sends error
        transmit_node_id = 0xFFu;
        (void)HAL_I2C_Slave_Seq_Transmit_IT(hi2c, &transmit_node_id,
                                            sizeof(transmit_node_id), I2C_LAST_FRAME);
    }
}


/*
 * Called automatically after the slave finishes receiving a byte.
 * Interprets the received byte as a register command and prepares
 * to receive another byte when the command requires extra data.
 */
extern "C" void HAL_I2C_SlaveRxCpltCallback(I2C_HandleTypeDef* hi2c)
{
    if (hi2c != slave_i2c)
    {
        return;
    }

    if (receive_state == ReceiveState::Register)
    {
        selected_register = received_byte;

        if (selected_register == REG_SETBATT)
        {
            receive_state = ReceiveState::BatteryValue;
            (void)HAL_I2C_Slave_Seq_Receive_IT(hi2c, &received_byte, 1u, I2C_LAST_FRAME);
        }

        return;
    }

    // REG_SETBATT is reserved for the future EPS node. Payload accepts and
    // consumes the byte so the shared register protocol remains testable,
    // but it deliberately does not change Payload battery data.

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
