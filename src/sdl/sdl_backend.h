// sdl_backend.h - interface between the pico-lutionX SDL 1.2 subset and a platform.
//
// sdlb_* functions are implemented by the platform (hosttest/dx_host.cpp on Linux, the
// board's platform layer on the RP2350). sdlp_* functions are implemented by the SDL
// subset and called by the platform from sdlb_poll() to report input.
#pragma once

#include "SDL.h"

#ifdef __cplusplus
extern "C" {
#endif

// ---- Implemented by the platform -----------------------------------------------------

// Milliseconds since start-up (monotonic).
Uint32 sdlb_ticks(void);
// Wait for the given number of milliseconds (the host harness advances a virtual clock).
void sdlb_delay(Uint32 ms);
// Allocate the 8 bpp pixels of the screen surface; returns NULL on failure. On the board
// this is the back buffer in PSRAM; SDL_Flip hands it to sdlb_present().
void *sdlb_screen_alloc(int width, int height, int *pitch);
void sdlb_screen_free(void *pixels);
// Show the screen surface (8 bpp, colours from the last sdlb_set_palette()). The board
// copies it in the background: the surface must not be drawn into until
// sdlb_screen_wait() returns.
void sdlb_present(const Uint8 *pixels, int pitch, int width, int height);
// Wait until the screen surface may be drawn into again (returns at once where
// sdlb_present copies before it returns).
void sdlb_screen_wait(void);
// Set entries of the display palette.
void sdlb_set_palette(const SDL_Color *colors, int first, int count);
// Sample the input devices and report changes through the sdlp_* functions.
void sdlb_poll(void);

// ---- Implemented by the SDL subset, called by the platform ---------------------------

// A key went down or up. `unicode` is the character it produces (0 if none); it is only
// delivered to the engine while SDL_EnableUNICODE(1) is active.
void sdlp_key(int down, SDLKey sym, Uint16 unicode);
// Relative mouse movement; the subset keeps the absolute cursor inside the screen.
void sdlp_mouse_motion(int dx, int dy);
// Absolute mouse position (e.g. scripted input in the host harness).
void sdlp_mouse_position(int x, int y);
// A mouse button (SDL_BUTTON_*) went down or up. Wheel steps are buttons 4 and 5.
void sdlp_mouse_button(int down, int button);
// Declare joystick 0 (the merged gamepads); `present` 0 removes it.
void sdlp_joystick_config(int present, const char *name, int buttons, int hats, int axes);
void sdlp_joystick_button(int button, int down);
void sdlp_joystick_hat(Uint8 value);
void sdlp_joystick_axis(int axis, Sint16 value);
// Ask the engine to quit.
void sdlp_quit(void);

#ifdef __cplusplus
}
#endif
