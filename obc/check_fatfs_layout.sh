#!/usr/bin/env bash
# Run this from obc/ immediately after EVERY CubeMX regeneration.
#
# CubeMX has already broken this project twice by regenerating over verified settings.
# Each failure below links cleanly and only shows up on real hardware, so grep for them
# instead of trusting a green build.
set -u
cd "$(dirname "$0")" 2>/dev/null || true
rc=0

# --- 1. FatFs layout -------------------------------------------------------
# CubeMX always emits FatFs into obc/FATFS/ and obc/Middlewares/Third_Party/FatFs/.
# This project keeps its FatFs stack under obc/Storage/, so a regen creates a
# duplicate tree -> ~45 "multiple definition" link errors.
if [ -d FATFS ]; then
  echo "FAIL: obc/FATFS/ was regenerated. Delete it - the real tree is Storage/FATFS/."
  rc=1
fi
if [ -d Middlewares/Third_Party/FatFs ]; then
  echo "FAIL: obc/Middlewares/Third_Party/FatFs/ was regenerated. Delete it"
  echo "      (keep Middlewares/Third_Party/FreeRTOS - that one IS used)."
  rc=1
fi
for p in '"\.\./FATFS/Target"' '"\.\./FATFS/App"' '"\.\./Middlewares/Third_Party/FatFs/src"'; do
  if grep -q "value=$p" .cproject 2>/dev/null; then
    echo "FAIL: .cproject re-added include path $p - remove it."
    rc=1
  fi
done
if grep -q 'kind="sourcePath" name="FATFS"' .cproject 2>/dev/null; then
  echo "FAIL: .cproject re-added the FATFS sourcePath entry - remove it."
  rc=1
fi

# --- 2. SD card driver wiring ---------------------------------------------
# CubeMX regenerates user_diskio.c as a stub whose USER_initialize returns
# STA_NOINIT unconditionally -> f_mount fails with FR_NOT_READY (FatFs error 3).
if ! grep -q 'USER_SPI_initialize' Storage/FATFS/Target/user_diskio.c 2>/dev/null; then
  echo "FAIL: Storage/FATFS/Target/user_diskio.c lost its SPI wiring (stub restored)."
  echo "      USER_initialize must 'return USER_SPI_initialize(pdrv);'"
  rc=1
fi
if [ ! -f Storage/FATFS/Target/user_diskio_spi.c ]; then
  echo "FAIL: Storage/FATFS/Target/user_diskio_spi.c is missing."
  rc=1
fi

# --- 3. SPI1 settings ------------------------------------------------------
# A regen dropped DataSize and BaudRatePrescaler from SPI1.IPParameters in the .ioc,
# so the generated code fell back to HAL defaults: 4-bit frames at 40 MHz.
# SD cards need 8-bit frames, and card identification (CMD0..ACMD41) requires
# 100-400 kHz. 80 MHz / 256 = 312.5 kHz is the verified value.
if ! grep -q 'hspi1.Init.DataSize = SPI_DATASIZE_8BIT;' Core/Src/spi.c 2>/dev/null; then
  echo "FAIL: SPI1 DataSize is not 8BIT. SD cards require 8-bit frames."
  rc=1
fi
if ! grep -q 'hspi1.Init.BaudRatePrescaler = SPI_BAUDRATEPRESCALER_256;' Core/Src/spi.c 2>/dev/null; then
  echo "FAIL: SPI1 init prescaler is not 256 (312.5 kHz). SD init needs 100-400 kHz."
  rc=1
fi
if ! grep -q '^SPI1.IPParameters=.*DataSize.*BaudRatePrescaler' obc.ioc 2>/dev/null; then
  echo "FAIL: obc.ioc SPI1.IPParameters lost DataSize/BaudRatePrescaler."
  echo "      Without them CubeMX regenerates HAL defaults (4-bit @ 40 MHz)."
  rc=1
fi

# --- 4. PA5 / LD2 / SPI1_SCK conflict --------------------------------------
# LD2 is PA5, which is also SPI1_SCK. BSP_LED_Init reconfigures PA5 as a GPIO
# output and severs the SD clock. CubeMX re-adds this block when the BSP is on.
# Match only a real call statement, so prose/commented-out lines don't trip it.
if grep -qE '^[[:space:]]*BSP_LED_Init[[:space:]]*\(' Core/Src/main.c 2>/dev/null; then
  echo "FAIL: BSP_LED_Init re-added to OBC main.c. PA5 is SPI1_SCK - remove it."
  rc=1
fi

# --- 5. RTOS entry point ---------------------------------------------------
# osKernelStart() never returns, so main()'s while(1) superloop is dead code.
# ObcController_Process() must be driven from a task or the OBC transmits its boot
# banner and then never answers a ground-station command (GS times out at 2 s).
# A regen that renames tasks can drop the USER CODE block holding this call.
if ! grep -qE '^[[:space:]]*ObcController_Process[[:space:]]*\(' Core/Src/freertos.c 2>/dev/null; then
  echo "FAIL: nothing calls ObcController_Process() in Core/Src/freertos.c."
  echo "      UART commands will silently stop working (TX still fine, RX unanswered)."
  rc=1
fi

[ "$rc" -eq 0 ] && echo "OK: FatFs layout, SD driver wiring, SPI1 settings, PA5 and RTOS entry point all intact."
exit $rc
