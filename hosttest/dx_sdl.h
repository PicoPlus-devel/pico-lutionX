// dx_sdl.h - the window, sound and input of dx_host_sdl (dx_sdl.cpp).
//
// No SDL types here: dx_host.cpp sees the port's SDL 1.2 subset, dx_sdl.cpp the real SDL2.
// Input reaches the engine through the firmware's src/pico/dx_input.cpp: dx_sdl.cpp
// implements the raw input of src/pico/dx_input.h (dx_input_pop and the others) in place
// of the board's USB host (src/pico/dx_usb.cpp).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Loads SDL2 and opens the window, the sound device and the game controllers. Returns
// false (with a message) when there is no SDL2 or no window; no sound is not an error.
bool dx_sdl_init(void);
// Shows an 8 bpp frame; rgbx holds 256 entries of r, g, b, unused (SDL_Color).
void dx_sdl_present(const uint8_t *pixels, int pitch, int width, int height, const uint8_t *rgbx);
// Collects the pending SDL events into the raw input queue.
void dx_sdl_pump(void);
// True once after the window has been closed.
bool dx_sdl_quit_requested(void);
// The pointer's position in screen pixels, if it moved since the last call.
bool dx_sdl_mouse_position(int *x, int *y);
// Frames of sound queued on the device and not yet played; -1 without a sound device.
int dx_sdl_audio_queued(void);
// Queues mixed sound: 16-bit stereo frames at 22050 Hz.
void dx_sdl_queue_audio(const int16_t *stereo, int frames);
// Drops the sound queued on the device.
void dx_sdl_clear_audio(void);

#ifdef __cplusplus
}
#endif
