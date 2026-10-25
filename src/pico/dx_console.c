// dx_console.c -- the boot console (see dx_console.h). From pico-quake2's q2_console.c,
// with each font pixel drawn as 2x2 for the 640x480 front buffer.

#include <string.h>

#include "pico/stdio.h"
#include "pico/stdio/driver.h"

#include "dx_console.h"
#include "FrensFonts.h"
#include "font_8x8.h"		// the pico_shared 8x8 font (FrensFonts.cpp is C++)

static unsigned font_slice (char c, int row)
{
	return (unsigned char)font_8x8[(c - FONT_FIRST_ASCII) + row * FONT_N_CHARS];
}

#define SCALE	2

static char		grid[DXCON_ROWS][DXCON_COLS];
static int		cur_row = 1, cur_col;		// row 0 is the title bar
static uint8_t	*surface;
static int		stride;
static const char	*title = "pico-lutionX";

static void paint_row (int row)
{
	int		col, y, x;
	uint8_t	bg = row ? DXCON_BG : DXCON_TITLE_BG;
	uint8_t	fg = row ? DXCON_FG : DXCON_TITLE_FG;

	if (!surface)
		return;
	for (col = 0; col < DXCON_COLS; col++)
	{
		char	c = grid[row][col];

		if (c < FONT_FIRST_ASCII || c >= FONT_FIRST_ASCII + FONT_N_CHARS)
			c = ' ';
		for (y = 0; y < FONT_CHAR_HEIGHT; y++)
		{
			unsigned	slice = font_slice (c, y);
			uint8_t		*px = surface + ((row * FONT_CHAR_HEIGHT + y) * SCALE) * stride
							+ col * FONT_CHAR_WIDTH * SCALE;

			for (x = 0; x < FONT_CHAR_WIDTH; x++, slice >>= 1)
			{
				uint8_t	v = (slice & 1) ? fg : bg;

				px[2 * x] = px[2 * x + 1] = v;
				px[stride + 2 * x] = px[stride + 2 * x + 1] = v;
			}
		}
	}
}

static void set_title_row (void)
{
	size_t	n = strlen (title);

	memset (grid[0], ' ', DXCON_COLS);
	if (n > DXCON_COLS)
		n = DXCON_COLS;
	memcpy (grid[0] + (DXCON_COLS - n) / 2, title, n);
}

static void scroll (void)
{
	int		r;

	memmove (grid[1], grid[2], (DXCON_ROWS - 2) * DXCON_COLS);
	memset (grid[DXCON_ROWS - 1], ' ', DXCON_COLS);
	for (r = 1; r < DXCON_ROWS; r++)
		paint_row (r);
}

void dx_console_write (const char *s, size_t len)
{
	int		dirty = -1;

	while (len--)
	{
		char	c = *s++;

		if (c == '\r')
		{
			cur_col = 0;
			continue;
		}
		if (c == '\n' || cur_col == DXCON_COLS)
		{
			if (dirty >= 0)
				paint_row (dirty);
			dirty = -1;
			cur_col = 0;
			if (++cur_row == DXCON_ROWS)
			{
				cur_row = DXCON_ROWS - 1;
				scroll ();
			}
			if (c == '\n')
				continue;
		}
		if (c == '\t')
			c = ' ';
		grid[cur_row][cur_col++] = c;
		dirty = cur_row;
	}
	if (dirty >= 0)
		paint_row (dirty);
}

static void stdio_out (const char *buf, int len)
{
	dx_console_write (buf, (size_t)len);
}

static stdio_driver_t console_driver = { .out_chars = stdio_out };

void dx_console_attach_stdio (void)
{
	memset (grid, ' ', sizeof(grid));
	set_title_row ();
	stdio_set_driver_enabled (&console_driver, true);
}

void dx_console_show (uint8_t *fb, int fb_stride)
{
	int		r;

	surface = fb;
	stride = fb_stride;
	for (r = 0; r < DXCON_ROWS; r++)
		paint_row (r);
}

void dx_console_stop (void)
{
	surface = NULL;
}

bool dx_console_active (void)
{
	return surface != NULL;
}

void dx_console_set_title (const char *t)
{
	title = t;
	set_title_row ();
	paint_row (0);
}
