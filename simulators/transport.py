"""Serial and dry-run transport for simulator VTable updates."""

from __future__ import annotations

import threading

from ground_station.protocol import FrameParser

from .protocol import SimMessageType, SimValue, decode_sim_ack, encode_sim_set, format_value


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
                self._sequence = (self._sequence + 1) & 0xFFFF
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

    def _poll_responses(self) -> None:
        if self._serial is None or self._serial.in_waiting <= 0:
            return
        for frame in self._parser.feed(self._serial.read(self._serial.in_waiting)):
            if frame.msg_type == SimMessageType.ACK:
                ack = decode_sim_ack(frame)
                key = ack.name or "-"
                print(
                    f"[board ack] status={ack.status.name} request=0x{ack.request_type:02X} "
                    f"key={key} index={ack.index} count={ack.count}"
                )
            else:
                print(f"[board] frame type=0x{frame.msg_type:02X} seq={frame.sequence}")


def list_serial_ports() -> list[tuple[str, str]]:
    try:
        from serial.tools import list_ports
    except ImportError as error:
        raise RuntimeError(
            "pyserial is not installed; run: python -m pip install -r "
            "ground_station/requirements.txt"
        ) from error
    return [(port.device, port.description) for port in list_ports.comports()]
