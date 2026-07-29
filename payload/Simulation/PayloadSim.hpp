#pragma once

#include "../../common/protocol.h"

#ifdef __cplusplus
extern "C" {
#endif

void PayloadSim_Init(void);
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
