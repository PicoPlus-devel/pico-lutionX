// dx_console.h -- the boot console: 40x30 characters of stdout on the HDMI output
// (the 8x8 font at double size on the 640x480 front buffer).
//
// Text is captured from the first printf (as a pico_stdio driver) and painted into
// an 8bpp framebuffer with the palette indices below, until the engine draws its
// first frame. The grid keeps updating afterwards, so a fatal error can show the
// lines that led to it.

#ifndef DX_CONSOLE_H
#define DX_CONSOLE_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define DXCON_COLS		40
#define DXCON_ROWS		30

// palette indices the console paints with (dx_display.c loads matching colours)
#define DXCON_BG		0
#define DXCON_FG		1
#define DXCON_TITLE_BG	2
#define DXCON_TITLE_FG	3

void	dx_console_attach_stdio (void);
void	dx_console_write (const char *s, size_t len);
// paints the whole grid into fb (640x480) and keeps painting on writes
void	dx_console_show (uint8_t *fb, int stride);
void	dx_console_stop (void);		// the engine owns the screen from now on
bool	dx_console_active (void);
void	dx_console_set_title (const char *title);	// not copied

#ifdef __cplusplus
}
#endif

#endif
