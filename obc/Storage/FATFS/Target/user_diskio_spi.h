#ifndef __USER_DISKIO_SPI_H
#define __USER_DISKIO_SPI_H

#ifdef __cplusplus
extern "C" {
#endif

#include "ff_gen_drv.h"

DSTATUS USER_SPI_initialize(BYTE pdrv);
DSTATUS USER_SPI_status(BYTE pdrv);
DRESULT USER_SPI_read(BYTE pdrv, BYTE *buff, DWORD sector, UINT count);
#if _USE_WRITE == 1
DRESULT USER_SPI_write(BYTE pdrv, const BYTE *buff, DWORD sector, UINT count);
#endif
#if _USE_IOCTL == 1
DRESULT USER_SPI_ioctl(BYTE pdrv, BYTE cmd, void *buff);
#endif

extern Diskio_drvTypeDef USER_SPI_Driver;

#ifdef __cplusplus
}
#endif

#endif /* __USER_DISKIO_SPI_H */
