/* Public interface for the OBC startup SD-card check. */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/* Mounts the SD card and writes HELLO.TXT once during OBC startup. */
void SdCard_RunStartupTest(void);

#ifdef __cplusplus
}
#endif
