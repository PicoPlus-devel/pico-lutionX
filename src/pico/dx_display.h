// dx_display.h -- HSTX video output: the 640x480 8 bpp front buffer (see dx_display.c).

#ifndef DX_DISPLAY_H
#define DX_DISPLAY_H

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Brings up HSTX 640x480 on core1 with the boot console on screen. Idempotent.
void dx_display_init(void);
// Hands the screen back to the boot console (fatal errors).
void dx_display_show_console(void);
// HSTX frames scanned out since init (60 per second).
uint32_t dx_display_frames(void);
// Hands an 8 bpp frame (the engine's back buffer) to core1, which copies it to the screen
// from the next vsync; returns at once. The frame must not be drawn into again before
// dx_display_wait_copy returns.
void dx_display_present(const uint8_t *pixels, int pitch, int width, int height);
// Waits until core1 has copied the last presented frame (running the idle task meanwhile).
void dx_display_wait_copy(void);
// Core1's background task after the frame copy (dx_audio.c's audio pump).
void dx_display_set_core1_task(void (*task)(void));
// Duration of the last frame copy (with DX_COPY_ON_CORE1: on core1, from its vsync), and the
// longest since the last call.
void dx_display_copy_us(uint32_t *last, uint32_t *max);
// Sets palette entries; rgbx holds count entries of r, g, b, unused (SDL_Color).
// Takes effect at the next vsync.
void dx_display_set_palette(int first, int count, const uint8_t *rgbx);
// A short task that dx_display_present runs while it waits for the vertical blank (each
// call must take much less than a frame: the copy may start that much later); it
// returns 0 when it has nothing to do.
void dx_display_set_idle(int (*task)(void));
// Deepest use of core1's 4 KB stack so far, in bytes.
unsigned dx_display_core1_stack_used(void);

#ifdef __cplusplus
}
#endif

#endif
