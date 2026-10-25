// dx_fatal.h -- fatal errors on the HDMI output.

#ifndef DX_FATAL_H
#define DX_FATAL_H

#ifdef __cplusplus
extern "C" {
#endif

// Shows the message under the boot console's log and stops. The conditions that
// get here (no SD card, no PSRAM, no game data, an engine error) do not go away on
// a reset, so the screen stays up for the user to read.
void	dx_fatal (const char *fmt, ...) __attribute__((noreturn, format(printf, 1, 2)));
// Same, for an error whose text was already printed.
void	dx_fatal_halt (void) __attribute__((noreturn));

#ifdef __cplusplus
}
#endif

#endif
