// sdl_events.cpp - event queue, keyboard, mouse and joystick of the pico-lutionX SDL 1.2
// subset. The platform reports input through the sdlp_* functions (see sdl_backend.h).
#include "SDL.h"
#include "sdl_backend.h"

namespace {

constexpr int QueueSize = 128;
SDL_Event Queue[QueueSize];
int QueueHead;
int QueueCount;

Uint8 KeyState[SDLK_LAST];
SDLMod ModState = KMOD_NONE;
int UnicodeEnabled;

int MouseX = 320;
int MouseY = 240;
Uint8 MouseButtons;

struct VirtualJoystick {
	int present;
	char name[48];
	int buttons;
	int hats;
	int axes;
	Uint8 buttonState[32];
	Uint8 hatState;
	Sint16 axisState[8];
	int opened;
};
VirtualJoystick Joystick;
// SDL_Joystick is opaque to the engine; hand out the address of the only joystick.
SDL_Joystick *const JoystickHandle = reinterpret_cast<SDL_Joystick *>(&Joystick);

void Push(const SDL_Event &event)
{
	if (QueueCount == QueueSize)
		return; // full: drop the event, as SDL does
	Queue[(QueueHead + QueueCount) % QueueSize] = event;
	QueueCount++;
}

SDLMod ModifierOf(SDLKey sym)
{
	switch (sym) {
	case SDLK_LSHIFT:
		return KMOD_LSHIFT;
	case SDLK_RSHIFT:
		return KMOD_RSHIFT;
	case SDLK_LCTRL:
		return KMOD_LCTRL;
	case SDLK_RCTRL:
		return KMOD_RCTRL;
	case SDLK_LALT:
		return KMOD_LALT;
	case SDLK_RALT:
		return KMOD_RALT;
	case SDLK_LMETA:
		return KMOD_LMETA;
	case SDLK_RMETA:
		return KMOD_RMETA;
	case SDLK_MODE:
		return KMOD_MODE;
	default:
		return KMOD_NONE;
	}
}

void PushMotion(int xrel, int yrel)
{
	SDL_Event event = {};
	event.motion.type = SDL_MOUSEMOTION;
	event.motion.state = MouseButtons;
	event.motion.x = static_cast<Uint16>(MouseX);
	event.motion.y = static_cast<Uint16>(MouseY);
	event.motion.xrel = static_cast<Sint16>(xrel);
	event.motion.yrel = static_cast<Sint16>(yrel);
	Push(event);
}

void SetMouse(int x, int y)
{
	SDL_Surface *screen = SDL_GetVideoSurface();
	int w = screen != nullptr ? screen->w : 640;
	int h = screen != nullptr ? screen->h : 480;
	x = x < 0 ? 0 : (x >= w ? w - 1 : x);
	y = y < 0 ? 0 : (y >= h ? h - 1 : y);
	int xrel = x - MouseX, yrel = y - MouseY;
	MouseX = x;
	MouseY = y;
	if (xrel != 0 || yrel != 0)
		PushMotion(xrel, yrel);
}

} // namespace

extern "C" {

// ---- Called by the platform ----------------------------------------------------------

void sdlp_key(int down, SDLKey sym, Uint16 unicode)
{
	if (sym <= SDLK_UNKNOWN || sym >= SDLK_LAST)
		return;
	SDLMod mod = ModifierOf(sym);
	if (down) {
		if (sym == SDLK_CAPSLOCK)
			ModState = static_cast<SDLMod>(ModState ^ KMOD_CAPS);
		else if (sym == SDLK_NUMLOCK)
			ModState = static_cast<SDLMod>(ModState ^ KMOD_NUM);
		ModState = static_cast<SDLMod>(ModState | mod);
	} else {
		ModState = static_cast<SDLMod>(ModState & ~mod);
	}
	KeyState[sym] = down ? SDL_PRESSED : SDL_RELEASED;
	SDL_Event event = {};
	event.key.type = down ? SDL_KEYDOWN : SDL_KEYUP;
	event.key.state = down ? SDL_PRESSED : SDL_RELEASED;
	event.key.keysym.sym = sym;
	event.key.keysym.mod = ModState;
	event.key.keysym.unicode = (down && UnicodeEnabled) ? unicode : 0;
	Push(event);
}

void sdlp_mouse_motion(int dx, int dy)
{
	SetMouse(MouseX + dx, MouseY + dy);
}

void sdlp_mouse_position(int x, int y)
{
	SetMouse(x, y);
}

void sdlp_mouse_button(int down, int button)
{
	if (button < 1 || button > 8)
		return;
	if (down)
		MouseButtons = static_cast<Uint8>(MouseButtons | SDL_BUTTON(button));
	else
		MouseButtons = static_cast<Uint8>(MouseButtons & ~SDL_BUTTON(button));
	SDL_Event event = {};
	event.button.type = down ? SDL_MOUSEBUTTONDOWN : SDL_MOUSEBUTTONUP;
	event.button.button = static_cast<Uint8>(button);
	event.button.state = down ? SDL_PRESSED : SDL_RELEASED;
	event.button.x = static_cast<Uint16>(MouseX);
	event.button.y = static_cast<Uint16>(MouseY);
	Push(event);
}

void sdlp_joystick_config(int present, const char *name, int buttons, int hats, int axes)
{
	Joystick.present = present;
	SDL_strlcpy(Joystick.name, name != nullptr ? name : "Gamepad", sizeof(Joystick.name));
	Joystick.buttons = buttons > 32 ? 32 : buttons;
	Joystick.hats = hats > 1 ? 1 : hats;
	Joystick.axes = axes > 8 ? 8 : axes;
}

void sdlp_joystick_button(int button, int down)
{
	if (button < 0 || button >= Joystick.buttons)
		return;
	Uint8 state = down ? SDL_PRESSED : SDL_RELEASED;
	if (Joystick.buttonState[button] == state)
		return;
	Joystick.buttonState[button] = state;
	SDL_Event event = {};
	event.jbutton.type = down ? SDL_JOYBUTTONDOWN : SDL_JOYBUTTONUP;
	event.jbutton.button = static_cast<Uint8>(button);
	event.jbutton.state = state;
	Push(event);
}

void sdlp_joystick_hat(Uint8 value)
{
	if (Joystick.hats == 0 || Joystick.hatState == value)
		return;
	Joystick.hatState = value;
	SDL_Event event = {};
	event.jhat.type = SDL_JOYHATMOTION;
	event.jhat.value = value;
	Push(event);
}

void sdlp_joystick_axis(int axis, Sint16 value)
{
	if (axis < 0 || axis >= Joystick.axes || Joystick.axisState[axis] == value)
		return;
	Joystick.axisState[axis] = value;
	SDL_Event event = {};
	event.jaxis.type = SDL_JOYAXISMOTION;
	event.jaxis.axis = static_cast<Uint8>(axis);
	event.jaxis.value = value;
	Push(event);
}

void sdlp_quit(void)
{
	SDL_Event event = {};
	event.type = SDL_QUIT;
	Push(event);
}

// ---- Events --------------------------------------------------------------------------

void SDL_PumpEvents(void)
{
	sdlb_poll();
}

int SDL_PollEvent(SDL_Event *event)
{
	if (QueueCount == 0)
		sdlb_poll();
	if (QueueCount == 0)
		return 0;
	if (event != nullptr) {
		*event = Queue[QueueHead];
		QueueHead = (QueueHead + 1) % QueueSize;
		QueueCount--;
	}
	return 1;
}

int SDL_WaitEvent(SDL_Event *event)
{
	while (!SDL_PollEvent(event))
		sdlb_delay(1);
	return 1;
}

int SDL_PushEvent(SDL_Event *event)
{
	Push(*event);
	return 0;
}

Uint8 SDL_GetAppState(void)
{
	return SDL_APPMOUSEFOCUS | SDL_APPINPUTFOCUS | SDL_APPACTIVE;
}

Uint8 SDL_EventState(Uint8 type, int state)
{
	(void)type;
	(void)state;
	return SDL_ENABLE;
}

// ---- Keyboard ------------------------------------------------------------------------

int SDL_EnableUNICODE(int enable)
{
	int previous = UnicodeEnabled;
	if (enable >= 0)
		UnicodeEnabled = enable;
	return previous;
}

int SDL_EnableKeyRepeat(int delay, int interval)
{
	(void)delay;
	(void)interval;
	return 0;
}

Uint8 *SDL_GetKeyState(int *numkeys)
{
	if (numkeys != nullptr)
		*numkeys = SDLK_LAST;
	return KeyState;
}

SDLMod SDL_GetModState(void)
{
	return ModState;
}

void SDL_SetModState(SDLMod modstate)
{
	ModState = modstate;
}

char *SDL_GetKeyName(SDLKey key)
{
	static char name[16];
	if (key >= SDLK_SPACE && key < SDLK_DELETE) {
		name[0] = static_cast<char>(key);
		name[1] = '\0';
		return name;
	}
	if (key >= SDLK_F1 && key <= SDLK_F15) {
		snprintf(name, sizeof(name), "f%d", key - SDLK_F1 + 1);
		return name;
	}
	snprintf(name, sizeof(name), "key %d", key);
	return name;
}

// ---- Mouse ---------------------------------------------------------------------------

Uint8 SDL_GetMouseState(int *x, int *y)
{
	if (x != nullptr)
		*x = MouseX;
	if (y != nullptr)
		*y = MouseY;
	return MouseButtons;
}

Uint8 SDL_GetRelativeMouseState(int *x, int *y)
{
	if (x != nullptr)
		*x = 0;
	if (y != nullptr)
		*y = 0;
	return MouseButtons;
}

void SDL_WarpMouse(Uint16 x, Uint16 y)
{
	SetMouse(x, y);
}

// ---- Joystick ------------------------------------------------------------------------

int SDL_NumJoysticks(void)
{
	return Joystick.present ? 1 : 0;
}

const char *SDL_JoystickName(int device_index)
{
	return (device_index == 0 && Joystick.present) ? Joystick.name : nullptr;
}

SDL_Joystick *SDL_JoystickOpen(int device_index)
{
	if (device_index != 0 || !Joystick.present) {
		SDL_SetError("Joystick %d is not available", device_index);
		return nullptr;
	}
	Joystick.opened = 1;
	return JoystickHandle;
}

int SDL_JoystickOpened(int device_index)
{
	return device_index == 0 && Joystick.opened;
}

int SDL_JoystickIndex(SDL_Joystick *joystick)
{
	return joystick == JoystickHandle ? 0 : -1;
}

int SDL_JoystickNumAxes(SDL_Joystick *joystick)
{
	return joystick == JoystickHandle ? Joystick.axes : -1;
}

int SDL_JoystickNumBalls(SDL_Joystick *joystick)
{
	(void)joystick;
	return 0;
}

int SDL_JoystickNumHats(SDL_Joystick *joystick)
{
	return joystick == JoystickHandle ? Joystick.hats : -1;
}

int SDL_JoystickNumButtons(SDL_Joystick *joystick)
{
	return joystick == JoystickHandle ? Joystick.buttons : -1;
}

void SDL_JoystickUpdate(void)
{
	sdlb_poll();
}

int SDL_JoystickEventState(int state)
{
	(void)state;
	return SDL_ENABLE;
}

Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis)
{
	if (joystick != JoystickHandle || axis < 0 || axis >= Joystick.axes)
		return 0;
	return Joystick.axisState[axis];
}

Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat)
{
	if (joystick != JoystickHandle || hat != 0 || Joystick.hats == 0)
		return SDL_HAT_CENTERED;
	return Joystick.hatState;
}

Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button)
{
	if (joystick != JoystickHandle || button < 0 || button >= Joystick.buttons)
		return 0;
	return Joystick.buttonState[button];
}

void SDL_JoystickClose(SDL_Joystick *joystick)
{
	if (joystick == JoystickHandle)
		Joystick.opened = 0;
}

} // extern "C"
