// dx_settings.c -- the port's own settings, in /settings_DIABLO.dat on the SD card.
//
// So far only the display mode (HDMI or DVI). Everything else the engine keeps in
// diablo.ini, which it reads long after the display has started; the display
// mode is needed before that, so it has this file, which
// dx_fatfs_init reads right after mounting the card.
//
// The format follows pico-doom's /settings_DOOM.dat (and pico_shared's
// /settings_<TAG>.dat): a fixed-size record with a leading version, written raw
// and accepted on read only with the exact size and version. Anything else
// leaves the defaults.

#include <stdio.h>
#include <string.h>

#include "ff.h"

#include "pico_bootcfg.h"
#include "dx_settings.h"

#define DX_SETTINGS_PATH	"/settings_DIABLO.dat"
#define DX_SETTINGS_VERSION	1

// On-disk layout: fixed-width types only.
typedef struct
{
	uint16_t	version;
	int8_t		display;		// PICO_DISPLAY_*: 0 board default, 1 HDMI, 2 DVI
	int8_t		reserved[13];	// zeroed; bump the version before using any
} dx_settings_t;

_Static_assert (sizeof(dx_settings_t) == 16, "the settings record is an on-disk format");

static FIL				fil;		// also lends pico_bootcfg_display its FIL
static dx_settings_t	stored;		// the record as it is in the file
static bool				boot_dvi = PICO_BOARD_DEFAULT_DVI;

void dx_settings_load (void)
{
	dx_settings_t	rec;
	UINT			got = 0;
	FRESULT			fr;

	memset (&stored, 0, sizeof(stored));
	stored.version = DX_SETTINGS_VERSION;

	fr = f_open (&fil, DX_SETTINGS_PATH, FA_READ);
	if (fr == FR_OK)
	{
		if (f_size (&fil) == sizeof(rec))
			fr = f_read (&fil, &rec, sizeof(rec), &got);
		f_close (&fil);
		if (fr == FR_OK && got == sizeof(rec) && rec.version == DX_SETTINGS_VERSION)
		{
			stored = rec;
			if (stored.display < PICO_DISPLAY_DEFAULT || stored.display > PICO_DISPLAY_DVI)
				stored.display = PICO_DISPLAY_DEFAULT;
		}
		else
			printf ("dx_settings: " DX_SETTINGS_PATH " is not a version %d record, using defaults\n",
				DX_SETTINGS_VERSION);
	}

	boot_dvi = pico_bootcfg_boot_dvi (stored.display, &fil);
	printf ("dx_settings: display %s (stored %d, board default %s)\n", boot_dvi ? "DVI" : "HDMI",
		stored.display, PICO_BOARD_DEFAULT_DVI ? "DVI" : "HDMI");
}

bool dx_settings_boot_dvi (void)
{
	return boot_dvi;
}

void dx_settings_save_display (bool dvi)
{
	dx_settings_t	rec = stored;
	UINT			put = 0;
	FRESULT			fr;

	rec.display = pico_bootcfg_stored (dvi);
	if (rec.display == stored.display)
		return;
	fr = f_open (&fil, DX_SETTINGS_PATH, FA_WRITE | FA_CREATE_ALWAYS);
	if (fr == FR_OK)
	{
		fr = f_write (&fil, &rec, sizeof(rec), &put);
		if (f_close (&fil) != FR_OK && fr == FR_OK)
			fr = FR_DISK_ERR;
	}
	// Taken over either way: a write-protected card fails once, not on every change.
	stored = rec;
	if (fr != FR_OK || put != sizeof(rec))
		printf ("dx_settings: could not write " DX_SETTINGS_PATH " (FatFs error %d)\n", fr);
	else
		printf ("dx_settings: display %s saved\n", dvi ? "DVI" : "HDMI");
}
