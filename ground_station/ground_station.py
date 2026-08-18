"""Interactive serial ground station for the CubeSat OBC."""

from __future__ import annotations

import argparse
import queue
import sys
import threading
import time

try:
    from .protocol import (
        Frame,
        FrameParser,
        MessageType,
        Status,
        decode_debug_text,
        decode_payload,
        decode_status,
        encode_request,
    )
except ImportError:
    from protocol import (
        Frame,
        FrameParser,
        MessageType,
        Status,
        decode_debug_text,
        decode_payload,
        decode_status,
        encode_request,
    )


BAUD_RATE = 115200
RESPONSE_TIMEOUT_SECONDS = 2.0


class GroundStation:
    def __init__(self, serial_port) -> None:
        self.serial = serial_port
        self.parser = FrameParser()
        self.sequence = 0
        self.response_queue: queue.Queue[Frame] = queue.Queue()
        self.stop_event = threading.Event()
        self.reader_thread: threading.Thread | None = None

    # Start the only thread that is allowed to read from the serial port.
    def start(self) -> None:
        if self.reader_thread is not None and self.reader_thread.is_alive():
            return

        self.stop_event.clear()
        self.reader_thread = threading.Thread(
            target=self._reader_loop,
            name="ground-station-rx",
            daemon=True,
        )
        self.reader_thread.start()

    # Stop the receiver before the serial port is closed.
    def close(self) -> None:
        self.stop_event.set()

        if self.reader_thread is not None:
            self.reader_thread.join(timeout=1.0)

    # Keep receiving even while the console waits for the next user command.
    def _reader_loop(self) -> None:
        while not self.stop_event.is_set():
            for frame in self._read_frames():
                self._dispatch_frame(frame)

    # Display unsolicited frames now and queue requested replies for request().
    def _dispatch_frame(self, frame: Frame) -> None:
        if frame.msg_type == MessageType.AUTO_STATUS:
            show_automatic_status(frame)
            return

        if frame.msg_type == MessageType.DEBUG_TEXT:
            print(f"\n[OBC] {decode_debug_text(frame)}")
            return

        self.response_queue.put(frame)

    def _next_sequence(self) -> int:
        self.sequence = (self.sequence + 1) & 0xFFFF
        return self.sequence

    def _read_frames(self) -> list[Frame]:
        waiting = self.serial.in_waiting
        data = self.serial.read(waiting if waiting > 0 else 1)
        return self.parser.feed(data)

    def request(self, msg_type: MessageType) -> Frame:
        self.start()

        sequence = self._next_sequence()
        self.serial.write(encode_request(msg_type, sequence))
        self.serial.flush()

        deadline = time.monotonic() + RESPONSE_TIMEOUT_SECONDS
        while True:
            remaining = deadline - time.monotonic()

            if remaining <= 0.0:
                raise TimeoutError(
                    f"no response from OBC within {RESPONSE_TIMEOUT_SECONDS:.1f}s"
                )

            try:
                frame = self.response_queue.get(timeout=remaining)
            except queue.Empty:
                raise TimeoutError(
                    f"no response from OBC within {RESPONSE_TIMEOUT_SECONDS:.1f}s"
                ) from None

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


def _status_name(value: int) -> str:
    try:
        return Status(value).name
    except ValueError:
        return f"UNKNOWN(0x{value:02X})"


def _power_state_name(value: int) -> str:
    names = {
        0: "CRITICAL",
        1: "NORMAL",
        2: "FULL",
        0xFF: "UNKNOWN",
    }
    return names.get(value, f"UNKNOWN(0x{value:02X})")


def _sd_state_name(value: int) -> str:
    names = {
        0: "INITIALIZING",
        1: "READY",
        2: "ERROR",
    }
    return names.get(value, f"UNKNOWN(0x{value:02X})")


# Print the dedicated system-status payload used by manual and automatic STATUS.
def _show_system_status(label: str, frame: Frame) -> None:
    status = decode_status(frame)
    battery = f"{status.battery_pct}%" if status.battery_valid else "unavailable"

    print(f"{label} {_status_name(status.status)}")
    print(f"  uptime:              {frame.timestamp_ms} ms")
    print(f"  power state:          {_power_state_name(status.power_state)}")
    print(f"  battery:              {battery}")
    print(f"  Payload node:         {'online' if status.payload_online else 'offline'}")
    print(f"  EPS node:             {'online' if status.eps_online else 'offline'}")
    print(f"  SD logger:            {_sd_state_name(status.sd_state)}")
    print(f"  dropped records:      {status.dropped_frames}")
    print(f"  collector overruns:   {status.collector_overruns}")
    print(
        f"  I2C errors:           Payload={status.payload_i2c_errors}, "
        f"EPS={status.eps_i2c_errors}"
    )
    print(
        f"  I2C CRC failures:     Payload={status.payload_crc_failures}, "
        f"EPS={status.eps_crc_failures}"
    )
    print(f"  SD errors:            {status.sd_error_count}")


def show_status(station: GroundStation) -> None:
    frame = station.request(MessageType.STATUS)
    _show_system_status("[REQUESTED STATUS]", frame)


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


# Display an unsolicited system-status frame with a clear AUTO label.
def show_automatic_status(frame: Frame) -> None:
    try:
        _show_system_status("\n[AUTO STATUS]", frame)
    except ValueError as error:
        print(f"\n[AUTO STATUS ERROR] {error}")


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
            station = GroundStation(serial_port)
            station.start()

            try:
                interactive_loop(station)
            finally:
                station.close()
    except serial.SerialException as error:
        print(f"Serial error: {error}", file=sys.stderr)
        return 1

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
