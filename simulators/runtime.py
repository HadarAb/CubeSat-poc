"""Shared CLI helpers and run loops for the node simulators."""

from __future__ import annotations

import argparse
import shlex
import threading
import time
from typing import Protocol

from .protocol import SimValue
from .transport import SimulatorTransport, list_serial_ports


class SimulationModel(Protocol):
    available_sensors: tuple[str, ...]
    sensors: frozenset[str]

    def step(self, dt_s: float) -> list[SimValue]: ...

    def status(self) -> str: ...

    def command_help(self) -> str: ...

    def apply_command(self, words: list[str]) -> str: ...


def add_common_arguments(parser: argparse.ArgumentParser) -> None:
    parser.add_argument("--port", help="board COM port; omit for dry-run mode")
    parser.add_argument("--baudrate", type=int, default=115200)
    parser.add_argument("--list-ports", action="store_true")
    parser.add_argument("--interval", type=float, default=1.0, help="update period in seconds")
    parser.add_argument(
        "--time-scale",
        type=float,
        default=1.0,
        help="simulated seconds per real second (for example 3600 accelerates one hour into one second)",
    )
    parser.add_argument(
        "--steps",
        type=int,
        help="send a finite number of update cycles and exit instead of opening the console",
    )
    parser.add_argument(
        "--sensor",
        action="append",
        default=[],
        metavar="NAME[,NAME]",
        help="sensor group to simulate; repeat or pass a comma-separated list",
    )
    parser.add_argument("--seed", type=int, help="repeatable pseudo-random sequence")
    parser.add_argument("--show-frames", action="store_true", help="print encoded UART bytes")


def parse_sensor_selection(
    requested: list[str],
    available: tuple[str, ...],
    aliases: dict[str, str],
) -> set[str]:
    if not requested:
        return set(available)
    selected: set[str] = set()
    for group in requested:
        for raw_name in group.split(","):
            name = raw_name.strip().lower()
            canonical = aliases.get(name, name)
            if canonical not in available:
                choices = ", ".join(available)
                raise ValueError(f"unknown sensor group '{raw_name}'; choose from: {choices}")
            selected.add(canonical)
    return selected


def maybe_list_ports(args: argparse.Namespace) -> bool:
    if not args.list_ports:
        return False
    ports = list_serial_ports()
    if not ports:
        print("No serial ports found.")
    for device, description in ports:
        print(f"{device}: {description}")
    return True


def run_simulator(
    model: SimulationModel,
    transport: SimulatorTransport,
    *,
    interval_s: float,
    time_scale: float,
    steps: int | None,
) -> None:
    if interval_s <= 0.0:
        raise ValueError("interval must be greater than zero")
    if steps is not None and steps <= 0:
        raise ValueError("steps must be greater than zero")
    if time_scale <= 0.0:
        raise ValueError("time scale must be greater than zero")
    print(f"[sim] active sensor groups: {', '.join(sorted(model.sensors))}")
    print(f"[sim] {model.status()}")
    with transport:
        if steps is not None:
            _run_steps(model, transport, interval_s, time_scale, steps)
        else:
            _run_interactive(model, transport, interval_s, time_scale)


def _run_steps(
    model: SimulationModel,
    transport: SimulatorTransport,
    interval_s: float,
    time_scale: float,
    steps: int,
) -> None:
    for index in range(steps):
        transport.send_values(model.step(interval_s * time_scale))
        if index + 1 < steps:
            time.sleep(interval_s)


def _run_interactive(
    model: SimulationModel,
    transport: SimulatorTransport,
    interval_s: float,
    time_scale: float,
) -> None:
    stopped = threading.Event()

    def produce() -> None:
        previous = time.monotonic()
        while not stopped.is_set():
            now = time.monotonic()
            transport.send_values(model.step((now - previous) * time_scale))
            previous = now
            stopped.wait(interval_s)

    worker = threading.Thread(target=produce, name="sim-producer", daemon=True)
    worker.start()
    print(f"[sim] commands: {model.command_help()}")
    try:
        while not stopped.is_set():
            words = shlex.split(input("sim> ").strip())
            if not words:
                continue
            command = words[0].lower()
            if command in {"quit", "exit"}:
                stopped.set()
            elif command == "help":
                print(model.command_help())
            elif command == "status":
                print(model.status())
            else:
                try:
                    print(model.apply_command(words))
                except ValueError as error:
                    print(f"error: {error}")
    except (EOFError, KeyboardInterrupt):
        stopped.set()
    finally:
        stopped.set()
        worker.join(timeout=max(1.0, interval_s + 0.5))
        print("[sim] stopped")
