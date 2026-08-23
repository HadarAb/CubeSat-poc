"""Deterministic-enough models for exercising Payload and EPS firmware."""

from __future__ import annotations

import math
import random
import threading

from .protocol import SimValue, VtType


class PayloadModel:
    """Produces the four Payload keys required by the Phase 4 workplan."""

    available_sensors = ("temperature", "radiation", "sel", "reset")

    def __init__(
        self,
        sensors: set[str],
        *,
        temperature_c: float = 24.5,
        dose: float = 0.0,
        dose_rate: float = 0.02,
        sel_rate_per_minute: float = 0.1,
        reset_count: int = 0,
        seed: int | None = None,
    ) -> None:
        self.sensors = frozenset(sensors)
        self.temperature_c = float(temperature_c)
        self.temperature_target_c = float(temperature_c)
        self.total_dose = _nonnegative("total dose", dose)
        self.dose_rate = _nonnegative("dose rate", dose_rate)
        self.sel_rate_per_minute = _nonnegative("SEL rate", sel_rate_per_minute)
        self.sel_count = 0
        self.reset_count = _u32("reset count", reset_count)
        self._rng = random.Random(seed)
        self._lock = threading.RLock()

    def step(self, dt_s: float) -> list[SimValue]:
        with self._lock:
            dt_s = max(0.0, float(dt_s))
            # Slow mean reversion plus noise keeps temperature moving without
            # drifting forever.
            self.temperature_c += (
                (self.temperature_target_c - self.temperature_c) * 0.03 * dt_s
                + self._rng.uniform(-0.08, 0.08) * math.sqrt(dt_s)
            )
            self.total_dose += self.dose_rate * dt_s
            self.sel_count = min(
                0xFFFFFFFF,
                self.sel_count
                + _poisson(self._rng, self.sel_rate_per_minute * dt_s / 60.0),
            )

            values: list[SimValue] = []
            if "temperature" in self.sensors:
                values.append(SimValue("TEMP", VtType.F32, self.temperature_c))
            if "radiation" in self.sensors:
                values.append(SimValue("TDOSE", VtType.F32, self.total_dose))
            if "sel" in self.sensors:
                values.append(SimValue("SEL", VtType.U32, self.sel_count))
            if "reset" in self.sensors:
                values.append(SimValue("NRESET", VtType.U32, self.reset_count))
            return values

    def status(self) -> str:
        with self._lock:
            return (
                f"TEMP={self.temperature_c:.2f} C, TDOSE={self.total_dose:.4f}, "
                f"dose_rate={self.dose_rate:.4f}/s, SEL={self.sel_count}, "
                f"sel_rate={self.sel_rate_per_minute:.3f}/min, NRESET={self.reset_count}"
            )

    def command_help(self) -> str:
        return (
            "temperature <C> | dose-rate <units/s> | sel-rate <events/min> | "
            "sel [count] | reset [count] | status | help | quit"
        )

    def apply_command(self, words: list[str]) -> str:
        if not words:
            return ""
        command = words[0].lower()
        with self._lock:
            if command in {"temperature", "temp"} and len(words) == 2:
                self.temperature_target_c = float(words[1])
                return f"temperature target set to {self.temperature_target_c:.2f} C"
            if command in {"dose-rate", "dose"} and len(words) == 2:
                self.dose_rate = _nonnegative("dose rate", float(words[1]))
                return f"dose rate set to {self.dose_rate:.4f}/s"
            if command == "sel-rate" and len(words) == 2:
                self.sel_rate_per_minute = _nonnegative("SEL rate", float(words[1]))
                return f"SEL rate set to {self.sel_rate_per_minute:.3f}/min"
            if command == "sel" and len(words) in {1, 2}:
                amount = int(words[1]) if len(words) == 2 else 1
                self.sel_count = _u32("SEL count", self.sel_count + amount)
                return f"injected {amount} SEL event(s); SEL={self.sel_count}"
            if command == "reset" and len(words) in {1, 2}:
                amount = int(words[1]) if len(words) == 2 else 1
                self.reset_count = _u32("reset count", self.reset_count + amount)
                return f"injected {amount} reset(s); NRESET={self.reset_count}"
        raise ValueError(f"unknown or malformed command: {' '.join(words)}")


class EpsModel:
    """Simple battery/load/solar model designed for power-mode testing."""

    available_sensors = ("battery", "temperature", "solar")

    def __init__(
        self,
        sensors: set[str],
        *,
        capacity_ah: float = 2.6,
        state_of_charge_pct: float = 80.0,
        load_current_a: float = 0.25,
        solar_current_a: float = 0.0,
        temperature_c: float = 25.0,
        voltage_override: float | None = None,
        seed: int | None = None,
    ) -> None:
        self.sensors = frozenset(sensors)
        self.capacity_ah = _positive("capacity", capacity_ah)
        self.state_of_charge_pct = _percentage(state_of_charge_pct)
        self.load_current_a = _nonnegative("load current", load_current_a)
        self.solar_current_a = _nonnegative("solar current", solar_current_a)
        self.temperature_c = float(temperature_c)
        self.voltage_override = (
            None if voltage_override is None else _nonnegative("voltage", voltage_override)
        )
        self._rng = random.Random(seed)
        self._lock = threading.RLock()

    @property
    def voltage_v(self) -> float:
        if self.voltage_override is not None:
            return self.voltage_override
        open_circuit = 3.30 + 0.90 * (self.state_of_charge_pct / 100.0)
        discharge_current = max(0.0, self.load_current_a - self.solar_current_a)
        return max(0.0, open_circuit - 0.12 * discharge_current)

    def step(self, dt_s: float) -> list[SimValue]:
        with self._lock:
            dt_s = max(0.0, float(dt_s))
            net_discharge_a = self.load_current_a - self.solar_current_a
            delta_pct = net_discharge_a * dt_s / (3600.0 * self.capacity_ah) * 100.0
            self.state_of_charge_pct = min(
                100.0, max(0.0, self.state_of_charge_pct - delta_pct)
            )
            heat_target = 25.0 + 4.0 * self.load_current_a**2
            self.temperature_c += (
                (heat_target - self.temperature_c) * 0.02 * dt_s
                + self._rng.uniform(-0.04, 0.04) * math.sqrt(dt_s)
            )

            values: list[SimValue] = []
            if "battery" in self.sensors:
                values.append(SimValue("VBAT", VtType.F32, self.voltage_v))
            if "temperature" in self.sensors:
                values.append(SimValue("TEMP", VtType.F32, self.temperature_c))
            if "solar" in self.sensors:
                per_panel = self.solar_current_a / 6.0
                light_heat = min(15.0, self.solar_current_a * 8.0)
                for panel in range(6):
                    panel_temp = self.temperature_c + light_heat + self._rng.uniform(-0.5, 0.5)
                    panel_current = max(
                        0.0, per_panel * (1.0 + self._rng.uniform(-0.06, 0.06))
                    )
                    values.append(SimValue(f"SP{panel}_T", VtType.F32, panel_temp))
                    values.append(SimValue(f"SP{panel}_I", VtType.F32, panel_current))
            return values

    def status(self) -> str:
        with self._lock:
            voltage_source = "manual" if self.voltage_override is not None else "model"
            return (
                f"VBAT={self.voltage_v:.3f} V ({voltage_source}), "
                f"SOC={self.state_of_charge_pct:.2f}%, capacity={self.capacity_ah:.3f} Ah, "
                f"load={self.load_current_a:.3f} A, solar={self.solar_current_a:.3f} A, "
                f"TEMP={self.temperature_c:.2f} C"
            )

    def command_help(self) -> str:
        return (
            "voltage <V|auto> | load <A> | drain <A> | solar <A> | "
            "capacity <Ah> | soc <percent> | status | help | quit"
        )

    def apply_command(self, words: list[str]) -> str:
        if not words:
            return ""
        command = words[0].lower()
        with self._lock:
            if command == "voltage" and len(words) == 2:
                if words[1].lower() == "auto":
                    self.voltage_override = None
                    return "manual voltage override disabled"
                self.voltage_override = _nonnegative("voltage", float(words[1]))
                return f"VBAT manually overridden to {self.voltage_override:.3f} V"
            if command in {"load", "drain"} and len(words) == 2:
                self.load_current_a = _nonnegative("load current", float(words[1]))
                return f"load current set to {self.load_current_a:.3f} A"
            if command in {"solar", "charge"} and len(words) == 2:
                self.solar_current_a = _nonnegative("solar current", float(words[1]))
                return f"solar current set to {self.solar_current_a:.3f} A"
            if command == "capacity" and len(words) == 2:
                self.capacity_ah = _positive("capacity", float(words[1]))
                return f"capacity set to {self.capacity_ah:.3f} Ah"
            if command == "soc" and len(words) == 2:
                self.state_of_charge_pct = _percentage(float(words[1]))
                return f"state of charge set to {self.state_of_charge_pct:.2f}%"
        raise ValueError(f"unknown or malformed command: {' '.join(words)}")


def _poisson(rng: random.Random, expected: float) -> int:
    if expected <= 0.0:
        return 0
    if expected > 30.0:
        return max(0, round(rng.gauss(expected, math.sqrt(expected))))
    threshold = math.exp(-expected)
    product = 1.0
    count = 0
    while product > threshold:
        count += 1
        product *= rng.random()
    return count - 1


def _positive(label: str, value: float) -> float:
    value = float(value)
    if value <= 0.0:
        raise ValueError(f"{label} must be greater than zero")
    return value


def _nonnegative(label: str, value: float) -> float:
    value = float(value)
    if value < 0.0:
        raise ValueError(f"{label} cannot be negative")
    return value


def _percentage(value: float) -> float:
    value = float(value)
    if not 0.0 <= value <= 100.0:
        raise ValueError("state of charge must be between 0 and 100 percent")
    return value


def _u32(label: str, value: int) -> int:
    value = int(value)
    if not 0 <= value <= 0xFFFFFFFF:
        raise ValueError(f"{label} must fit in uint32")
    return value
