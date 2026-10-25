// dx_display.c -- HSTX video output: the 640x480 8 bpp front buffer.
//
// The engine renders 640x480 8 bpp into its back buffer in PSRAM (the SDL screen
// surface, see dx_backend.cpp). Scanout never touches the QMI bus (flash and PSRAM),
// which keeps the picture stable while flash is written and the engine streams from PSRAM.
//
// The frame copy: by default core0 waits for the vsync (mixing audio meanwhile) and copies
// the back buffer itself. With DX_COPY_ON_CORE1, core1 copies it while core0 goes on with
// input, game logic and audio. On HW_CONFIG 2 that gained nothing: both cores share the one
// QSPI bus to flash and PSRAM, so the copy took 9-17 ms instead of 6 and slowed core0 by
// as much as it saved (and above about 15 ms it falls behind the beam). With it:
//   - dx_display_present hands the back buffer over (COPY_PENDING) and returns;
//   - core1's vsync callback starts the copy (COPY_RUNNING), and its background task copies
//     COPY_ROWS rows per call between scanline interrupts and the audio pump, top to bottom
//     and faster than the beam (640 bytes per 31.75 us line), so it does not tear;
//   - before the engine draws into the back buffer again it waits for COPY_IDLE
//     (dx_display_wait_copy, through sdlb_screen_wait), and so do flash writes, which
//     take PSRAM off the bus.
// If core1 does not take a frame within COPY_TIMEOUT_US (video stopped), core0 copies it.
//
// Core1's scanline callback (in SRAM, inside the DMA interrupt) expands each row through
// a 256-entry RGB555 table, two pixels per 32-bit word. Palette changes are latched at
// vsync. Structure from pico-quake2's q2_display.c.

#include <stdio.h>
#include <string.h>

#include "pico/stdlib.h"
#include "pico/multicore.h"
#include "hardware/clocks.h"
#include "hardware/sync.h"

#include "video_output.h"
#include "hstx_data_island_queue.h"

#include "dx_console.h"
#include "dx_display.h"
#include "dx_prof.h"

#define FB_W 640
#define FB_H 480

#ifndef DX_COPY_ON_CORE1
#define DX_COPY_ON_CORE1 0
#endif

static uint8_t fb[FB_W * FB_H] __attribute__((aligned(4)));
static uint16_t lut[2][256]; // RGB555, the one on screen and the next
static uint16_t engine_palette[256]; // the engine's palette, all entries
static uint32_t __scratch_y("dx_core1_stack") core1_stack[1024]; // 4 KB SCRATCH_Y bank
#define CORE1_STACK_PAINT 0x5a5a5a5au

static volatile int lut_front;
static volatile int pending_lut = -1;
static int engine_lut = -1; // engine palette held back while the boot console shows
static volatile uint32_t frames;
static bool video_up;
static int (*idle_task)(void);
static void (*core1_task)(void);

enum { COPY_IDLE, COPY_PENDING, COPY_RUNNING, COPY_CORE0 };
#define COPY_ROWS 8
#define COPY_TIMEOUT_US 100000
static volatile uint32_t copy_state = COPY_IDLE;
static const uint8_t *volatile copy_src;
static volatile int copy_pitch, copy_width, copy_rows, copy_row;
static volatile uint32_t copy_start_us, copy_last_us, copy_max_us;

static bool copy_take(uint32_t to)
{
	uint32_t expected = COPY_PENDING;
	return __atomic_compare_exchange_n(&copy_state, &expected, to, false, __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
}

// memcpy is in SRAM. A copy that loads 8 words at a time (LDM) measured the same 6.2 ms:
// the PSRAM read is the limit.
static void __not_in_flash_func(copy_rows_to)(int end)
{
	for (int y = copy_row; y < end; y++)
		memcpy(fb + y * FB_W, copy_src + y * copy_pitch, (size_t)copy_width);
	copy_row = end;
}

static void __not_in_flash_func(scanline_cb)(uint32_t v_scanline, uint32_t active_line, uint32_t *line_buffer)
{
	const uint8_t *src = fb + active_line * FB_W;
	const uint16_t *pal = lut[lut_front];

	(void)v_scanline;
	for (int i = 0; i < FB_W / 2; i += 4, src += 8) {
		line_buffer[i] = pal[src[0]] | ((uint32_t)pal[src[1]] << 16);
		line_buffer[i + 1] = pal[src[2]] | ((uint32_t)pal[src[3]] << 16);
		line_buffer[i + 2] = pal[src[4]] | ((uint32_t)pal[src[5]] << 16);
		line_buffer[i + 3] = pal[src[6]] | ((uint32_t)pal[src[7]] << 16);
	}
}

static void __not_in_flash_func(vsync_cb)(void)
{
	frames++;
	if (pending_lut >= 0) {
		lut_front = pending_lut;
		pending_lut = -1;
	}
	// the background task runs on this core, so it sees the row reset with the state
	if (copy_state == COPY_PENDING && copy_take(COPY_RUNNING)) {
		copy_row = 0;
		copy_start_us = time_us_32();
	}
}

// Core1's background task (between scanline interrupts): a slice of the frame copy, then
// the audio pump.
static void __not_in_flash_func(core1_background)(void)
{
	if (copy_state == COPY_RUNNING) {
		const int end = copy_row + COPY_ROWS < copy_rows ? copy_row + COPY_ROWS : copy_rows;
		copy_rows_to(end);
		if (end == copy_rows) {
			const uint32_t us = time_us_32() - copy_start_us;
			copy_last_us = us;
			if (us > copy_max_us)
				copy_max_us = us;
			__atomic_store_n(&copy_state, COPY_IDLE, __ATOMIC_RELEASE);
		}
	}
	if (core1_task != NULL)
		core1_task();
}

static uint16_t rgb555(unsigned r, unsigned g, unsigned b)
{
	return (uint16_t)(((r >> 3) << 10) | ((g >> 3) << 5) | (b >> 3));
}

// The palette the boot console paints with (DXCON_* indices).
static void load_console_palette(uint16_t *pal)
{
	memset(pal, 0, 256 * sizeof(uint16_t));
	pal[DXCON_BG] = rgb555(0, 0, 0);
	pal[DXCON_FG] = rgb555(255, 255, 255);
	pal[DXCON_TITLE_BG] = rgb555(110, 20, 10);
	pal[DXCON_TITLE_FG] = rgb555(255, 255, 255);
}

void dx_display_init(void)
{
	if (video_up)
		return;

	// clk_hstx is set to 126 MHz in dx_boot.c (25.2 MHz pixel clock).
	if (clock_get_hz(clk_hstx) != 126000000u)
		printf("dx_display: clk_hstx is %lu Hz, expected 126 MHz\n", (unsigned long)clock_get_hz(clk_hstx));

	memset(fb, DXCON_BG, sizeof(fb));
	load_console_palette(lut[0]);
	lut_front = 0;

	// HDMI mode, with data-island audio. The rate is set after video_output_init (which
	// sets 48 kHz) and before core1 starts, since it rebuilds the blanking lines.
	video_output_set_dvi_mode(false);
	hstx_di_queue_init();
	video_output_set_vsync_callback(vsync_cb);
	video_output_init(FB_W, FB_H);
	pico_hdmi_set_audio_sample_rate(44100);
	video_output_set_scanline_callback(scanline_cb);
	video_output_set_background_task(core1_background);
	for (unsigned i = 0; i < sizeof(core1_stack) / sizeof(core1_stack[0]); i++)
		core1_stack[i] = CORE1_STACK_PAINT; // for dx_display_core1_stack_used
	multicore_launch_core1_with_stack(video_output_core1_run, core1_stack, sizeof(core1_stack));
	video_up = true;

	dx_console_show(fb, FB_W);
	printf("dx_display: HSTX 640x480 up\n");
}

void dx_display_show_console(void)
{
	dx_display_init();
	dx_display_wait_copy();
	load_console_palette(lut[lut_front ^ 1]);
	pending_lut = lut_front ^ 1;
	dx_console_show(fb, FB_W);
}

uint32_t dx_display_frames(void)
{
	return frames;
}

void dx_display_set_idle(int (*task)(void))
{
	idle_task = task;
}

void dx_display_set_core1_task(void (*task)(void))
{
	core1_task = task;
}

void dx_display_copy_us(uint32_t *last, uint32_t *max)
{
	*last = copy_last_us;
	*max = copy_max_us;
	copy_max_us = 0;
}

void dx_display_wait_copy(void)
{
	if (copy_state == COPY_IDLE)
		return;

	const int prof = dx_prof_enter(DXP_VSYNC);
	const uint32_t t0 = time_us_32();

	while (__atomic_load_n(&copy_state, __ATOMIC_ACQUIRE) != COPY_IDLE) {
		if (time_us_32() - t0 > COPY_TIMEOUT_US && copy_take(COPY_CORE0)) {
			// core1 has not started the copy (no vsync): do it here
			static bool warned;
			if (!warned) {
				printf("dx_display: core1 did not copy the frame, copying on core0\n");
				warned = true;
			}
			dx_prof_enter(DXP_COPY);
			copy_row = 0;
			copy_rows_to(copy_rows);
			__atomic_store_n(&copy_state, COPY_IDLE, __ATOMIC_RELEASE);
			break;
		}
		if (idle_task == NULL || !idle_task())
			tight_loop_contents();
	}
	dx_prof_leave(prof);
}

unsigned dx_display_core1_stack_used(void)
{
	unsigned unused = 0;

	while (unused < sizeof(core1_stack) / sizeof(core1_stack[0]) && core1_stack[unused] == CORE1_STACK_PAINT)
		unused++;
	return (unsigned)sizeof(core1_stack) - unused * 4;
}

void dx_display_set_palette(int first, int count, const uint8_t *rgbx)
{
	int back = lut_front ^ 1;

	for (int i = 0; i < count; i++, rgbx += 4)
		if (first + i >= 0 && first + i < 256)
			engine_palette[first + i] = rgb555(rgbx[0], rgbx[1], rgbx[2]);
	memcpy(lut[back], engine_palette, sizeof(engine_palette));
	// The engine sets its palette while the boot console is still on screen: hold it
	// back until the engine presents its first frame.
	if (dx_console_active())
		engine_lut = back;
	else
		pending_lut = back;
}

void dx_display_present(const uint8_t *pixels, int pitch, int width, int height)
{
	if (dx_console_active()) {
		dx_console_stop(); // the engine has drawn a frame of its own
		if (engine_lut >= 0) {
			pending_lut = engine_lut;
			engine_lut = -1;
		}
	}
	if (width > FB_W)
		width = FB_W;
	if (height > FB_H)
		height = FB_H;

#if DX_COPY_ON_CORE1
	// the previous frame first; then core1 copies this one from the next vsync
	dx_display_wait_copy();
	copy_src = pixels;
	copy_pitch = pitch;
	copy_width = width;
	copy_rows = height;
	__atomic_store_n(&copy_state, COPY_PENDING, __ATOMIC_RELEASE);
#else
	// Start right after a vsync (bounded in case core1 has stopped; the idle task mixes
	// audio meanwhile), then stay ahead of the beam.
	const uint32_t f = frames;
	const int prof = dx_prof_enter(DXP_VSYNC);
	uint32_t t0 = time_us_32();
	while (frames == f && time_us_32() - t0 < 20000)
		if (idle_task == NULL || !idle_task())
			tight_loop_contents();
	dx_prof_enter(DXP_COPY);
	t0 = time_us_32();
	copy_src = pixels;
	copy_pitch = pitch;
	copy_width = width;
	copy_row = 0;
	copy_rows_to(height);
	const uint32_t us = time_us_32() - t0;
	copy_last_us = us;
	if (us > copy_max_us)
		copy_max_us = us;
	dx_prof_leave(prof);
#endif
}
