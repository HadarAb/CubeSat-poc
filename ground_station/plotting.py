"""Plots a fetch result, one line per sensor key."""

from __future__ import annotations

from datetime import datetime

try:
    from .sensors import decode_value, describe_sensor
except ImportError:
    from sensors import decode_value, describe_sensor


def plot_records(records: list) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError:
        print("matplotlib is not installed. Run: python -m pip install matplotlib")
        return

    if not records:
        print("nothing to plot")
        return

    # Group by sensor so each key becomes its own line.
    series: dict[str, tuple[list, list]] = {}
    for record in records:
        node, key, vt_type = describe_sensor(record.sensor_id)
        label = f"{node}.{key}"
        times, values = series.setdefault(label, ([], []))
        times.append(datetime.fromtimestamp(record.epoch_s))
        values.append(decode_value(record.value, vt_type))

    figure, axes = plt.subplots(figsize=(10, 5))
    for label, (times, values) in sorted(series.items()):
        axes.plot(times, values, marker=".", linewidth=1, label=label)

    axes.set_xlabel("time")
    axes.set_ylabel("value")
    axes.set_title(f"CubeSat fetch: {len(records)} records")
    axes.grid(True, alpha=0.3)
    axes.legend(loc="best", fontsize="small")
    figure.autofmt_xdate()
    plt.tight_layout()
    plt.show()
