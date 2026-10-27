// sd_host.h -- SD card mode of the harness (sd_host.c).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Puts the engine's files on the card image from here on. 0 = OK.
int dx_sdcard_attach(const char *image);
// +1 / -1 around the harness's own files (frame dumps, scripts, flash region): on the host.
void dx_sdcard_host_files(int delta);

#ifdef __cplusplus
}
#endif
