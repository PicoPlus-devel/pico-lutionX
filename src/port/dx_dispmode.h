// dx_dispmode.h - the display mode of the HDMI connector, for DevilutionX's settings menu.
//
// The output runs in HDMI mode (with sound for the monitor) or DVI mode (no sound on
// the cable; the board's own audio output plays). The Graphics settings show it as
// "Display Mode" (OptionEntryDisplayMode in Source/options.cpp). Implemented by
// src/pico/dx_display.c on the board, and by a stub in the host harness.
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// 1 for DVI, 0 for HDMI.
int dx_display_mode_get(void);
// Switches the running picture and stores the choice on the SD card
// (/settings_DIABLO.dat, not diablo.ini: it is needed before the engine starts).
void dx_display_mode_set(int dvi);

#ifdef __cplusplus
}
#endif
