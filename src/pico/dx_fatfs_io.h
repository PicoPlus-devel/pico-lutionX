// dx_fatfs_io.h -- SD card and file I/O (see dx_fatfs_io.c).

#ifndef DX_FATFS_IO_H
#define DX_FATFS_IO_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Mounts the card, changes to DX_DATA_DIR and checks for the game data; fatal (on
// screen) if anything is missing.
void dx_fatfs_init(void);
// Identity of the game data found by dx_fatfs_init (for the flash asset cache).
uint32_t dx_fatfs_data_id(void);

#ifdef __cplusplus
}
#endif

#endif
