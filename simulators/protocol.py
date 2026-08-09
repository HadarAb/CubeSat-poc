"""UART payloads used by the PC simulators and STM32 simulated nodes."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
from typing import Any

from ground_station.protocol import Frame, encode_frame


VT_NAME_LEN = 8
VT_VALUE_LEN = 8

SIM_SET_PAYLOAD = struct.Struct("<8sBB8s")
SIM_GET_PAYLOAD = struct.Struct("<8s")
SIM_ACK_PAYLOAD = struct.Struct("<BBHH8sBB8s")


class SimMessageType(IntEnum):
    SET = 0x40
    GET = 0x41
    LIST = 0x42
    ACK = 0x43


class SimStatus(IntEnum):
    OK = 0x00
    BAD_REQUEST = 0x01
    UNKNOWN_KEY = 0x02
    TABLE_FULL = 0x03
    BAD_TYPE = 0x04


class VtType(IntEnum):
    U32 = 0
    I32 = 1
    F32 = 2
    BYTES = 3


@dataclass(frozen=True)
class SimValue:
    """One key/value update ready to be written into the node VTable."""

    name: str
    type: VtType
    value: int | float | bytes

    def encoded_value(self) -> bytes:
        if self.type == VtType.U32:
            if not isinstance(self.value, int) or not 0 <= self.value <= 0xFFFFFFFF:
                raise ValueError(f"{self.name}: U32 value must be in 0..2^32-1")
            return struct.pack("<I", self.value)
        if self.type == VtType.I32:
            if not isinstance(self.value, int) or not -(2**31) <= self.value < 2**31:
                raise ValueError(f"{self.name}: I32 value must fit in int32")
            return struct.pack("<i", self.value)
        if self.type == VtType.F32:
            if not isinstance(self.value, (int, float)):
                raise ValueError(f"{self.name}: F32 value must be numeric")
            return struct.pack("<f", float(self.value))
        if self.type == VtType.BYTES:
            encoded = bytes(self.value)
            if not 1 <= len(encoded) <= VT_VALUE_LEN:
                raise ValueError(f"{self.name}: BYTES value must contain 1..8 bytes")
            return encoded
        raise ValueError(f"{self.name}: unsupported VTable type {self.type!r}")


@dataclass(frozen=True)
class SimAck:
    status: SimStatus
    request_type: int
    index: int
    count: int
    name: str
    value: SimValue | None


def encode_name(name: str) -> bytes:
    try:
        encoded = name.encode("ascii")
    except UnicodeEncodeError as error:
        raise ValueError("VTable names must contain ASCII characters only") from error
    if not 1 <= len(encoded) <= VT_NAME_LEN:
        raise ValueError("VTable names must contain 1..8 ASCII bytes")
    if b"\0" in encoded:
        raise ValueError("VTable names cannot contain NUL bytes")
    return encoded.ljust(VT_NAME_LEN, b"\0")


def decode_name(encoded: bytes) -> str:
    if len(encoded) != VT_NAME_LEN:
        raise ValueError("encoded VTable name must be exactly 8 bytes")
    return encoded.rstrip(b"\0").decode("ascii")


def encode_sim_set(sequence: int, item: SimValue) -> bytes:
    raw_value = item.encoded_value()
    payload = SIM_SET_PAYLOAD.pack(
        encode_name(item.name),
        int(item.type),
        len(raw_value),
        raw_value.ljust(VT_VALUE_LEN, b"\0"),
    )
    return encode_frame(SimMessageType.SET, sequence, payload)


def encode_sim_get(sequence: int, name: str) -> bytes:
    return encode_frame(
        SimMessageType.GET,
        sequence,
        SIM_GET_PAYLOAD.pack(encode_name(name)),
    )


def encode_sim_list(sequence: int) -> bytes:
    return encode_frame(SimMessageType.LIST, sequence)


def decode_sim_set(frame: Frame) -> SimValue:
    if frame.msg_type != SimMessageType.SET or len(frame.payload) != SIM_SET_PAYLOAD.size:
        raise ValueError("frame is not a valid SIM_SET")
    raw_name, raw_type, length, raw_value = SIM_SET_PAYLOAD.unpack(frame.payload)
    return _decode_value(raw_name, raw_type, length, raw_value)


def decode_sim_ack(frame: Frame) -> SimAck:
    if frame.msg_type != SimMessageType.ACK or len(frame.payload) != SIM_ACK_PAYLOAD.size:
        raise ValueError("frame is not a valid SIM_ACK")
    status, request_type, index, count, name, raw_type, length, raw_value = (
        SIM_ACK_PAYLOAD.unpack(frame.payload)
    )
    decoded_name = decode_name(name)
    decoded_value = None
    if length > 0:
        decoded_value = _decode_value(name, raw_type, length, raw_value)
    return SimAck(
        SimStatus(status), request_type, index, count, decoded_name, decoded_value
    )


def format_value(item: SimValue) -> str:
    if item.type == VtType.F32:
        return f"{float(item.value):.3f}"
    if item.type == VtType.BYTES:
        return bytes(item.value).hex(" ")
    return str(item.value)


def _decode_value(raw_name: bytes, raw_type: int, length: int, raw_value: bytes) -> SimValue:
    if not 1 <= length <= VT_VALUE_LEN:
        raise ValueError(f"invalid VTable value length {length}")
    value_type = VtType(raw_type)
    data = raw_value[:length]
    expected_lengths = {VtType.U32: 4, VtType.I32: 4, VtType.F32: 4}
    if value_type in expected_lengths and length != expected_lengths[value_type]:
        raise ValueError(f"{value_type.name} must contain exactly 4 bytes")
    decoders: dict[VtType, Any] = {
        VtType.U32: lambda value: struct.unpack("<I", value)[0],
        VtType.I32: lambda value: struct.unpack("<i", value)[0],
        VtType.F32: lambda value: struct.unpack("<f", value)[0],
        VtType.BYTES: bytes,
    }
    return SimValue(decode_name(raw_name), value_type, decoders[value_type](data))
