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

## CubeMX regeneration ritual (OBC)

Regenerate only at the agreed sync, with both developers present, and commit the
result on its own with a message like `cubemx: regen (added RTC)`.

CubeMX always writes FatFs into `obc/FATFS/` and `obc/Middlewares/Third_Party/FatFs/`,
but this project keeps its FatFs stack under `obc/Storage/`. Every regeneration
therefore creates a duplicate tree that has to be removed by hand. Follow these
steps in order after every Generate Code.

1. Copy the regenerated FatFs configuration across, before deleting anything:

       cp obc/FATFS/Target/ffconf.h obc/Storage/FATFS/Target/ffconf.h

   This step is easy to miss and fails silently. `ffconf.h` is the only place a
   FATFS setting changed in CubeMX (`_VOLUMES`, `_MULTI_PARTITION`, ...) actually
   lands, and it lands in the root tree that step 2 deletes. Skip this and the
   build keeps using the old settings with no error anywhere.

2. Delete the duplicate trees `obc/FATFS/` and `obc/Middlewares/Third_Party/FatFs/`.
   Keep `obc/Middlewares/Third_Party/FreeRTOS/` - that one is used.

3. In `obc/.cproject`, remove the include paths `../FATFS/Target`, `../FATFS/App`
   and `../Middlewares/Third_Party/FatFs/src` from every configuration block, and
   remove the `FATFS` sourcePath entry if the IDE has not already dropped it.

4. Delete `obc/Debug/` and run Project > Clean. The generated makefiles still
   reference the files removed in step 2.

5. Run the layout guard and do not continue until it is green:

       bash obc/check_fatfs_layout.sh

6. Flash and confirm `SD write OK` over UART before starting any other work.

The guard in step 5 checks the things a regeneration has silently broken before:
the duplicate FatFs tree, the `user_diskio.c` SPI wiring being replaced by a stub,
SPI1 `DataSize`/`BaudRatePrescaler` reverting to HAL defaults, `BSP_LED_Init`
reappearing on PA5 (which is also SPI1_SCK), the RTOS entry point losing its call
to `ObcController_Process()`, and `ffconf.h` drifting out of sync with `obc.ioc`.


