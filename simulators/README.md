# PC-driven node simulators

## What each file does

```text
models.py           = generate values / the actual simulator for both eps and payload 
protocol.py         = convert values to UART messages
transport.py        = send messages through the COM port
runtime.py          = keep everything running and interactive / loop
payload_sim.py      = Payload program you launch 
eps_sim.py          = EPS program you launch
README.md           = instructions
```

Tests are kept outside the simulator code in `tests/simulators/`.

The Payload and EPS use the same UART envelope (`0xA55A`, header, payload,
CRC32), but run as separate programs because they model different hardware.
Both default to dry-run mode, so their values and complete frames can be tested
before the STM32 UART receiver is implemented.

Install serial support when connecting a board:

```powershell
python -m pip install -r ground_station/requirements.txt
```

## Payload

The supported groups are `temperature`, `radiation`, `sel`, and `reset`.
With no `--sensor` argument, all four are active. Select one or several:

```powershell
python payload_sim.py --sensor radiation --steps 3
python payload_sim.py --sensor temperature,sel --port COM5
python payload_sim.py --sensor temp --sensor reset --port COM5
```

Without `--steps`, an interactive console stays open while updates continue in
the background. Useful commands include:

```text
temperature 32
dose-rate 0.5
sel-rate 4
sel 3
reset
status
quit
```

Both node simulators also accept `get <key>` and `list`. These send `SIM_GET`
and `SIM_LIST` to the connected board and print its `SIM_ACK` responses.

The standard Payload VTable keys are `TEMP`, `TDOSE`, `SEL`, and `NRESET`.

## EPS

The EPS has separate `battery`, `temperature`, and `solar` groups. Battery
charge uses simple coulomb counting:

```powershell
python eps_sim.py --capacity-ah 2.6 --soc 80 --load-a 0.4 --solar-a 0.1 --port COM6
python eps_sim.py --sensor battery --voltage 3.45 --steps 2 --show-frames
python eps_sim.py --sensor battery,solar --time-scale 3600 --steps 3
```

The interactive console can change the test conditions while it runs:

```text
load 0.8
solar 0.2
voltage 3.40
voltage auto
capacity 3.0
soc 65
status
quit
```

`voltage <V>` forces `VBAT`, which is useful for testing OBC power-mode
thresholds. `voltage auto` returns to the capacity/load/solar model. Solar mode
produces `SP0_T..SP5_T` and `SP0_I..SP5_I`; EPS also publishes `VBAT` and
`TEMP`. `--time-scale 3600` makes every real second represent one simulated
hour, so capacity and drain tests do not need to run overnight.

## Simulator UART payloads

Message IDs and C layouts are frozen in `common/uart/uart_protocol.h`.

- `SIM_SET (0x40)`: `name[8], type, len, value[8]`
- `SIM_GET (0x41)`: `name[8]`
- `SIM_LIST (0x42)`: empty request payload
- `SIM_ACK (0x43)`: status, request type, index/count, name/type/len/value

Names are ASCII and NUL-padded, but an eight-character name has no terminator.
Integers and floats are little-endian. CRC32 covers the common UART header and
the complete simulator payload.

## Tests

```powershell
python -m unittest discover -s tests -t . -p "test_*.py" -v
```
