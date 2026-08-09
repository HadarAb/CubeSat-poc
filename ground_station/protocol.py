"""CubeSat OBC UART protocol encoder and streaming decoder."""

from __future__ import annotations

from dataclasses import dataclass
from enum import IntEnum
import struct
import time
import zlib

RECORD_FMT = '<IIBBBBhHHHB7sI' # struct.calcsize(RECORD_FMT) == 32

FRAME_START = 0xA55A
MAX_PAYLOAD_SIZE = 64

# Must match UartFrameHeader_t and UartPayload_t in common/uart/uart_protocol.h.
HEADER = struct.Struct("<HBHIH")
PAYLOAD = struct.Struct("<BBBBIhHHBII")
CRC = struct.Struct("<I")
START_BYTES = struct.pack("<H", FRAME_START)



class MessageType(IntEnum):
    STATUS = 0x01
    PAYLOAD = 0x02
    BATTERY = 0x03
    DEBUG_TEXT = 0x70
    ERROR = 0xFF


class Status(IntEnum):
    OK = 0x00
    NO_DATA = 0x01
    I2C_ERROR = 0x02
    CRC_ERROR = 0x03
    UNKNOWN_MESSAGE = 0x80
    BAD_REQUEST = 0x81


@dataclass(frozen=True)
class Frame:
    msg_type: int
    sequence: int
    timestamp_ms: int
    payload: bytes


@dataclass(frozen=True)
class UartPayload:
    valid: bool
    status: int
    node_id: int
    flags: int
    timestamp_ms: int
    temperature_c_x10: int
    humidity_pct_x10: int
    radiation_cps: int
    battery_pct: int
    i2c_success_count: int
    i2c_error_count: int


def timestamp_ms() -> int:
    return int(time.monotonic() * 1000) & 0xFFFFFFFF


def encode_frame(
    msg_type: int,
    sequence: int,
    payload: bytes = b"",
    *,
    frame_timestamp_ms: int | None = None,
) -> bytes:
    payload = bytes(payload)
    if len(payload) > MAX_PAYLOAD_SIZE:
        raise ValueError(
            f"payload is {len(payload)} bytes; maximum is {MAX_PAYLOAD_SIZE}"
        )
    if not 0 <= sequence <= 0xFFFF:
        raise ValueError("sequence must fit in uint16")
    if frame_timestamp_ms is None:
        frame_timestamp_ms = timestamp_ms()

    header = HEADER.pack(
        FRAME_START,
        int(msg_type),
        sequence,
        frame_timestamp_ms & 0xFFFFFFFF,
        len(payload),
    )
    body = header + payload
    return body + CRC.pack(zlib.crc32(body) & 0xFFFFFFFF)


def encode_request(
    msg_type: MessageType | int,
    sequence: int,
    payload: bytes = b"",
    *,
    frame_timestamp_ms: int | None = None,
) -> bytes:
    """Requests contain a header, an optional payload, and a CRC.

    Commands that take arguments pass them in payload; the OBC checks the
    length per command.
    """

    return encode_frame(
        msg_type,
        sequence,
        payload,
        frame_timestamp_ms=frame_timestamp_ms,
    )


class FrameParser:
    """Incrementally parses fragmented, combined, or noisy serial input."""

    def __init__(self) -> None:
        self._buffer = bytearray()
        self.crc_error_count = 0
        self.length_error_count = 0

    def feed(self, data: bytes) -> list[Frame]:
        self._buffer.extend(data)
        frames: list[Frame] = []

        while True:
            start_index = self._buffer.find(START_BYTES)
            if start_index < 0:
                if self._buffer.endswith(START_BYTES[:1]):
                    self._buffer[:] = START_BYTES[:1]
                else:
                    self._buffer.clear()
                break

            if start_index > 0:
                del self._buffer[:start_index]

            if len(self._buffer) < HEADER.size:
                break

            start, msg_type, sequence, frame_time, payload_length = (
                HEADER.unpack_from(self._buffer)
            )
            if start != FRAME_START:
                del self._buffer[0]
                continue

            if payload_length > MAX_PAYLOAD_SIZE:
                self.length_error_count += 1
                del self._buffer[0]
                continue

            frame_size = HEADER.size + payload_length + CRC.size
            if len(self._buffer) < frame_size:
                break

            frame_without_crc = bytes(self._buffer[: frame_size - CRC.size])
            received_crc = CRC.unpack_from(
                self._buffer, frame_size - CRC.size
            )[0]
            calculated_crc = zlib.crc32(frame_without_crc) & 0xFFFFFFFF

            if received_crc != calculated_crc:
                self.crc_error_count += 1
                del self._buffer[0]
                continue

            frames.append(
                Frame(
                    msg_type=msg_type,
                    sequence=sequence,
                    timestamp_ms=frame_time,
                    payload=bytes(
                        self._buffer[
                            HEADER.size : HEADER.size + payload_length
                        ]
                    ),
                )
            )
            del self._buffer[:frame_size]

        return frames


def decode_payload(frame: Frame) -> UartPayload:
    if len(frame.payload) != PAYLOAD.size:
        raise ValueError(
            f"message 0x{frame.msg_type:02X} has "
            f"{len(frame.payload)} payload bytes; expected {PAYLOAD.size}"
        )

    values = PAYLOAD.unpack(frame.payload)
    return UartPayload(
        valid=bool(values[0]),
        status=values[1],
        node_id=values[2],
        flags=values[3],
        timestamp_ms=values[4],
        temperature_c_x10=values[5],
        humidity_pct_x10=values[6],
        radiation_cps=values[7],
        battery_pct=values[8],
        i2c_success_count=values[9],
        i2c_error_count=values[10],
    )


def decode_debug_text(frame: Frame) -> str:
    return frame.payload.decode("utf-8", errors="replace")
