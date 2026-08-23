"""Interactive PC-driven battery and solar simulator for the CubeSat EPS node."""

from __future__ import annotations

import argparse

from simulators.models import EpsModel
from simulators.runtime import (
    add_common_arguments,
    maybe_list_ports,
    parse_sensor_selection,
    run_simulator,
)
from simulators.transport import SimulatorTransport


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    add_common_arguments(parser)
    parser.add_argument("--capacity-ah", type=float, default=2.6)
    parser.add_argument("--soc", type=float, default=80.0, help="initial charge percentage")
    parser.add_argument("--load-a", type=float, default=0.25, help="battery drain current")
    parser.add_argument("--solar-a", type=float, default=0.0, help="total solar charging current")
    parser.add_argument("--temperature", type=float, default=25.0)
    parser.add_argument("--voltage", type=float, help="manual VBAT override")
    args = parser.parse_args()
    try:
        if maybe_list_ports(args):
            return 0
        sensors = parse_sensor_selection(
            args.sensor,
            EpsModel.available_sensors,
            {"vbat": "battery", "temp": "temperature", "sunlight": "solar", "panels": "solar"},
        )
        model = EpsModel(
            sensors,
            capacity_ah=args.capacity_ah,
            state_of_charge_pct=args.soc,
            load_current_a=args.load_a,
            solar_current_a=args.solar_a,
            temperature_c=args.temperature,
            voltage_override=args.voltage,
            seed=args.seed,
        )
        transport = SimulatorTransport(
            port=args.port,
            baudrate=args.baudrate,
            show_frames=args.show_frames,
        )
        print(f"EPS simulator: {', '.join(sorted(sensors))}")
        run_simulator(
            model,
            transport,
            interval_s=args.interval,
            time_scale=args.time_scale,
            steps=args.steps,
        )
        return 0
    except (OSError, RuntimeError, ValueError) as error:
        parser.error(str(error))
        return 2


if __name__ == "__main__":
    raise SystemExit(main())
