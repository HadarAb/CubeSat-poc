/* Host tests for the I2C CRC16 contract and VTable wire boundaries. */
#include <cassert>
#include <cstdint>
#include <cstring>

#if defined(_MSC_VER)
#define __attribute__(ignored)
#endif

#include "../../common/crc16.h"
#include "../../common/vtable.h"


int main(void)
{
    static const uint8_t check[] = "123456789";
    assert(Protocol_Crc16(check, 9u) == 0x29B1u);

    VtValueWire_t value = {};
    value.type = VT_TYPE_U32;
    value.len = sizeof(uint32_t);
    const uint32_t counter = 42u;
    std::memcpy(value.value, &counter, sizeof(counter));
    value.crc16 = Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&value), VT_VALUE_CRC_SIZE);

    assert(value.crc16 == Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&value), VT_VALUE_CRC_SIZE));
    assert(VT_VALUE_CRC_SIZE == 10u);
    assert(VT_ENTRY_CRC_SIZE == 18u);

    VtValueWire_t missing = {};
    missing.type = VT_TYPE_BYTES;
    missing.len = 0u;
    missing.crc16 = Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&missing), VT_VALUE_CRC_SIZE);
    assert(missing.len == 0u);
    assert(missing.crc16 == Protocol_Crc16(
        reinterpret_cast<const uint8_t*>(&missing), VT_VALUE_CRC_SIZE));

    return 0;
}
