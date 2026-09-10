// Host test for the HAL-independent VTable implementation.
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstring>

/* MSVC does not implement GCC's packed attribute; these layouts are naturally
 * identical for this test, and the ARM build still checks the packed sizes. */
#if defined(_MSC_VER)
#define __attribute__(ignored)
#endif

#include "../../common/vtable/vtable.c"


static void test_create_update_and_get(void)
{
    vtable_init();
    assert(vtable_count() == 0u);

    const float first = 24.5f;
    assert(vtable_set("TEMP", VT_TYPE_F32, &first, sizeof(first), 10u));
    assert(vtable_count() == 1u);

    VtEntry_t entry = {};
    assert(vtable_get("TEMP", &entry));
    assert(entry.type == VT_TYPE_F32);
    assert(entry.len == sizeof(first));
    assert(entry.updated_ms == 10u);
    assert((entry.flags & VT_FLAG_FRESH) != 0u);

    float decoded = 0.0f;
    std::memcpy(&decoded, entry.value, sizeof(decoded));
    assert(decoded == first);

    assert(vtable_get("TEMP", &entry));
    assert((entry.flags & VT_FLAG_FRESH) == 0u);

    const float updated = 31.25f;
    assert(vtable_set("TEMP", VT_TYPE_F32, &updated, sizeof(updated), 99u));
    assert(vtable_count() == 1u);
    assert(vtable_get("TEMP", &entry));
    assert(entry.updated_ms == 99u);
    std::memcpy(&decoded, entry.value, sizeof(decoded));
    assert(decoded == updated);
}


static void test_names_types_and_lengths(void)
{
    vtable_init();
    const uint32_t counter = 7u;
    const uint8_t bytes[3] = {1u, 2u, 3u};

    assert(vtable_set("ABCDEFGH", VT_TYPE_U32, &counter, sizeof(counter), 1u));
    assert(vtable_set("RAW", VT_TYPE_BYTES, bytes, sizeof(bytes), 2u));
    assert(!vtable_set("", VT_TYPE_U32, &counter, sizeof(counter), 3u));
    assert(!vtable_set("BADLEN", VT_TYPE_U32, &counter, 2u, 3u));
    assert(!vtable_set("BADTYPE", static_cast<VtType_t>(99u),
                       &counter, sizeof(counter), 3u));

    VtEntry_t entry = {};
    assert(vtable_get("ABCDEFGH", &entry));
    assert(std::memcmp(entry.name, "ABCDEFGH", VT_NAME_LEN) == 0);
    assert(vtable_get("RAW", &entry));
    assert(entry.len == 3u);
    assert(std::memcmp(entry.value, bytes, sizeof(bytes)) == 0);
}


static void test_capacity_and_enumeration(void)
{
    vtable_init();
    for (uint32_t index = 0u; index < VT_MAX_ENTRIES; ++index)
    {
        char name[VT_NAME_LEN + 1u] = {};
        std::snprintf(name, sizeof(name), "K%07u", index);
        assert(vtable_set(name, VT_TYPE_U32, &index, sizeof(index), index));
    }
    assert(vtable_count() == VT_MAX_ENTRIES);

    const uint32_t extra = 100u;
    assert(!vtable_set("EXTRA", VT_TYPE_U32, &extra, sizeof(extra), extra));

    bool saw_first = false;
    bool saw_last = false;
    for (uint16_t index = 0u; index < vtable_count(); ++index)
    {
        VtEntry_t entry = {};
        assert(vtable_at(index, &entry));
        saw_first = saw_first || (std::memcmp(entry.name, "K0000000", VT_NAME_LEN) == 0);
        saw_last = saw_last || (std::memcmp(entry.name, "K0000047", VT_NAME_LEN) == 0);
    }
    assert(saw_first && saw_last);
    assert(!vtable_at(vtable_count(), nullptr));
}


int main(void)
{
    test_create_update_and_get();
    test_names_types_and_lengths();
    test_capacity_and_enumeration();
    assert(vtable_hash_name("TEMP") == vtable_hash_name("TEMP"));
    assert(vtable_hash_name("TEMP") != vtable_hash_name("TDOSE"));
    std::puts("VTable host tests passed");
    return 0;
}
