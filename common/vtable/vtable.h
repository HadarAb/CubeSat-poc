#ifndef CUBESAT_COMMON_VTABLE_H
#define CUBESAT_COMMON_VTABLE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Key/value telemetry table.
 *
 * Every simulated sensor value lives here under a short ASCII key. The PC
 * simulation creates and updates entries over UART at runtime, so adding a new
 * field needs no firmware change. The OBC reads entries back over I2C by key,
 * or enumerates them by index to discover what a node offers.
 *
 * Storage is a fixed array, so there is no allocation in flight code. Entries
 * are created and updated but never deleted: a key added by the simulation
 * lives until the node resets. That removes the tombstone handling that open
 * addressing would otherwise need.
 */
#define VT_MAX_ENTRIES  48u
#define VT_NAME_LEN     8u
#define VT_VALUE_LEN    8u

// types unsign32 , int32, float32 ....
typedef enum
{
    VT_TYPE_U32   = 0u,
    VT_TYPE_I32   = 1u,
    VT_TYPE_F32   = 2u,
    VT_TYPE_BYTES = 3u
} VtType_t;

// each entity in vtable have flag field
#define VT_FLAG_IN_USE  0x0001u  // if on means this entity is in use
#define VT_FLAG_FRESH   0x0002u  // if on then data is fresh and OBC still didnt got it

// this format is only how it is saved inside the ram
// wire format is how u send it threw i2c is below
typedef struct __attribute__((packed))
{
    char name[VT_NAME_LEN];      /* NUL-padded, not NUL-terminated */
    uint8_t type;
    uint8_t len;                 /* 1..VT_VALUE_LEN valid bytes in value */
    uint16_t flags;
    uint8_t value[VT_VALUE_LEN];
    uint32_t updated_ms;         // last time this field was updated
} VtEntry_t;


//i2c frame when you know the entity name and just need the value
// len == 0 means the selected key does not exist
typedef struct __attribute__((packed))
{
    uint8_t type;
    uint8_t len;
    uint8_t value[VT_VALUE_LEN];
    uint16_t crc16;
} VtValueWire_t;

//i2c frame when OBC needs to discover new entity
typedef struct __attribute__((packed))
{
    char name[VT_NAME_LEN];
    uint8_t type;
    uint8_t len;
    uint8_t value[VT_VALUE_LEN];
    uint16_t crc16;
} VtEntryWire_t;

#define VT_VALUE_WIRE_SIZE ((uint16_t)sizeof(VtValueWire_t))
#define VT_ENTRY_WIRE_SIZE ((uint16_t)sizeof(VtEntryWire_t))
#define VT_VALUE_CRC_SIZE ((uint32_t)offsetof(VtValueWire_t, crc16))
#define VT_ENTRY_CRC_SIZE ((uint32_t)offsetof(VtEntryWire_t, crc16))

/* Clears the table. Call once before the scheduler starts. */
void VTable_Init(void);

/*
 * Creates or updates one entry. updated_ms is supplied by the caller so this
 * module stays independent of HAL_GetTick() and remains PC-testable pure C.
 * Returns false for invalid input or when the table is full.
 */
bool VTable_Set(const char* name, VtType_t type, const void* value, uint8_t len,
                uint32_t updated_ms);

/*
 * Copies one entry out by key and clears VT_FLAG_FRESH in the stored entry.
 * Returns false when the key is unknown.
 */
bool VTable_Get(const char* name, VtEntry_t* out);

/* Number of live entries, and the exclusive upper bound for VTable_At. */
uint16_t VTable_Count(void);

/* Copies the entry at a dense index in [0, VTable_Count()). Used for discovery. */
bool VTable_At(uint16_t index, VtEntry_t* out);

// input a name return an ID using FNV-1a, it is usefull to store the ID on the SD
uint16_t VTable_HashName(const char* name);

#ifdef __cplusplus
}

static_assert(sizeof(VtEntry_t) == 24u,
              "VtEntry_t must stay 24 bytes");
static_assert(sizeof(VtValueWire_t) == 12u,
              "VtValueWire_t wire layout must stay 12 bytes");
static_assert(sizeof(VtEntryWire_t) == 20u,
              "VtEntryWire_t wire layout must stay 20 bytes");
#else
_Static_assert(sizeof(VtEntry_t) == 24u,
               "VtEntry_t must stay 24 bytes");
_Static_assert(sizeof(VtValueWire_t) == 12u,
               "VtValueWire_t wire layout must stay 12 bytes");
_Static_assert(sizeof(VtEntryWire_t) == 20u,
               "VtEntryWire_t wire layout must stay 20 bytes");
#endif

#endif
