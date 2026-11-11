// dx_settings.h -- the port's own settings on the SD card (see dx_settings.c).

#ifndef DX_SETTINGS_H
#define DX_SETTINGS_H

#include <stdbool.h>

// Reads /settings_DIABLO.dat and decides the display mode to start in. Called by
// dx_fatfs_init right after the card is mounted, before the display starts.
void	dx_settings_load (void);

// The display mode to start in: true for DVI. The board default until loaded.
bool	dx_settings_boot_dvi (void);

// Stores the display mode picked in the menu (written at once if it changed).
void	dx_settings_save_display (bool dvi);

#endif
