/* Public interface for generated payload telemetry and CRC-fault testing. */
#pragma once

#include "../../common/protocol.h"
#include "../../common/legacy_payload_data.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Creates the initial simulated payload sample. */
void PayloadSim_Init(void);

/* Updates the simulated sample when the 500 ms period has elapsed. */
void PayloadSim_Tick(void);

/*
 * Copies one complete sample for an I2C response.
 * Every twentieth call intentionally corrupts one non-CRC bit after the CRC
 * has been calculated so the OBC can demonstrate CRC error detection.
 */
void PayloadSim_PrepareTransmitData(PayloadData_t* output_data);

#ifdef __cplusplus
}
#endif
