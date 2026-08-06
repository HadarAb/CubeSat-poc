#ifndef CUBESAT_COMMON_VTABLE_H
#define CUBESAT_COMMON_VTABLE_H

#include <stdbool.h>
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

typedef enum
{
    VT_TYPE_U32   = 0u,
    VT_TYPE_I32   = 1u,
    VT_TYPE_F32   = 2u,
    VT_TYPE_BYTES = 3u
} VtType_t;

#define VT_FLAG_IN_USE  0x0001u
#define VT_FLAG_FRESH   0x0002u  /* set on write, cleared once the OBC reads it */

/* In-RAM entry. This is not a wire format, see the two wire structs below. */
typedef struct __attribute__((packed))
{
    char name[VT_NAME_LEN];      /* NUL-padded, not NUL-terminated */
    uint8_t type;
    uint8_t len;                 /* 1..VT_VALUE_LEN valid bytes in value */
    uint16_t flags;
    uint8_t value[VT_VALUE_LEN];
    uint32_t updated_ms;         /* node-local tick of the last write */
} VtEntry_t;

/*
 * I2C response frames. The slave computes crc16 while it transmits, so the CRC
 * is never part of the stored entry and is never produced when data is packed.
 */
typedef struct __attribute__((packed))
{
    uint8_t type;
    uint8_t len;
    uint8_t value[VT_VALUE_LEN];
    uint16_t crc16;
} VtValueWire_t;

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

/* Clears the table. Call once before the scheduler starts. */
void VTable_Init(void);

/* Creates or updates one entry. Returns false when the table is full. */
bool VTable_Set(const char* name, VtType_t type, const void* value, uint8_t len);

/* Copies one entry out by key. Returns false when the key is unknown. */
bool VTable_Get(const char* name, VtEntry_t* out);

/* Number of live entries, and the exclusive upper bound for VTable_At. */
uint16_t VTable_Count(void);

/* Copies the entry at a dense index in [0, VTable_Count()). Used for discovery. */
bool VTable_At(uint16_t index, VtEntry_t* out);

/* FNV-1a over the padded key. This is the sensor_id stored in SD log slots. */
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
