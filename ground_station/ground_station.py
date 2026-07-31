"""Interactive serial ground station for the CubeSat OBC."""

from __future__ import annotations

import argparse
import sys
import time

from protocol import (
    Frame,
    FrameParser,
    MessageType,
    Status,
    decode_debug_text,
    decode_payload,
    encode_request,
)


BAUD_RATE = 115200
RESPONSE_TIMEOUT_SECONDS = 2.0


class GroundStation:
    def __init__(self, serial_port) -> None:
        self.serial = serial_port
        self.parser = FrameParser()
        self.sequence = 0

    def _next_sequence(self) -> int:
        self.sequence = (self.sequence + 1) & 0xFFFF
        return self.sequence

    def _read_frames(self) -> list[Frame]:
        waiting = self.serial.in_waiting
        data = self.serial.read(waiting if waiting > 0 else 1)
        return self.parser.feed(data)

    def request(self, msg_type: MessageType) -> Frame:
        sequence = self._next_sequence()
        self.serial.write(encode_request(msg_type, sequence))
        self.serial.flush()

        deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
        while time.monotonic() < deadline:
            for frame in self._read_frames():
                if frame.msg_type == MessageType.DEBUG_TEXT:
                    print(f"[OBC] {decode_debug_text(frame)}")
                    continue

                if frame.sequence != sequence:
                    continue

                if frame.msg_type == MessageType.ERROR:
                    error = decode_payload(frame)
                    raise RuntimeError(
                        f"OBC error: {_status_name(error.status)}"
                    )

                if frame.msg_type != msg_type:
                    raise RuntimeError(
                        f"unexpected response type 0x{frame.msg_type:02X}"
                    )

                return frame

        raise TimeoutError(
            f"no response from OBC within {RESPONSE_TIMEOUT_SECONDS:.1f}s"
        )


def _status_name(value: int) -> str:
    try:
        return Status(value).name
    except ValueError:
        return f"UNKNOWN(0x{value:02X})"


def show_status(station: GroundStation) -> None:
    frame = station.request(MessageType.STATUS)
    payload = decode_payload(frame)
    print(f"OBC status: {_status_name(payload.status)}")
    print(f"Uptime: {frame.timestamp_ms} ms")
    print(f"Payload valid: {'yes' if payload.valid else 'no'}")
    print(f"Payload node: 0x{payload.node_id:02X}")
    print(
        f"I2C reads: {payload.i2c_success_count} successful, "
        f"{payload.i2c_error_count} failed"
    )


def show_payload(station: GroundStation) -> None:
    payload = decode_payload(station.request(MessageType.PAYLOAD))
    if not payload.valid:
        print("No valid payload is cached; all measurement fields are zero.")
        return

    print(f"Payload timestamp: {payload.timestamp_ms} ms")
    print(f"Temperature: {payload.temperature_c_x10 / 10:.1f} C")
    print(f"Humidity: {payload.humidity_pct_x10 / 10:.1f} %")
    print(f"Radiation: {payload.radiation_cps} cps")
    print(f"Battery: {payload.battery_pct} %")
    print(f"Node: 0x{payload.node_id:02X}")
    print(f"Flags: 0x{payload.flags:02X}")


def show_battery(station: GroundStation) -> None:
    payload = decode_payload(station.request(MessageType.BATTERY))
    if not payload.valid:
        print("Battery: 0 % (no valid payload is cached)")
        return
    print(f"Battery: {payload.battery_pct} %")


def interactive_loop(station: GroundStation) -> None:
    print("Connected. Commands: status, payload, battery, help, quit")
    actions = {
        "status": show_status,
        "payload": show_payload,
        "battery": show_battery,
    }

    while True:
        try:
            command = input("gs> ").strip().lower()
        except (EOFError, KeyboardInterrupt):
            print()
            return

        if command in {"quit", "exit", "q"}:
            return
        if command == "help":
            print("status  - request OBC and I2C status")
            print("payload - request the latest cached telemetry")
            print("battery - request the latest battery percentage")
            print("quit    - close the ground station")
            continue
        if not command:
            continue

        action = actions.get(command)
        if action is None:
            print(f"Unknown command: {command!r}. Type 'help'.")
            continue

        try:
            action(station)
        except (TimeoutError, RuntimeError, ValueError) as error:
            print(f"Error: {error}")


def list_ports() -> int:
    from serial.tools import list_ports

    ports = list(list_ports.comports())
    if not ports:
        print("No serial ports found.")
        return 1
    for port in ports:
        print(f"{port.device}: {port.description}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--port",
        help="serial port connected to the NUCLEO, for example COM5",
    )
    parser.add_argument(
        "--list-ports",
        action="store_true",
        help="show available serial ports and exit",
    )
    args = parser.parse_args()

    try:
        import serial
    except ImportError:
        print(
            "pyserial is not installed. Run: "
            "python -m pip install -r requirements.txt",
            file=sys.stderr,
        )
        return 2

    if args.list_ports:
        return list_ports()
    if not args.port:
        parser.error("--port is required unless --list-ports is used")

    try:
        with serial.Serial(
            args.port,
            BAUD_RATE,
            timeout=0.05,
            write_timeout=1.0,
        ) as serial_port:
            interactive_loop(GroundStation(serial_port))
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
