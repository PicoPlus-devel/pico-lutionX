// dx_input.cpp -- USB keyboard, mouse and gamepads to SDL events.
//
// dx_input_poll runs in thread context (from SDL_PollEvent) and turns the events the
// USB timer recorded (dx_usb.cpp) into SDL 1.2 events (src/sdl/sdl_backend.h):
//   - keyboard: HID usages to SDL keys, with the character for text input (US layout);
//   - mouse (a USB mouse merged with a SNES Mouse in a controller port): buttons, wheel
//     notches (SDL buttons 4 and 5) and relative motion, which the SDL subset integrates
//     into the cursor position;
//   - gamepads (USB pads merged with the NES/SNES ports): SDL joystick 0, buttons and
//     axes as cmake/dx_engine.cmake declares them to the engine (JOY_*).
//
// Gamepad buttons are named by position, as the HID driver reports them: io B is the
// bottom face button (Xbox A, PlayStation cross, SNES B), io A the right one, io Y the
// left one and io X the top one. What they do is DevilutionX's default gamepad layout
// (InitPadmapActions), which every pad reaches: the USB pads with all of their buttons, a
// SNES pad (no triggers, no sticks) partly through the engine's Start menu, and a NES pad
// through a layout of its own (dx_nespad.cpp).

#include <cstdint>

#include "gamepad.h"
#include "sdl_backend.h"

#include "dx_input.h"
#include "dx_prof.h"

namespace {

// ---- Keyboard ---------------------------------------------------------------------------

struct KeyInfo {
	SDLKey sym;
	char plain;
	char shifted;
};

KeyInfo KeyForUsage(uint8_t usage)
{
	static const char Digits[] = "1234567890";
	static const char DigitsShifted[] = "!@#$%^&*()";
	if (usage >= 0x04 && usage <= 0x1d) {
		char c = static_cast<char>('a' + usage - 0x04);
		return { static_cast<SDLKey>(c), c, static_cast<char>(c - 'a' + 'A') };
	}
	if (usage >= 0x1e && usage <= 0x27) {
		char c = Digits[usage - 0x1e];
		return { static_cast<SDLKey>(c), c, DigitsShifted[usage - 0x1e] };
	}
	if (usage >= 0x3a && usage <= 0x45)
		return { static_cast<SDLKey>(SDLK_F1 + usage - 0x3a), 0, 0 };
	if (usage >= 0x59 && usage <= 0x61)
		return { static_cast<SDLKey>(SDLK_KP1 + usage - 0x59), static_cast<char>('1' + usage - 0x59), static_cast<char>('1' + usage - 0x59) };
	switch (usage) {
	case 0x28: return { SDLK_RETURN, '\r', '\r' };
	case 0x29: return { SDLK_ESCAPE, 0, 0 };
	case 0x2a: return { SDLK_BACKSPACE, '\b', '\b' };
	case 0x2b: return { SDLK_TAB, 0, 0 };
	case 0x2c: return { SDLK_SPACE, ' ', ' ' };
	case 0x2d: return { SDLK_MINUS, '-', '_' };
	case 0x2e: return { SDLK_EQUALS, '=', '+' };
	case 0x2f: return { SDLK_LEFTBRACKET, '[', '{' };
	case 0x30: return { SDLK_RIGHTBRACKET, ']', '}' };
	case 0x31: return { SDLK_BACKSLASH, '\\', '|' };
	case 0x32: return { SDLK_HASH, '#', '~' };
	case 0x33: return { SDLK_SEMICOLON, ';', ':' };
	case 0x34: return { SDLK_QUOTE, '\'', '"' };
	case 0x35: return { SDLK_BACKQUOTE, '`', '~' };
	case 0x36: return { SDLK_COMMA, ',', '<' };
	case 0x37: return { SDLK_PERIOD, '.', '>' };
	case 0x38: return { SDLK_SLASH, '/', '?' };
	case 0x39: return { SDLK_CAPSLOCK, 0, 0 };
	case 0x46: return { SDLK_PRINT, 0, 0 };
	case 0x47: return { SDLK_SCROLLOCK, 0, 0 };
	case 0x48: return { SDLK_PAUSE, 0, 0 };
	case 0x49: return { SDLK_INSERT, 0, 0 };
	case 0x4a: return { SDLK_HOME, 0, 0 };
	case 0x4b: return { SDLK_PAGEUP, 0, 0 };
	case 0x4c: return { SDLK_DELETE, 0, 0 };
	case 0x4d: return { SDLK_END, 0, 0 };
	case 0x4e: return { SDLK_PAGEDOWN, 0, 0 };
	case 0x4f: return { SDLK_RIGHT, 0, 0 };
	case 0x50: return { SDLK_LEFT, 0, 0 };
	case 0x51: return { SDLK_DOWN, 0, 0 };
	case 0x52: return { SDLK_UP, 0, 0 };
	case 0x53: return { SDLK_NUMLOCK, 0, 0 };
	case 0x54: return { SDLK_KP_DIVIDE, '/', '/' };
	case 0x55: return { SDLK_KP_MULTIPLY, '*', '*' };
	case 0x56: return { SDLK_KP_MINUS, '-', '-' };
	case 0x57: return { SDLK_KP_PLUS, '+', '+' };
	case 0x58: return { SDLK_KP_ENTER, '\r', '\r' };
	case 0x62: return { SDLK_KP0, '0', '0' };
	case 0x63: return { SDLK_KP_PERIOD, '.', '.' };
	case 0x64: return { SDLK_BACKSLASH, '\\', '|' };
	case 0x65: return { SDLK_MENU, 0, 0 };
	case 0xe0: return { SDLK_LCTRL, 0, 0 };
	case 0xe1: return { SDLK_LSHIFT, 0, 0 };
	case 0xe2: return { SDLK_LALT, 0, 0 };
	case 0xe3: return { SDLK_LSUPER, 0, 0 };
	case 0xe4: return { SDLK_RCTRL, 0, 0 };
	case 0xe5: return { SDLK_RSHIFT, 0, 0 };
	case 0xe6: return { SDLK_RALT, 0, 0 };
	case 0xe7: return { SDLK_RSUPER, 0, 0 };
	default: return { SDLK_UNKNOWN, 0, 0 };
	}
}

void HandleKey(uint8_t usage, bool down)
{
	static bool shift[2], capsLock;
	KeyInfo key = KeyForUsage(usage);

	if (usage == 0xe1 || usage == 0xe5)
		shift[usage == 0xe5] = down;
	if (usage == 0x39 && down)
		capsLock = !capsLock;
	if (key.sym == SDLK_UNKNOWN)
		return;
	bool shifted = shift[0] || shift[1];
	char c = shifted ? key.shifted : key.plain;
	if (capsLock && key.plain >= 'a' && key.plain <= 'z')
		c = shifted ? key.plain : key.shifted;
	sdlp_key(down ? 1 : 0, key.sym, static_cast<Uint16>(static_cast<unsigned char>(c)));
}

// ---- Gamepads ---------------------------------------------------------------------------

using Button = io::GamePadState::Button;

// io::GamePadState::Button bit -> SDL joystick button (cmake/dx_engine.cmake), -1: none.
int JoystickButtonForPadBit(int bit)
{
	switch (1u << bit) {
	case static_cast<unsigned>(Button::B): return 0;      // bottom: JOY_BUTTON_A
	case static_cast<unsigned>(Button::A): return 1;      // right: JOY_BUTTON_B
	case static_cast<unsigned>(Button::Y): return 2;      // left: JOY_BUTTON_X
	case static_cast<unsigned>(Button::X): return 3;      // top: JOY_BUTTON_Y
	case static_cast<unsigned>(Button::L): return 4;      // JOY_BUTTON_LEFTSHOULDER
	case static_cast<unsigned>(Button::R): return 5;      // JOY_BUTTON_RIGHTSHOULDER
	case static_cast<unsigned>(Button::C): return 6;      // Genesis pads: JOY_BUTTON_TRIGGERLEFT
	case static_cast<unsigned>(Button::Z): return 7;      // JOY_BUTTON_TRIGGERRIGHT
	case static_cast<unsigned>(Button::SELECT): return 8; // JOY_BUTTON_BACK
	case static_cast<unsigned>(Button::START): return 9;  // JOY_BUTTON_START
	case static_cast<unsigned>(Button::LSTICK): return 10; // JOY_BUTTON_LEFTSTICK
	case static_cast<unsigned>(Button::RSTICK): return 11; // JOY_BUTTON_RIGHTSTICK
	case static_cast<unsigned>(Button::UP): return 12;    // JOY_BUTTON_DPAD_UP
	case static_cast<unsigned>(Button::DOWN): return 13;
	case static_cast<unsigned>(Button::LEFT): return 14;
	case static_cast<unsigned>(Button::RIGHT): return 15;
	default: return -1;
	}
}

} // namespace

extern "C" void dx_input_init(void)
{
	sdlp_joystick_config(1, "Gamepad", 16, 0, 4);
}

extern "C" void dx_input_poll(void)
{
	DxProfZone prof(DXP_INPUT);
	dx_input_event_t ev;

	while (dx_input_pop(&ev)) {
		switch (ev.type) {
		case DX_EV_KEY:
			HandleKey(ev.code, ev.down != 0);
			break;
		case DX_EV_MOUSE_BUTTON: {
			static const int MouseButtons[3] = { SDL_BUTTON_LEFT, SDL_BUTTON_RIGHT, SDL_BUTTON_MIDDLE };
			if (ev.code < 3)
				sdlp_mouse_button(ev.down, MouseButtons[ev.code]);
			break;
		}
		case DX_EV_PAD_BUTTON: {
			int button = JoystickButtonForPadBit(ev.code);
			if (button >= 0)
				sdlp_joystick_button(button, ev.down);
			break;
		}
		case DX_EV_TRIGGER:
			sdlp_joystick_button(6 + ev.code, ev.down);
			break;
		}
	}

	int dx, dy, wheel;
	dx_input_mouse(&dx, &dy, &wheel);
	if (dx != 0 || dy != 0)
		sdlp_mouse_motion(dx, dy);
	for (; wheel > 0; wheel--) {
		sdlp_mouse_button(1, SDL_BUTTON_WHEELUP);
		sdlp_mouse_button(0, SDL_BUTTON_WHEELUP);
	}
	for (; wheel < 0; wheel++) {
		sdlp_mouse_button(1, SDL_BUTTON_WHEELDOWN);
		sdlp_mouse_button(0, SDL_BUTTON_WHEELDOWN);
	}

	// the Wii extension pad, read here in thread context (it shares the codec's I2C bus)
	static uint32_t lastWii;
	const uint32_t wii = dx_wiipad_buttons();
	if (wii != lastWii) {
		for (int bit = 0; bit < 32; bit++) {
			if (((wii ^ lastWii) >> bit) & 1) {
				int button = JoystickButtonForPadBit(bit);
				if (button >= 0)
					sdlp_joystick_button(button, (wii >> bit) & 1);
			}
		}
		lastWii = wii;
	}

	int16_t axes[4];
	dx_input_sticks(axes);
	for (int i = 0; i < 4; i++)
		sdlp_joystick_axis(i, axes[i]);
}
