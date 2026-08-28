"""Resolves on-disk sensor_id values back to VTable key names."""

from __future__ import annotations

import struct

VT_NAME_LEN = 8

PAYLOAD_NODE_ID = 0x02
EPS_NODE_ID = 0x03

# Key name -> VtType, mirroring simulators/models.py.
VT_TYPE_U32 = 0
VT_TYPE_I32 = 1
VT_TYPE_F32 = 2

PAYLOAD_KEYS = {
    "TEMP": VT_TYPE_F32,
    "TDOSE": VT_TYPE_F32,
    "SEL": VT_TYPE_U32,
    "NRESET": VT_TYPE_U32,
}

EPS_KEYS = {
    "VBAT": VT_TYPE_F32,
    "TEMP": VT_TYPE_F32,
}
for _panel in range(6):
    EPS_KEYS[f"SP{_panel}_T"] = VT_TYPE_F32
    EPS_KEYS[f"SP{_panel}_I"] = VT_TYPE_F32


def vtable_hash_name(name: str) -> int:
    """Mirror of VTable_HashName in common/vtable/vtable.c."""
    padded = name.encode("ascii")[:VT_NAME_LEN].ljust(VT_NAME_LEN, b"\x00")

    hash_value = 2166136261
    for byte in padded:
        hash_value ^= byte
        hash_value = (hash_value * 16777619) & 0xFFFFFFFF

    return (hash_value ^ (hash_value >> 16)) & 0xFFFF


def _build_reverse_map() -> dict[int, tuple[str, str, int]]:
    """sensor_id -> (node name, key name, VtType)."""
    table: dict[int, tuple[str, str, int]] = {}

    for node_id, node_name, keys in (
        (PAYLOAD_NODE_ID, "PAYLOAD", PAYLOAD_KEYS),
        (EPS_NODE_ID, "EPS", EPS_KEYS),
    ):
        for key, vt_type in keys.items():
            sensor_id = (node_id << 8) | (vtable_hash_name(key) & 0x00FF)
            table[sensor_id] = (node_name, key, vt_type)

    return table


SENSOR_MAP = _build_reverse_map()

# Reserved IDs written by the OBC itself, not by a node key.
SENSOR_MAP[0x03FF] = ("OBC", "BOOT", VT_TYPE_U32)


def describe_sensor(sensor_id: int) -> tuple[str, str, int]:
    """Return (node, key, vt_type); falls back to a hex label when unknown."""
    known = SENSOR_MAP.get(sensor_id)
    if known is not None:
        return known

    node = (sensor_id >> 8) & 0xFF
    return (f"NODE{node:02X}", f"ID{sensor_id:04X}", VT_TYPE_U32)


def decode_value(raw: bytes, vt_type: int) -> float | int:
    """Decode a 4-byte record value using the key's declared type."""
    if vt_type == VT_TYPE_F32:
        return struct.unpack("<f", raw[:4])[0]
    if vt_type == VT_TYPE_I32:
        return struct.unpack("<i", raw[:4])[0]
    return struct.unpack("<I", raw[:4])[0]


def find_hash_collisions() -> list[tuple[int, list[str]]]:
    """The low byte of the hash is only 8 bits; report any collision."""
    seen: dict[int, list[str]] = {}

    for node_id, keys in ((PAYLOAD_NODE_ID, PAYLOAD_KEYS), (EPS_NODE_ID, EPS_KEYS)):
        for key in keys:
            sensor_id = (node_id << 8) | (vtable_hash_name(key) & 0x00FF)
            seen.setdefault(sensor_id, []).append(f"{node_id:02X}:{key}")

    return [(sid, names) for sid, names in seen.items() if len(names) > 1]
