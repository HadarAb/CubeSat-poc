From dev1-phase2:
UART frame protocol and CRC.
Python ground station.
OBC controller structure.
I2C communication.
Payload telemetry simulator.

From phase1-hadar:
STM32 SPI, UART and FatFs .
SD card SPI driver.
Hardware pin configuration.

what was changed : 
The large SD and I2C test code inside main.c , now it moved to OBC/Storage/SdCard.cpp/hpp
it makes main function clear and readable. 

ObcController:
Main OBC coordinator.
Polls the payload every 500 ms.
Handles ground-station commands.
Stores the latest valid payload.

UartProtocol
Receives and creates UART frames.
Checks frame start, length and CRC.
Sends command responses and debug messages.

Phase 4 PC simulators:
- `python payload_sim.py` drives Payload `TEMP`, `TDOSE`, `SEL`, and `NRESET` values.
- `python eps_sim.py` drives EPS battery, temperature, and solar-panel values.
- Both run without hardware in dry-run mode; see `simulators/README.md` for options.

