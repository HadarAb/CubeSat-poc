"""Interactive PC-driven simulator for the CubeSat Payload node."""

from __future__ import annotations

import argparse

from simulators.models import PayloadModel
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
    parser.add_argument("--temperature", type=float, default=24.5, help="initial TEMP in C")
    parser.add_argument("--dose", type=float, default=0.0, help="initial TDOSE")
    parser.add_argument("--dose-rate", type=float, default=0.02, help="TDOSE units per second")
    parser.add_argument("--sel-rate", type=float, default=0.1, help="SEL events per minute")
    parser.add_argument("--reset-count", type=int, default=0, help="initial NRESET")
    args = parser.parse_args()
    try:
        if maybe_list_ports(args):
            return 0
        sensors = parse_sensor_selection(
            args.sensor,
            PayloadModel.available_sensors,
            {"temp": "temperature", "dose": "radiation", "tdose": "radiation", "nreset": "reset"},
        )
        model = PayloadModel(
            sensors,
            temperature_c=args.temperature,
            dose=args.dose,
            dose_rate=args.dose_rate,
            sel_rate_per_minute=args.sel_rate,
            reset_count=args.reset_count,
            seed=args.seed,
        )
        transport = SimulatorTransport(
            port=args.port,
            baudrate=args.baudrate,
            show_frames=args.show_frames,
        )
        print(f"Payload simulator: {', '.join(sorted(sensors))}")
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
