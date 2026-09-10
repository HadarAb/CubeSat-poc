# CubeSat POC

A three-node CubeSat proof-of-concept built on STM32L476RG (NUCLEO-L476RG)
boards, with a Python ground station and PC-driven hardware simulators for
development without physical sensors.

The system models a satellite On-Board Computer (OBC) that polls two
subordinate nodes — an Electrical Power System (EPS) and a Payload — over
I2C, logs their telemetry to an SD card, and exchanges commands and
telemetry with a ground station over UART.

## Architecture

```text
                        UART (115200 8N1, 0xA55A framed)
   Ground Station  <───────────────────────────────────>  OBC (STM32L476RG)
   (Python)                                                    │
                                                                 │ I2C (register file protocol)
                                                        ┌────────┴────────┐
                                                        │                 │
                                                  EPS (STM32L476RG)  Payload (STM32L476RG)
                                                        │                 │
                                                  UART (sim)        UART (sim)
                                                        │                 │
                                                  eps_sim.py       payload_sim.py
```

- **OBC** — FreeRTOS firmware. I2C master to the EPS and Payload, UART
  protocol endpoint for the ground station, telemetry logger to a
  microSD card over SPI/FatFs, and a watchdog-supervised task set.
- **EPS** and **Payload** — bare-metal STM32 firmware. Each acts as an I2C
  slave exposing a key/value telemetry table (`VTable`) to the OBC, and as
  a UART slave that a PC simulator drives to set that table's values.
- **Ground station** (`ground_station/`) — interactive Python CLI over a
  serial port; issues commands and decodes OBC responses.
- **Simulators** (`simulators/`, `payload_sim.py`, `eps_sim.py`) — PC
  programs that model sensor behavior and push values into an EPS/Payload
  board's VTable over UART, so the OBC's I2C polling has real values to
  read without the target sensors being present.
- **`common/`** — headers and pure-C modules shared by all three firmware
  targets: wire-format structs, CRC-16/CRC-32 routines, the VTable
  implementation, and the on-disk log record format.

## Repository layout

```text
common/             Shared protocol headers, CRC routines, VTable module
obc/                OBC firmware (STM32CubeIDE project, FreeRTOS)
eps/                EPS firmware (STM32CubeIDE project, bare-metal)
payload/             Payload firmware (STM32CubeIDE project, bare-metal)
ground_station/     Python ground station CLI
simulators/         Shared PC simulator engine (models, protocol, transport)
payload_sim.py      Payload simulator entry point
eps_sim.py          EPS simulator entry point
tests/              Python unit tests and C++ unit tests for common/ modules
```

Each firmware directory (`obc/`, `eps/`, `payload/`) is a self-contained
STM32CubeIDE project generated from its own `.ioc` file, with CubeMX-generated
`Core/`, `Drivers/` and linker scripts alongside hand-written `Communication/`,
`Power/`, and `Storage/` modules.

## Communication protocols

### Ground-station UART link

The OBC and ground station exchange fixed-format, CRC-32-protected frames
over the NUCLEO ST-LINK virtual COM port at 115200 baud, 8N1.

```text
UartFrameHeader_t (11 bytes) | payload (0..64 bytes) | crc32 (4 bytes)
```

`UartFrameHeader_t` carries a start marker (`0xA55A`), a message type, a
sequence number, a millisecond timestamp, and the payload length. The CRC-32
(`Protocol_Crc32`, CRC-32/ISO-HDLC) covers the header and payload.

Message types (`common/uart/uart_protocol.h`):

| Type | ID | Direction | Purpose |
|---|---|---|---|
| `STATUS` | `0x01` | GS → OBC → GS | Request/return OBC + I2C health |
| `PAYLOAD` | `0x02` | GS → OBC → GS | Request/return latest cached payload snapshot |
| `BATTERY` | `0x03` | GS → OBC → GS | Request/return cached battery percentage |
| `AUTO_STATUS` | `0x04` | OBC → GS | Unsolicited periodic status report |
| `SET_TIME` | `0x05` | GS → OBC | Set the OBC's RTC from a Unix epoch |
| `FETCH` | `0x06` | GS → OBC | Request stored records in a time range |
| `FETCH_DATA` | `0x07` | OBC → GS | One stored record from a `FETCH` |
| `FETCH_END` | `0x08` | OBC → GS | Terminates a fetch stream, reports record/probe counts |
| `SIM_SET` / `SIM_GET` / `SIM_LIST` / `SIM_ACK` | `0x40`-`0x43` | simulator ↔ EPS/Payload | VTable maintenance (see below) |
| `DEBUG_TEXT` | `0x70` | OBC → GS | Free-form firmware log text, CRC-protected |
| `ERROR` | `0xFF` | OBC → GS | Malformed/unknown request response |

`STATUS` and `AUTO_STATUS` share `UartStatusPayload_t`: power state, battery
percentage and validity, Payload/EPS online flags, SD logger state, dropped
frame/overrun counters, I2C and CRC failure counters per node, and SD error
count. `PAYLOAD` and `BATTERY` share the legacy `UartPayload_t` layout.

Every struct on the wire is `__attribute__((packed))` and its size is
enforced at compile time with `static_assert`, so a firmware and
ground-station build that disagree on layout fail to compile rather than
silently miscommunicate.

### Fetch protocol (SD-backed history)

`FETCH` requests an inclusive `[from_epoch_s, to_epoch_s]` range from one of
two on-SD volumes (`0` = payload telemetry, `1` = housekeeping) with an
optional record cap. The OBC's SD-logger task locates the first matching
record with a binary search over the telemetry file, then streams matches
back as a sequence of `FETCH_DATA` frames, followed by one `FETCH_END` frame
carrying the returned record count and the binary-search probe count. The
ground station's `fetch <from> <to> payload|hk` command drives this; `--boot`
filters the housekeeping stream down to boot-counter and reset-cause records.

### I2C register protocol (OBC ↔ EPS/Payload)

The OBC is the I2C master; the EPS and Payload are addressed slaves
(`0x20` and `0x21` respectively, `common/i2c/bus_config.h`). Node identity on
the bus is separate from the logical node ID carried in message payloads
(`PAYLOAD_NODE_ID = 0x02`, `EPS_NODE_ID = 0x03`).

Reads are register-addressed: the master writes one register number, then
reads a fixed-size response for that register.

| Register | Access | Purpose |
|---|---|---|
| `REG_WHOAMI` (`0x00`) | read 1 byte | Logical node ID |
| `REG_VT_COUNT` (`0x30`) | read 2 bytes | Live VTable entry count |
| `REG_VT_SELECT` (`0x31`) | write 8 bytes | Select an entry by key name |
| `REG_VT_VALUE` (`0x32`) | read 12 bytes | Value of the selected key (`VtValueWire_t`) |
| `REG_VT_AT` (`0x33`) | write 2 bytes | Select an entry by dense index |
| `REG_VT_ENTRY` (`0x34`) | read 20 bytes | Full entry at the selected index (`VtEntryWire_t`) |

This lets the OBC poll individual sensor keys on a per-key schedule rather
than reading one large fixed struct, and lets it discover the complete set
of keys a node currently publishes by iterating `REG_VT_AT`/`REG_VT_ENTRY`
over `[0, REG_VT_COUNT)`. Both wire structs end in a CRC-16/CCITT-FALSE
(`Protocol_Crc16`) covering the preceding bytes.

### VTable (key/value telemetry store)

`common/vtable/vtable.c` implements a fixed-capacity (48-entry), allocation-free
key/value table shared by the EPS and Payload firmware. Each entry holds an
8-byte ASCII key, a type tag (`U32`/`I32`/`F32`/`BYTES`), up to 8 bytes of
value, a last-updated timestamp, and in-use/fresh flags. Entries are created
or updated by a PC simulator over UART (`SIM_SET`) and read back by the OBC
over I2C by key (`REG_VT_SELECT`/`REG_VT_VALUE`) or by index for discovery.
Entries are never deleted, so no tombstone handling is needed — a key lives
until the node reboots. `VTable_HashName` (FNV-1a) gives a stable short ID
for a key name, used when persisting sensor identity to the SD log.

The simulator-facing UART messages `SIM_SET`/`SIM_GET`/`SIM_LIST`/`SIM_ACK`
let a PC simulator create, update, look up, or enumerate VTable entries on
a running EPS or Payload board without a firmware rebuild.

### On-disk telemetry format

`common/log_record.h` defines the fixed 16-byte record the OBC writes to the
SD card: epoch timestamp, sensor ID, record type (telemetry, boot, event, or
survival), a length-prefixed 4-byte value, and a CRC-32 over the first 12
bytes. Thirty-two records fill one 512-byte FatFs sector exactly. Two
reserved sensor IDs route boot-counter and reset-cause records to the
housekeeping directory: `SENSOR_ID_BOOT` (`0x03FF`) and
`SENSOR_ID_RESET_CAUSE` (`0x03FE`), the latter carrying the STM32 reset-flag
mask, which the ground station renders as readable names
(`IWDG`, `RESET_PIN`, `SOFTWARE`, `POWER/BROWNOUT`).

## OBC firmware

The OBC runs FreeRTOS with five tasks, a hardware IWDG watchdog, and a
software task-liveness supervisor:

| Task | Priority | Responsibility |
|---|---|---|
| `Task_Supervisor` | High | Every 2 s, refreshes the IWDG only if all four worker tasks checked in since the last window |
| `Task_PowerMgmt` | Above Normal | Samples EPS battery percentage once per second, updates the power-state state machine |
| `Task_PayloadCol` | Normal | Polls EPS/Payload VTable keys over I2C on a schedule, updates the ground-station snapshot, queues records for SD logging |
| `Task_GroundComm` | Normal | Drains the UART RX buffer, answers ground-station requests, sends scheduled `AUTO_STATUS` frames |
| `Task_SD_Logger` | Below Normal | Owns all FatFs access: batched writes, file rotation, retention, and `FETCH` reads |

`Task_Supervisor` refreshes the ~32-second hardware IWDG only when
`task_watch_all_alive_and_clear()` reports that every task bit
(`TASK_WATCH_COLLECTOR`, `TASK_WATCH_SD_LOGGER`, `TASK_WATCH_POWER_MGMT`,
`TASK_WATCH_GROUND_COMM`) has been set since the previous 2-second window;
a missing bit is left set so a delayed task still has the remainder of the
32-second hardware interval to check in before a reset occurs.

### Power state machine

`obc/Power/power_state.cpp` smooths the incoming battery-percentage samples
with an exponential moving average (division factor 4 or 8, depending on
current state) and drives a three-state machine — `CRITICAL` (<20%),
`NORMAL`, `FULL` (≥80%) — with a 5-point hysteresis band so a battery
sitting on a threshold cannot oscillate between states.

### Schedule

`obc/Power/schedule.cpp` holds a period-in-milliseconds table indexed by
job (`AUTO_STATUS`, each polled sensor key, `SD_FLUSH`) and by the current
power state. A period of zero disables that job entirely in that state —
in `CRITICAL`, the two solar-panel jobs are disabled outright rather than
merely slowed, and every other job runs at its slowest cadence. `FULL`
uses the fastest cadences. `Schedule_TryTakeDue` advances each job's next
deadline from the deadline itself, not from the call time, so a late poll
does not shift the whole cadence forward, and a job that falls more than one
period behind resynchronizes instead of firing repeatedly to catch up.

### Storage

`obc/Storage/TelemetryFileStore.cpp` owns file creation, batched writes with
periodic sync, 1 MiB size-based rotation, retention of older files, and
crash/reboot recovery of in-progress session state. `SessionStore.cpp`
persists a CRC-protected `SessionMetadata_t` (generation, session ID, active
and next file index, last commit time) to a double-buffered `SESSION.BIN` so
a mid-write power loss can be recovered on the next boot without losing the
active file. `FETCH` requests are served by a binary search over the
telemetry file for the first record at or after `from_epoch_s`, then
sequential reads until `to_epoch_s` or the record cap is reached.

### Collector and snapshot

`obc/Communication/PayloadCollector.cpp` polls EPS and Payload VTable keys
according to the schedule, maintains a mutex-protected in-RAM snapshot per
node (`SnapshotData_t`, CRC-32 protected) for the ground-station `PAYLOAD`
and `BATTERY` commands, queues due readings as `LogRecord_t` entries for
`Task_SD_Logger`, and tracks per-node online state and I2C/CRC error counts
that feed the `STATUS`/`AUTO_STATUS` frames.

## EPS and Payload firmware

Both are bare-metal STM32 projects (no RTOS) built around the same pattern:

- An I2C slave (`I2CSlave.cpp`/`.hpp`) implements the register file protocol
  described above over interrupt-driven HAL I2C, backed by the shared
  `VTable` module.
- A UART interface (`EpsUart.cpp`/`PayloadUart.cpp`) receives `SIM_SET`,
  `SIM_GET`, and `SIM_LIST` frames from the PC simulator, applies them to the
  VTable, and replies with `SIM_ACK`. Reception is interrupt-driven; framing
  and CRC validation happen in the main loop.
- Both boards run at the same UART framing and CRC as the ground-station
  link, using the shared `common/uart/` headers, so the same
  `UartReceivedFrame_t` parser and `Protocol_Crc32` implementation serve all
  three firmware targets.

The Payload's standard VTable keys are `TEMP`, `TDOSE`, `SEL`, and `NRESET`.
The EPS publishes `VBAT`, `TEMP`, and, in solar mode, `SP0_T..SP5_T` /
`SP0_I..SP5_I`.

## Ground station

`ground_station/ground_station.py` is an interactive CLI that opens the
NUCLEO virtual COM port and exchanges the framed UART protocol described
above. Available commands: `status`, `payload`, `battery`,
`fetch <from> <to> payload|hk [--boot]`, `help`, `quit`. It runs a continuous
receive loop so unsolicited `AUTO_STATUS` frames and `DEBUG_TEXT` messages
(`[OBC] ...`) are displayed as they arrive, independent of command/response
traffic. `ground_station/plotting.py` and `ground_station/sensors.py`
provide supporting data handling. `ground_station/protocol.py` implements
frame encoding/decoding shared between the CLI and its test suite.

```powershell
cd ground_station
python -m pip install -r requirements.txt
python ground_station.py --list-ports
python ground_station.py --port COM5
```

## PC simulators

`simulators/` is a shared engine (`models.py` for value generation,
`protocol.py` for UART message construction, `transport.py` for the serial
connection, `runtime.py` for the interactive loop) driven by two entry
points, `payload_sim.py` and `eps_sim.py`. Both use the same `0xA55A`-framed,
CRC-32-protected UART envelope as the ground-station link and default to a
dry-run mode that requires no hardware or `pyserial`.

```powershell
python payload_sim.py --sensor radiation --steps 3
python payload_sim.py --sensor temperature,sel --port COM5
python eps_sim.py --capacity-ah 2.6 --soc 80 --load-a 0.4 --solar-a 0.1 --port COM6
```

The Payload simulator models `temperature`, `radiation`, `sel`, and `reset`
sensor groups. The EPS simulator models `battery` (via coulomb counting),
`temperature`, and `solar` groups, and supports a `--time-scale` factor so
long-duration capacity/drain behavior can be tested without running in real
time. Both simulators expose an interactive console (`get <key>`, `list`,
plus sensor-specific commands) when run without `--steps`.

## Building and testing

The three firmware targets are STM32CubeIDE projects; open `obc/`, `eps/`,
or `payload/` in STM32CubeIDE (or build with `arm-none-eabi-gcc` via the
generated `Debug/makefile`) and flash to a NUCLEO-L476RG board.

Python unit tests (ground station, simulators) require no hardware:

```powershell
python -m unittest discover -s tests -t . -p "test_*.py" -v
```

C++ unit tests for shared `common/` modules (CRC-16, VTable) live under
`tests/common/` and `tests/payload/` and are built as host-side (non-STM32)
targets independent of the firmware toolchain.

## CubeMX regeneration (OBC)

The OBC firmware keeps its FatFs stack under `obc/Storage/` rather than the
default CubeMX-generated location. Because CubeMX always regenerates FatFs
into `obc/FATFS/` and `obc/Middlewares/Third_Party/FatFs/`, every code
regeneration requires a fixed manual cleanup: copying the regenerated
`ffconf.h` into `obc/Storage/FATFS/Target/` before deleting the duplicate
trees, removing the corresponding include paths from `obc/.cproject`,
clearing `obc/Debug/` and re-running Project > Clean, and running
`obc/check_fatfs_layout.sh` as a guard before flashing. The guard checks for
the duplicate FatFs tree, SPI wiring reverting to a stub, SPI1 timing
settings reverting to HAL defaults, an LED init reappearing on the SPI1_SCK
pin, the FreeRTOS entry point losing its call to `ObcController_Process()`,
and `ffconf.h` drifting out of sync with `obc.ioc`.
