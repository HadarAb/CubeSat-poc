/* Host test for the HAL-independent VTable implementation. */
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* MSVC does not implement GCC's packed attribute; these layouts are naturally
 * identical for this test, and the ARM build still checks the packed sizes. */
#if defined(_MSC_VER)
#define __attribute__(ignored)
#endif

#include "../../payload/Communication/VTable.c"


static void TestCreateUpdateAndGet(void)
{
    VTable_Init();
    assert(VTable_Count() == 0u);

    const float first = 24.5f;
    assert(VTable_Set("TEMP", VT_TYPE_F32, &first, sizeof(first), 10u));
    assert(VTable_Count() == 1u);

    VtEntry_t entry = {};
    assert(VTable_Get("TEMP", &entry));
    assert(entry.type == VT_TYPE_F32);
    assert(entry.len == sizeof(first));
    assert(entry.updated_ms == 10u);
    assert((entry.flags & VT_FLAG_FRESH) != 0u);

    float decoded = 0.0f;
    std::memcpy(&decoded, entry.value, sizeof(decoded));
    assert(decoded == first);

    assert(VTable_Get("TEMP", &entry));
    assert((entry.flags & VT_FLAG_FRESH) == 0u);

    const float updated = 31.25f;
    assert(VTable_Set("TEMP", VT_TYPE_F32, &updated, sizeof(updated), 99u));
    assert(VTable_Count() == 1u);
    assert(VTable_Get("TEMP", &entry));
    assert(entry.updated_ms == 99u);
    std::memcpy(&decoded, entry.value, sizeof(decoded));
    assert(decoded == updated);
}


static void TestNamesTypesAndLengths(void)
{
    VTable_Init();
    const uint32_t counter = 7u;
    const uint8_t bytes[3] = {1u, 2u, 3u};

    assert(VTable_Set("ABCDEFGH", VT_TYPE_U32, &counter, sizeof(counter), 1u));
    assert(VTable_Set("RAW", VT_TYPE_BYTES, bytes, sizeof(bytes), 2u));
    assert(!VTable_Set("", VT_TYPE_U32, &counter, sizeof(counter), 3u));
    assert(!VTable_Set("BADLEN", VT_TYPE_U32, &counter, 2u, 3u));
    assert(!VTable_Set("BADTYPE", static_cast<VtType_t>(99u),
                       &counter, sizeof(counter), 3u));

    VtEntry_t entry = {};
    assert(VTable_Get("ABCDEFGH", &entry));
    assert(std::memcmp(entry.name, "ABCDEFGH", VT_NAME_LEN) == 0);
    assert(VTable_Get("RAW", &entry));
    assert(entry.len == 3u);
    assert(std::memcmp(entry.value, bytes, sizeof(bytes)) == 0);
}


static void TestCapacityAndEnumeration(void)
{
    VTable_Init();
    for (uint32_t index = 0u; index < VT_MAX_ENTRIES; ++index)
    {
        char name[VT_NAME_LEN + 1u] = {};
        std::snprintf(name, sizeof(name), "K%07u", index);
        assert(VTable_Set(name, VT_TYPE_U32, &index, sizeof(index), index));
    }
    assert(VTable_Count() == VT_MAX_ENTRIES);

    const uint32_t extra = 100u;
    assert(!VTable_Set("EXTRA", VT_TYPE_U32, &extra, sizeof(extra), extra));

    bool saw_first = false;
    bool saw_last = false;
    for (uint16_t index = 0u; index < VTable_Count(); ++index)
    {
        VtEntry_t entry = {};
        assert(VTable_At(index, &entry));
        saw_first = saw_first || (std::memcmp(entry.name, "K0000000", VT_NAME_LEN) == 0);
        saw_last = saw_last || (std::memcmp(entry.name, "K0000047", VT_NAME_LEN) == 0);
    }
    assert(saw_first && saw_last);
    assert(!VTable_At(VTable_Count(), nullptr));
}


int main(void)
{
    TestCreateUpdateAndGet();
    TestNamesTypesAndLengths();
    TestCapacityAndEnumeration();
    assert(VTable_HashName("TEMP") == VTable_HashName("TEMP"));
    assert(VTable_HashName("TEMP") != VTable_HashName("TDOSE"));
    std::puts("VTable host tests passed");
    return 0;
}
