/* Shared interrupt-based I2C VTable slave used by Payload and EPS. */
#pragma once

#include "stm32l4xx_hal.h"

#include <stdint.h>

/*
 * Configures one firmware image as an I2C VTable slave.
 *
 * Each firmware passes its own physical I2C address and logical node ID.
 * All receive, register-selection, CRC, and transmit behavior is shared.
 */
HAL_StatusTypeDef CommonI2CSlave_Init(I2C_HandleTypeDef* i2c_handle,
                                      uint16_t own_address_hal,
                                      uint8_t logical_node_id);
