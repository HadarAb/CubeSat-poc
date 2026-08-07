/* Fixed-capacity, allocation-free VTable implementation. */
#include "../../common/vtable.h"

#include <string.h>


static VtEntry_t entries[VT_MAX_ENTRIES];
static uint16_t entry_count;


/*
 * puts char by char into normalized and at the end puts \0 till the end
 * for example TEMP ---> [T][E][M][P][\0][\0][\0][\0]*/
static bool NormalizeName(const char* name, char normalized[VT_NAME_LEN])
{
    bool reached_end = false;

    if ((name == NULL) || (name[0] == '\0'))
    {
        return false;
    }

    for (uint8_t index = 0u; index < VT_NAME_LEN; ++index)
    {
        if (reached_end)
        {
            normalized[index] = '\0';
            continue;
        }

        const char character = name[index];

        if (character == '\0')
        {
            normalized[index] = '\0';
            reached_end = true;
        }
        else
        {
            normalized[index] = character;
        }
    }

    return true;
}

/*
 * from a name gives you an ID  */
static uint32_t Fnv1a32(const char name[VT_NAME_LEN])
{
    uint32_t hash = 2166136261u;

    for (uint8_t index = 0u; index < VT_NAME_LEN; ++index)
    {
        hash ^= (uint8_t)name[index];
        hash *= 16777619u;
    }

    return hash;
}


static bool TypeAndLengthAreValid(VtType_t type, uint8_t len)
{
    if ((type < VT_TYPE_U32) || (type > VT_TYPE_BYTES))
    {
        return false;
    }

    if ((len == 0u) || (len > VT_VALUE_LEN))
    {
        return false;
    }

    if ((type != VT_TYPE_BYTES) && (len != sizeof(uint32_t)))
    {
        return false;
    }

    return true;
}


/*checks if the name already exists in the vtable
 * if not return slot where you can put it
 * if ye return you found it and it what slot */
static int16_t FindSlot(const char normalized[VT_NAME_LEN], bool* found)
{
	//finds first slot by id
    const uint32_t start = Fnv1a32(normalized) % VT_MAX_ENTRIES;

    //starts sarching the vtable for a free slot
    for (uint16_t probe = 0u; probe < VT_MAX_ENTRIES; ++probe)
    {
    	// changes the slot
        const uint16_t slot = (uint16_t)((start + probe) % VT_MAX_ENTRIES);

        //if slot is empty return slot number
        if ((entries[slot].flags & VT_FLAG_IN_USE) == 0u)
        {
            *found = false;
            return (int16_t)slot;
        }
        // if you found slot with the same name return you found the name and return slot num
        if (memcmp(entries[slot].name, normalized, VT_NAME_LEN) == 0)
        {
            *found = true;
            return (int16_t)slot;
        }
    }

    *found = false;
    return -1;
}


void VTable_Init(void)
{
    memset(entries, 0, sizeof(entries));
    entry_count = 0u;
}


bool VTable_Set(const char* name, VtType_t type, const void* value, uint8_t len,
                uint32_t updated_ms)
{

    char normalized[VT_NAME_LEN];
    bool found = false;
    //here you change the name via reference
    if ((value == NULL) || !NormalizeName(name, normalized)
        || !TypeAndLengthAreValid(type, len))
    {
        return false;
    }
    // find where you can put it in the vtable
    const int16_t slot = FindSlot(normalized, &found);
    if (slot < 0)
    {
        return false;
    }

    //the pointer to the slot
    VtEntry_t* const entry = &entries[(uint16_t)slot];
    //if the slot is fresh new and still not saved
    // then you fill it
    if (!found)
    {
        memset(entry, 0, sizeof(*entry));
        memcpy(entry->name, normalized, VT_NAME_LEN);
        entry->flags = VT_FLAG_IN_USE;
        ++entry_count;
    }

    entry->type = (uint8_t)type;
    entry->len = len;
    //flag as fresh data
    entry->flags |= (VT_FLAG_IN_USE | VT_FLAG_FRESH);
    //set new value
    memset(entry->value, 0, sizeof(entry->value));
    memcpy(entry->value, value, len);
    entry->updated_ms = updated_ms;
    return true;
}


bool VTable_Get(const char* name, VtEntry_t* out)
{
    char normalized[VT_NAME_LEN];
    bool found = false;

    if ((out == NULL) || !NormalizeName(name, normalized))
    {
        return false;
    }

    const int16_t slot = FindSlot(normalized, &found);
    if ((slot < 0) || !found)
    {
        return false;
    }

    VtEntry_t* const entry = &entries[(uint16_t)slot];
    memcpy(out, entry, sizeof(*out));
    //clears the fresh flag . if some one got it this means some one sow it .
    entry->flags &= (uint16_t)(UINT16_MAX ^ VT_FLAG_FRESH);
    return true;
}


uint16_t VTable_Count(void)
{
    return entry_count;
}



bool VTable_At(uint16_t index, VtEntry_t* out)
{
    uint16_t dense_index = 0u;

    if ((out == NULL) || (index >= entry_count))
    {
        return false;
    }

    for (uint16_t slot = 0u; slot < VT_MAX_ENTRIES; ++slot)
    {
        if ((entries[slot].flags & VT_FLAG_IN_USE) == 0u)
        {
            continue;
        }

        if (dense_index == index)
        {
            memcpy(out, &entries[slot], sizeof(*out));
            return true;
        }

        ++dense_index;
    }

    return false;
}


/* from a name to ID */
uint16_t VTable_HashName(const char* name)
{
    char normalized[VT_NAME_LEN];

    if (!NormalizeName(name, normalized))
    {
        return 0u;
    }

    const uint32_t hash = Fnv1a32(normalized);
    return (uint16_t)(hash ^ (hash >> 16u));
}
