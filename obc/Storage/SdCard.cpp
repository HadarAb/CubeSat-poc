/* Runs the OBC startup SD-card mount and file-write diagnostic. */
#include "SdCard.hpp"

#include "UartProtocol.hpp"
#include "fatfs.h"

/* Writes HELLO.TXT and reports each result through framed UART debug messages. */
extern "C" void SdCard_RunStartupTest(void)
{
    FIL file = {};
    UINT bytes_written = 0u;
    static const char file_text[] = "cubesat\r\n";

    SendUartMsg("SD test starting");

    if (f_mount(&USERFatFS, USERPath, 1u) != FR_OK)
    {
        SendUartMsg("SD mount failed");
        return;
    }

    if (f_open(&file, "HELLO.TXT", FA_CREATE_ALWAYS | FA_WRITE) != FR_OK)
    {
        SendUartMsg("SD open failed");
        return;
    }

    const FRESULT write_result = f_write(&file, file_text, sizeof(file_text) - 1u, &bytes_written);
    const FRESULT close_result = f_close(&file);

    if ((write_result != FR_OK)
        || (bytes_written != (sizeof(file_text) - 1u))
        || (close_result != FR_OK))
    {
        SendUartMsg("SD write failed");
        return;
    }

    SendUartMsg("SD write OK");
}
