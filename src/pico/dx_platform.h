// dx_platform.h -- odds and ends of the pico-lutionX platform layer.

#ifndef DX_PLATFORM_H
#define DX_PLATFORM_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Where the game data lives on the SD card: spawn.mpq or DIABDAT.MPQ and the
// DevilutionX `assets` folder. Saves and diablo.ini are written there too.
#define DX_DATA_DIR "/roms/devilutionx"

// Frame counter and a status line on the UART every 5 seconds (dx_boot.c).
void dx_report_status(void);
// Time per frame of each dx_prof zone since the last call, the XIP cache hit rate and the
// busiest code addresses (dx_prof.c). dx_prof_start starts the program counter sampling.
void dx_prof_report(uint32_t frames);
void dx_prof_start(void);

#ifdef __cplusplus
}
#endif

#endif
