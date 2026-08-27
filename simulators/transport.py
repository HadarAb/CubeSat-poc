"""Serial and dry-run transport for simulator VTable updates."""

from __future__ import annotations

import threading
import time

from ground_station.protocol import FrameParser

from .protocol import (
    SimMessageType,
    SimValue,
    decode_sim_ack,
    encode_sim_get,
    encode_sim_list,
    encode_sim_set,
    format_value,
)


class SimulatorTransport:
    def __init__(
        self,
        *,
        port: str | None,
        baudrate: int = 115200,
        show_frames: bool = False,
    ) -> None:
        self.port = port
        self.baudrate = baudrate
        self.show_frames = show_frames
        self._sequence = 0
        self._serial = None
        self._parser = FrameParser()
        self._lock = threading.Lock()

    @property
    def is_dry_run(self) -> bool:
        return self.port is None

    def open(self) -> None:
        if self.port is None:
            print("[sim] dry-run mode; pass --port COMx to send frames to a board")
            return
        try:
            import serial
        except ImportError as error:
            raise RuntimeError(
                "pyserial is not installed; run: python -m pip install -r "
                "ground_station/requirements.txt"
            ) from error
        self._serial = serial.Serial(
            port=self.port,
            baudrate=self.baudrate,
            bytesize=8,
            parity="N",
            stopbits=1,
            timeout=0,
            write_timeout=1.0,
        )
        print(f"[sim] connected to {self.port} at {self.baudrate} baud")

    def close(self) -> None:
        if self._serial is not None:
            self._serial.close()
            self._serial = None

    def __enter__(self) -> "SimulatorTransport":
        self.open()
        return self

    def __exit__(self, exc_type, exc_value, traceback) -> None:
        self.close()

    def send_values(self, values: list[SimValue]) -> None:
        with self._lock:
            for item in values:
                self._sequence = self._next_sequence()
                frame = encode_sim_set(self._sequence, item)
                destination = self.port or "dry-run"
                print(
                    f"[{destination} seq={self._sequence:05d}] "
                    f"{item.name}={format_value(item)} ({item.type.name})"
                )
                if self.show_frames:
                    print(f"  frame: {frame.hex(' ')}")
                if self._serial is not None:
                    self._serial.write(frame)
            self._poll_responses()

    def request_value(self, name: str) -> None:
        with self._lock:
            self._sequence = self._next_sequence()
            frame = encode_sim_get(self._sequence, name)
            self._send_request(frame, f"GET {name}")
            self._poll_responses(0.15)

    def request_list(self) -> None:
        with self._lock:
            self._sequence = self._next_sequence()
            frame = encode_sim_list(self._sequence)
            self._send_request(frame, "LIST")
            # A full table can return many ACK frames, so allow more receive time.
            self._poll_responses(0.50)

    def _next_sequence(self) -> int:
        return (self._sequence + 1) & 0xFFFF

    def _send_request(self, frame: bytes, description: str) -> None:
        destination = self.port or "dry-run"
        print(f"[{destination} seq={self._sequence:05d}] {description}")
        if self.show_frames:
            print(f"  frame: {frame.hex(' ')}")
        if self._serial is not None:
            self._serial.write(frame)

    def _poll_responses(self, wait_seconds: float = 0.0) -> None:
        if self._serial is None:
            return

        deadline = time.monotonic() + wait_seconds
        while True:
            waiting = self._serial.in_waiting
            if waiting > 0:
                self._print_responses(self._serial.read(waiting))

            if time.monotonic() >= deadline:
                return
            time.sleep(0.005)

    def _print_responses(self, received: bytes) -> None:
        for frame in self._parser.feed(received):
            if frame.msg_type != SimMessageType.ACK:
                print(f"[board] frame type=0x{frame.msg_type:02X} seq={frame.sequence}")
                continue

            ack = decode_sim_ack(frame)
            key = ack.name or "-"
            value = format_value(ack.value) if ack.value is not None else "-"
            print(
                f"[board ack] status={ack.status.name} request=0x{ack.request_type:02X} "
                f"key={key} value={value} index={ack.index} count={ack.count}"
            )


def list_serial_ports() -> list[tuple[str, str]]:
    try:
        from serial.tools import list_ports
    except ImportError as error:
        raise RuntimeError(
            "pyserial is not installed; run: python -m pip install -r "
            "ground_station/requirements.txt"
        ) from error
    return [(port.device, port.description) for port in list_ports.comports()]
