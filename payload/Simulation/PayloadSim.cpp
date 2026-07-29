#include "PayloadSim.hpp"

#include "../../common/crc32.h"
#include "stm32l4xx_hal.h"

#include <cstring>

namespace
{
constexpr uint32_t SimulationPeriodMs = 500u;


PayloadData_t current_data = {};
uint32_t last_simulation_tick = 0u;
uint32_t simulation_rng_state = 0x6D2B79F5u ^ PAYLOAD_NODE_ID;
uint32_t seu_rng_state = 0xA5A5A5A5u ^ PAYLOAD_NODE_ID;
uint32_t data_read_count = 0u;

// generates a new pseudo-random 32-bit number.
uint32_t XorShift32(uint32_t& state)
{
    state ^= state << 13u;
    state ^= state >> 17u;
    state ^= state << 5u;
    return state;
}

int32_t Clamp(int32_t value, int32_t minimum, int32_t maximum)
{
    if (value < minimum)
    {
        return minimum;
    }

    if (value > maximum)
    {
        return maximum;
    }

    return value;
}

//calculates new CRC from the sample
void UpdateCrc(PayloadData_t& sample)
{
    sample.crc32 = Protocol_Crc32( reinterpret_cast<const uint8_t*>(&sample),
    								PAYLOAD_DATA_CRC_SIZE);
}

// copies the sample into the shared verb
void CommitSample(const PayloadData_t& sample)
{
    const uint32_t previous_primask = __get_PRIMASK();
    // disables interrupts
    __disable_irq();

    std::memcpy(&current_data, &sample, sizeof(current_data));

    if (previous_primask == 0u)
    {
        __enable_irq();
    }
}

//copies shared varb data into output
void CopyCurrentSample(PayloadData_t& output)
{
    const uint32_t previous_primask = __get_PRIMASK();
    __disable_irq();
    std::memcpy(&output, &current_data, sizeof(output));

    if (previous_primask == 0u)
    {
        __enable_irq();
    }
}
}

void PayloadSim_Init(void)
{
    PayloadData_t initial_data = {};

    initial_data.timestamp_ms = HAL_GetTick();
    initial_data.temperature_c_x10 = 245; /* 24.5 C */
    initial_data.humidity_pct_x10 = 420;  /* 42.0 % */
    initial_data.radiation_cps = 18u;
    initial_data.battery_pct = 92u;
    initial_data.node_id = PAYLOAD_NODE_ID;
    initial_data.flags = 0u;
    UpdateCrc(initial_data);

    last_simulation_tick = initial_data.timestamp_ms;
    data_read_count = 0u;
    simulation_rng_state = 0x6D2B79F5u ^ PAYLOAD_NODE_ID;
    seu_rng_state = 0xA5A5A5A5u ^ PAYLOAD_NODE_ID;

    CommitSample(initial_data);
}

//This function updates the simulated payload data every 500 ms.
// changes the readings and saves them in the shared verb
void PayloadSim_Tick(void)
{
    const uint32_t current_tick = HAL_GetTick();

    if ((current_tick - last_simulation_tick) < SimulationPeriodMs)
    {
        return;
    }

    last_simulation_tick = current_tick;


    PayloadData_t next_data = {};
    CopyCurrentSample(next_data);

    //changes readings abit
    const int32_t temperature_change =
        static_cast<int32_t>(XorShift32(simulation_rng_state) % 3u) - 1;
    const int32_t humidity_change =
        static_cast<int32_t>(XorShift32(simulation_rng_state) % 5u) - 2;
    int32_t radiation_change =
        static_cast<int32_t>(XorShift32(simulation_rng_state) % 5u) - 2;


     // Add an occasional small radiation burst to make the simulated space
     // environment easy to see in the UART log.

    if ((XorShift32(simulation_rng_state) & 0x3Fu) == 0u)
    {
        radiation_change += 25;
    }

    // puts changed data in next data
    next_data.timestamp_ms = current_tick;
    next_data.temperature_c_x10 = static_cast<int16_t>(Clamp(
        static_cast<int32_t>(next_data.temperature_c_x10)
            + temperature_change,-400,850));

    next_data.humidity_pct_x10 = static_cast<uint16_t>(Clamp(
        static_cast<int32_t>(next_data.humidity_pct_x10)
            + humidity_change,0,1000));

    next_data.radiation_cps = static_cast<uint16_t>(Clamp(
        static_cast<int32_t>(next_data.radiation_cps)+ radiation_change,
        0,
        2000));
    next_data.node_id = PAYLOAD_NODE_ID;
    next_data.flags = 0u;
    //update CRC and put it inside shared verb
    UpdateCrc(next_data);
    CommitSample(next_data);
}


 // Prepares a safe copy of the latest payload data for I2C transmission.
 // This prevents the simulator from changing current_data while I²C is sending it.
 // ISSUE Every 20th read, it intentionally corrupts one bit to test CRC detection.
void PayloadSim_PrepareTransmitData(PayloadData_t* output_data)
{
    if (output_data == nullptr)
    {
        return;
    }

    CopyCurrentSample(*output_data);
    ++data_read_count;

    if ((data_read_count % 20u) != 0u)
    {
        return;
    }

    /*
     * Mark this as an intentional SEU test, calculate the correct CRC, and
     * then corrupt one bit in the protected data. The OBC must reject it.
     */
    output_data->flags |= PAYLOAD_FLAG_SEU_INJECTED;
    UpdateCrc(*output_data);

    uint8_t* protected_bytes = reinterpret_cast<uint8_t*>(output_data);
    const uint32_t byte_index = XorShift32(seu_rng_state) % PAYLOAD_DATA_CRC_SIZE;
    const uint8_t bit_index = static_cast<uint8_t>(XorShift32(seu_rng_state) % 8u);

    protected_bytes[byte_index] ^= static_cast<uint8_t>(1u << bit_index);
}
