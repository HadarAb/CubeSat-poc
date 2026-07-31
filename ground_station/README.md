# CubeSat UART Ground Station

The ground station talks to the OBC through the NUCLEO ST-LINK virtual COM
port at 115200 baud, 8 data bits, no parity, and 1 stop bit.

## Setup

```powershell
cd ground_station
python -m pip install -r requirements.txt
python ground_station.py --list-ports
python ground_station.py --port COM5
```

Replace `COM5` with the port shown on your computer.

Available interactive commands:

- `status` requests OBC and I2C health information.
- `payload` requests the latest CRC-valid payload cached by the OBC.
- `battery` requests only the cached battery percentage.
- `help` displays the command list.
- `quit` closes the program.

Each request is only a UART header plus CRC. The OBC replies with the same
message type and the single fixed `UartPayload_t` layout.

Firmware calls to `SendUartMsg("my text")` appear as `[OBC] my text`. Debug
text is sent inside a normal CRC-protected frame, so it does not corrupt
command or telemetry traffic. Text longer than 64 bytes is truncated.

## Tests

The protocol tests do not require serial hardware or `pyserial`:

```powershell
python -m unittest -v test_protocol.py
```
