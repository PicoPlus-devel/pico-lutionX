// dx_flash.h -- flash access for the asset cache (see dx_flash.c).

#ifndef DX_FLASH_H
#define DX_FLASH_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Reads the flash chip's JEDEC ID. Call before the overclock and the PSRAM setup.
void dx_flash_probe(void);

// Hands the region DX_FCACHE_BASE..+DX_FCACHE_SIZE to the asset cache, cut to the size of
// the chip dx_flash_probe found. data_id identifies the game data (dx_fatfs_data_id).
void dx_flash_init(uint32_t data_id);

// Switches the board LED, which blinks while assets are written to flash, off again when
// the writes have stopped (called once per frame).
void dx_flash_idle(void);

#ifdef __cplusplus
}
#endif

#endif
