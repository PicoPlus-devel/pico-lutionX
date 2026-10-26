// dx_input.h -- input: the USB host (dx_usb.cpp), the NES/SNES ports (dx_nespad.cpp)
// and their translation to SDL events (dx_input.cpp).

#ifndef DX_INPUT_H
#define DX_INPUT_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Raw input events, recorded by the USB timer interrupt (dx_usb.cpp) and turned into
// SDL events by dx_input_poll in thread context.
enum {
	DX_EV_KEY,          // code: HID keyboard usage (modifiers as 0xe0-0xe7)
	DX_EV_MOUSE_BUTTON, // code: 0 left, 1 right, 2 middle
	DX_EV_PAD_BUTTON,   // code: bit number of io::GamePadState::Button
	DX_EV_TRIGGER,      // code: 0 left, 1 right (analog trigger past the threshold)
};

typedef struct {
	uint8_t type;
	uint8_t code;
	uint8_t down;
} dx_input_event_t;

// Pops the oldest event; false when there is none.
bool dx_input_pop(dx_input_event_t *ev);
// Mouse motion and wheel notches since the last call (+wheel = up).
void dx_input_mouse(int *dx, int *dy, int *wheel);
// The analog sticks with the largest deflection, -32768..32767, +y down (SDL).
void dx_input_sticks(int16_t axes[4]);

// Brings up the USB host and starts the sampling timer.
void dx_usb_init(void);

// dx_usb_frame.c: drives the 1 ms USB frame (called by dx_usb_init), and keeps SOFs
// going around a flash erase or program (masks every other interrupt on core0).
typedef struct {
	bool masked;
	uint32_t primask;
	uint32_t iser[2];
} dx_irq_state_t;
void dx_usb_frame_start(void);
dx_irq_state_t dx_usb_flash_begin(void);
void dx_usb_flash_end(dx_irq_state_t st);

// dx_nespad.cpp: the NES/SNES controller ports on boards that have them (NES_PIN_CLK in
// the board header); no-ops elsewhere. dx_nespad_buttons runs in the USB timer
// interrupt and returns io::GamePadState::Button bits. A SNES Mouse in a port:
// dx_nespad_mouse_buttons returns its buttons as of the last dx_nespad_buttons (bit 0
// left, bit 1 right); dx_nespad_mouse_motion adds its motion since the last call, in
// cursor pixels, to *dx and *dy (call it with interrupts disabled).
void dx_nespad_init(void);
uint32_t dx_nespad_buttons(void);
uint8_t dx_nespad_mouse_buttons(void);
void dx_nespad_mouse_motion(int *dx, int *dy);

// dx_wiipad.cpp: the Wii extension pad on boards that have one (WII_PIN_SDA in the board
// header); no-ops elsewhere. Init before the audio codec is set up; read in thread context.
void dx_wiipad_init(void);
uint32_t dx_wiipad_buttons(void);
void dx_wiipad_shutdown(void);

// dx_input.cpp: declares SDL joystick 0 (before the engine starts), and turns the
// recorded events into SDL events (called from sdlb_poll).
void dx_input_init(void);
void dx_input_poll(void);

#ifdef __cplusplus
}
#endif

#endif
