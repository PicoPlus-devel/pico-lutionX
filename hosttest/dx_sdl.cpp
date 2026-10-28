// dx_sdl.cpp - the window, sound and input of dx_host_sdl (see dx_sdl.h).
//
// The port's SDL 1.2 subset (src/sdl) defines C functions with SDL2's names (SDL_Init,
// SDL_PollEvent, SDL_GetError, SDL_CreateRGBSurface, ...), so SDL2 cannot be linked into
// the same program: those calls would reach the subset. SDL2 is loaded with dlopen
// instead, and called through the function pointers below, which are typed from SDL2's
// own declarations. Inside the anonymous namespace an unqualified SDL_ name is the
// pointer, which hides SDL2's declaration; outside it the name is ambiguous and does not
// compile, so no call can reach the subset by mistake.
//
// As on the board:
//   - the picture is 640x480, each palette channel truncated to 5 bits (rgb555 in
//     src/pico/dx_display.c);
//   - keys go to the engine as HID usages (SDL2's scancodes are HID usages), so they are
//     mapped by position with the firmware's table (src/pico/dx_input.cpp), US layout;
//   - the game controllers are merged into one gamepad and their buttons named by
//     position, as the board's USB host reports its pads (src/pico/dx_usb.cpp);
//   - the engine's thread mixes the sound (dx_host.cpp, which models the board's sound
//     ring); the device plays it from a queue, and silence when the queue runs empty.
// The mouse is absolute: the engine's cursor follows the pointer.
//
// Nothing here allocates from the C library's heap: the harness charges the program's
// malloc to the board's PSRAM (--wrap=malloc). SDL2's own allocations are inside the
// library, which is not wrapped. The program's operator new and delete (dx_heap.cpp) are
// another matter: they replace the global ones for every library, so a C++ library loaded
// by SDL would allocate from the PSRAM model too. The GL drivers are such libraries (Mesa
// with LLVM), so the picture goes through SDL's software renderer to the X11 window's
// shared-memory framebuffer, and no GL library is loaded.
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include <dlfcn.h>

#include <SDL.h>

#include "dx_input.h"
#include "dx_sdl.h"
#include "gamepad.h"

namespace {

#define DX_SDL_FUNCTIONS \
	X(SDL_Init) \
	X(SDL_InitSubSystem) \
	X(SDL_Quit) \
	X(SDL_GetError) \
	X(SDL_SetHint) \
	X(SDL_GetDisplayUsableBounds) \
	X(SDL_GetCurrentVideoDriver) \
	X(SDL_CreateWindow) \
	X(SDL_CreateRenderer) \
	X(SDL_GetRendererInfo) \
	X(SDL_RenderSetLogicalSize) \
	X(SDL_CreateTexture) \
	X(SDL_LockTexture) \
	X(SDL_UnlockTexture) \
	X(SDL_RenderClear) \
	X(SDL_RenderCopy) \
	X(SDL_RenderPresent) \
	X(SDL_ShowCursor) \
	X(SDL_PollEvent) \
	X(SDL_GetCurrentAudioDriver) \
	X(SDL_OpenAudioDevice) \
	X(SDL_PauseAudioDevice) \
	X(SDL_QueueAudio) \
	X(SDL_GetQueuedAudioSize) \
	X(SDL_ClearQueuedAudio) \
	X(SDL_GameControllerOpen) \
	X(SDL_GameControllerClose) \
	X(SDL_GameControllerFromInstanceID) \
	X(SDL_GameControllerName) \
	X(SDL_GameControllerGetButton) \
	X(SDL_GameControllerGetAxis)

#define X(name) decltype(&::name) name;
DX_SDL_FUNCTIONS
#undef X

bool LoadSdl2()
{
	void *lib = dlopen("libSDL2-2.0.so.0", RTLD_NOW | RTLD_LOCAL);
	if (lib == nullptr) {
		fprintf(stderr, "dx_host_sdl: %s (the 32-bit SDL2: libsdl2-2.0-0:i386)\n", dlerror());
		return false;
	}
#define X(name) \
	if ((name = reinterpret_cast<decltype(name)>(dlsym(lib, #name))) == nullptr) { \
		fprintf(stderr, "dx_host_sdl: SDL2 has no %s\n", #name); \
		return false; \
	}
	DX_SDL_FUNCTIONS
#undef X
	return true;
}

constexpr int ScreenWidth = 640;
constexpr int ScreenHeight = 480;
constexpr int MixRate = 22050;
constexpr int TriggerOn = 100 * 32767 / 255; // TRIGGER_ON of src/pico/dx_usb.cpp on SDL's axis scale

SDL_Window *Window;
SDL_Renderer *Renderer;
SDL_Texture *Texture;
SDL_AudioDeviceID Audio;
bool QuitRequested;

// ---- Window ------------------------------------------------------------------------------

void OpenAudio();

bool Init()
{
	if (!LoadSdl2())
		return false;
	// no GL (see the top of this file): the window's own framebuffer, not a GL texture
	SDL_SetHint(SDL_HINT_FRAMEBUFFER_ACCELERATION, "0");
	if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMECONTROLLER) != 0) {
		fprintf(stderr, "dx_host_sdl: %s\n", SDL_GetError());
		return false;
	}
	atexit(SDL_Quit);

	// twice the size where the desktop has room for it
	int scale = 2;
	SDL_Rect desktop;
	if (SDL_GetDisplayUsableBounds(0, &desktop) == 0 && (desktop.w < 2 * ScreenWidth || desktop.h < 2 * ScreenHeight))
		scale = 1;
	if (const char *env = getenv("DX_SDL_SCALE"))
		scale = std::max(1, atoi(env));
	Window = SDL_CreateWindow("pico-lutionX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
	    ScreenWidth * scale, ScreenHeight * scale, SDL_WINDOW_RESIZABLE);
	if (Window != nullptr)
		Renderer = SDL_CreateRenderer(Window, -1, SDL_RENDERER_SOFTWARE);
	if (Renderer != nullptr)
		Texture = SDL_CreateTexture(Renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, ScreenWidth, ScreenHeight);
	if (Texture == nullptr) {
		fprintf(stderr, "dx_host_sdl: %s (%s video; without GL a window needs the x11 driver)\n", SDL_GetError(),
		    SDL_GetCurrentVideoDriver());
		return false;
	}
	// 4:3 in a window of any size, and mouse coordinates in screen pixels
	SDL_RenderSetLogicalSize(Renderer, ScreenWidth, ScreenHeight);
	// the engine draws its own cursor into the picture
	SDL_ShowCursor(SDL_DISABLE);
	SDL_RendererInfo info;
	const char *renderer = SDL_GetRendererInfo(Renderer, &info) == 0 ? info.name : "?";
	fprintf(stderr, "dx_host_sdl: %s video, %s renderer, window %dx%d\n", SDL_GetCurrentVideoDriver(), renderer,
	    ScreenWidth * scale, ScreenHeight * scale);
	OpenAudio();
	return true;
}

void Present(const uint8_t *pixels, int pitch, int width, int height, const uint8_t *rgbx)
{
	uint32_t lut[256];
	for (int i = 0; i < 256; i++) {
		const uint8_t *c = rgbx + 4 * i;
		lut[i] = 0xff000000u | (c[0] & 0xf8u) << 16 | (c[1] & 0xf8u) << 8 | (c[2] & 0xf8u);
	}
	void *out;
	int outPitch;
	if (SDL_LockTexture(Texture, nullptr, &out, &outPitch) != 0)
		return;
	// as dx_display_present: the board's front buffer is 640x480
	const int w = std::min(width, ScreenWidth);
	const int h = std::min(height, ScreenHeight);
	for (int y = 0; y < h; y++) {
		const uint8_t *src = pixels + y * pitch;
		uint32_t *dst = reinterpret_cast<uint32_t *>(static_cast<uint8_t *>(out) + y * outPitch);
		for (int x = 0; x < w; x++)
			dst[x] = lut[src[x]];
	}
	SDL_UnlockTexture(Texture);
	SDL_RenderClear(Renderer);
	SDL_RenderCopy(Renderer, Texture, nullptr, nullptr);
	SDL_RenderPresent(Renderer);
}

// ---- Sound -------------------------------------------------------------------------------

void OpenAudio()
{
	if (SDL_InitSubSystem(SDL_INIT_AUDIO) != 0) {
		fprintf(stderr, "dx_host_sdl: no sound: %s\n", SDL_GetError());
		return;
	}
	SDL_AudioSpec want = {};
	want.freq = MixRate;
	want.format = AUDIO_S16SYS;
	want.channels = 2;
	want.samples = 512;
	Audio = SDL_OpenAudioDevice(nullptr, 0, &want, nullptr, 0); // SDL converts to the device's format
	if (Audio == 0) {
		fprintf(stderr, "dx_host_sdl: no sound: %s\n", SDL_GetError());
		return;
	}
	SDL_PauseAudioDevice(Audio, 0);
	fprintf(stderr, "dx_host_sdl: %s sound\n", SDL_GetCurrentAudioDriver());
}

int AudioQueued()
{
	return Audio != 0 ? static_cast<int>(SDL_GetQueuedAudioSize(Audio) / 4) : -1;
}

void QueueAudio(const int16_t *stereo, int frames)
{
	if (Audio != 0)
		SDL_QueueAudio(Audio, stereo, static_cast<Uint32>(frames) * 4);
}

void ClearAudio()
{
	if (Audio != 0)
		SDL_ClearQueuedAudio(Audio);
}

// ---- Raw input, as the board's USB host records it (src/pico/dx_usb.cpp) ------------------

constexpr unsigned RingSize = 256;
dx_input_event_t Ring[RingSize];
unsigned RingHead, RingTail;

void Push(uint8_t type, uint8_t code, bool down)
{
	if (RingHead - RingTail >= RingSize)
		return; // full: drop
	Ring[RingHead++ % RingSize] = { type, code, static_cast<uint8_t>(down ? 1 : 0) };
}

bool KeyDown[256];
uint8_t MouseButtons; // bit 0 left, 1 right, 2 middle
int MouseX = -1, MouseY = -1;
bool MouseMoved;
int Wheel;

// Usages 4 to 0x65 and the modifiers 0xe0 to 0xe7, as a USB keyboard reports them.
void Key(int usage, bool down)
{
	if (usage < 4 || usage > 0xe7 || KeyDown[usage] == down)
		return;
	KeyDown[usage] = down;
	Push(DX_EV_KEY, static_cast<uint8_t>(usage), down);
}

void MouseButton(int bit, bool down)
{
	if (bit < 0 || (((MouseButtons >> bit) & 1) != 0) == down)
		return;
	MouseButtons ^= 1u << bit;
	Push(DX_EV_MOUSE_BUTTON, static_cast<uint8_t>(bit), down);
}

int MouseBit(Uint8 button)
{
	switch (button) {
	case SDL_BUTTON_LEFT: return 0;
	case SDL_BUTTON_RIGHT: return 1;
	case SDL_BUTTON_MIDDLE: return 2;
	default: return -1;
	}
}

void SetPointer(int x, int y)
{
	if (x == MouseX && y == MouseY)
		return;
	MouseX = x;
	MouseY = y;
	MouseMoved = true;
}

// Keys and buttons held when the window loses the focus would never be released.
void ReleaseAll()
{
	for (int usage = 0; usage < 256; usage++)
		Key(usage, false);
	for (int bit = 0; bit < 3; bit++)
		MouseButton(bit, false);
}

// ---- Game controllers --------------------------------------------------------------------

using Button = io::GamePadState::Button;

// SDL's buttons by position; io::GamePadState names them as the board's HID driver does
// (io B is the bottom face button, see src/pico/dx_input.cpp).
const struct {
	SDL_GameControllerButton button;
	int bits;
} PadButtons[] = {
	{ SDL_CONTROLLER_BUTTON_A, Button::B }, // bottom
	{ SDL_CONTROLLER_BUTTON_B, Button::A }, // right
	{ SDL_CONTROLLER_BUTTON_X, Button::Y }, // left
	{ SDL_CONTROLLER_BUTTON_Y, Button::X }, // top
	{ SDL_CONTROLLER_BUTTON_BACK, Button::SELECT },
	{ SDL_CONTROLLER_BUTTON_TOUCHPAD, Button::SELECT },               // PlayStation: the touchpad click is Back
	{ SDL_CONTROLLER_BUTTON_GUIDE, Button::SELECT | Button::START }, // as hid_app.cpp: Guide and PS
	{ SDL_CONTROLLER_BUTTON_START, Button::START },
	{ SDL_CONTROLLER_BUTTON_LEFTSHOULDER, Button::L },
	{ SDL_CONTROLLER_BUTTON_RIGHTSHOULDER, Button::R },
	{ SDL_CONTROLLER_BUTTON_LEFTSTICK, Button::LSTICK },
	{ SDL_CONTROLLER_BUTTON_RIGHTSTICK, Button::RSTICK },
	{ SDL_CONTROLLER_BUTTON_DPAD_UP, Button::UP },
	{ SDL_CONTROLLER_BUTTON_DPAD_DOWN, Button::DOWN },
	{ SDL_CONTROLLER_BUTTON_DPAD_LEFT, Button::LEFT },
	{ SDL_CONTROLLER_BUTTON_DPAD_RIGHT, Button::RIGHT },
};

constexpr int MaxPads = 4;
SDL_GameController *Pads[MaxPads];
uint32_t LastPadButtons;
bool LastTrigger[2];
int16_t Sticks[4];

void OpenPad(int deviceIndex)
{
	SDL_GameController *pad = SDL_GameControllerOpen(deviceIndex);
	if (pad == nullptr)
		return;
	SDL_GameController **slot = nullptr;
	for (SDL_GameController *&p : Pads) {
		if (p == pad) {
			SDL_GameControllerClose(pad); // already open: drop the second reference
			return;
		}
		if (p == nullptr && slot == nullptr)
			slot = &p;
	}
	if (slot == nullptr) {
		SDL_GameControllerClose(pad);
		return;
	}
	*slot = pad;
	fprintf(stderr, "dx_host_sdl: gamepad %s\n", SDL_GameControllerName(pad));
}

void ClosePad(SDL_JoystickID id)
{
	SDL_GameController *pad = SDL_GameControllerFromInstanceID(id);
	for (SDL_GameController *&p : Pads) {
		if (p != nullptr && p == pad) {
			SDL_GameControllerClose(p);
			p = nullptr;
		}
	}
}

// All controllers merged, as sample_pads and dx_input_sticks in src/pico/dx_usb.cpp: the
// buttons of any, the triggers past the threshold on any, the sticks with the largest
// deflection.
void SamplePads()
{
	uint32_t buttons = 0;
	int trigger[2] = { 0, 0 };
	int bestLeft = 0, bestRight = 0;
	int16_t sticks[4] = { 0, 0, 0, 0 };

	for (SDL_GameController *pad : Pads) {
		if (pad == nullptr)
			continue;
		for (const auto &b : PadButtons)
			if (SDL_GameControllerGetButton(pad, b.button))
				buttons |= static_cast<uint32_t>(b.bits);
		trigger[0] = std::max<int>(trigger[0], SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERLEFT));
		trigger[1] = std::max<int>(trigger[1], SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_TRIGGERRIGHT));
		const int lx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
		const int ly = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
		const int rx = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTX);
		const int ry = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_RIGHTY);
		if (abs(lx) + abs(ly) > bestLeft) {
			bestLeft = abs(lx) + abs(ly);
			sticks[0] = static_cast<int16_t>(lx);
			sticks[1] = static_cast<int16_t>(ly);
		}
		if (abs(rx) + abs(ry) > bestRight) {
			bestRight = abs(rx) + abs(ry);
			sticks[2] = static_cast<int16_t>(rx);
			sticks[3] = static_cast<int16_t>(ry);
		}
	}
	const uint32_t changed = buttons ^ LastPadButtons;
	for (int bit = 0; bit < 32; bit++)
		if ((changed >> bit) & 1)
			Push(DX_EV_PAD_BUTTON, static_cast<uint8_t>(bit), (buttons >> bit) & 1);
	LastPadButtons = buttons;
	for (int t = 0; t < 2; t++) {
		const bool on = trigger[t] >= TriggerOn;
		if (on != LastTrigger[t])
			Push(DX_EV_TRIGGER, static_cast<uint8_t>(t), on);
		LastTrigger[t] = on;
	}
	memcpy(Sticks, sticks, sizeof(Sticks));
}

// ---- Events ------------------------------------------------------------------------------

void Pump()
{
	SDL_Event ev;
	while (SDL_PollEvent(&ev)) {
		switch (ev.type) {
		case SDL_QUIT:
			QuitRequested = true;
			break;
		case SDL_KEYDOWN:
		case SDL_KEYUP:
			if (ev.key.repeat == 0) // a USB keyboard does not repeat
				Key(ev.key.keysym.scancode, ev.type == SDL_KEYDOWN);
			break;
		case SDL_MOUSEMOTION:
			SetPointer(ev.motion.x, ev.motion.y);
			break;
		case SDL_MOUSEBUTTONDOWN:
		case SDL_MOUSEBUTTONUP:
			SetPointer(ev.button.x, ev.button.y);
			MouseButton(MouseBit(ev.button.button), ev.type == SDL_MOUSEBUTTONDOWN);
			break;
		case SDL_MOUSEWHEEL:
			Wheel += ev.wheel.direction == SDL_MOUSEWHEEL_FLIPPED ? -ev.wheel.y : ev.wheel.y;
			break;
		case SDL_CONTROLLERDEVICEADDED:
			OpenPad(ev.cdevice.which);
			break;
		case SDL_CONTROLLERDEVICEREMOVED:
			ClosePad(ev.cdevice.which);
			break;
		case SDL_WINDOWEVENT:
			if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
				ReleaseAll();
			break;
		}
	}
	SamplePads();
}

} // namespace

extern "C" {

bool dx_sdl_init(void)
{
	return Init();
}

void dx_sdl_present(const uint8_t *pixels, int pitch, int width, int height, const uint8_t *rgbx)
{
	Present(pixels, pitch, width, height, rgbx);
}

void dx_sdl_pump(void)
{
	Pump();
}

bool dx_sdl_quit_requested(void)
{
	const bool quit = QuitRequested;
	QuitRequested = false;
	return quit;
}

bool dx_sdl_mouse_position(int *x, int *y)
{
	if (!MouseMoved)
		return false;
	MouseMoved = false;
	*x = MouseX;
	*y = MouseY;
	return true;
}

int dx_sdl_audio_queued(void)
{
	return AudioQueued();
}

void dx_sdl_queue_audio(const int16_t *stereo, int frames)
{
	QueueAudio(stereo, frames);
}

void dx_sdl_clear_audio(void)
{
	ClearAudio();
}

// ---- The raw input of src/pico/dx_input.h, read by the firmware's dx_input_poll ----------

bool dx_input_pop(dx_input_event_t *ev)
{
	if (RingTail == RingHead)
		return false;
	*ev = Ring[RingTail++ % RingSize];
	return true;
}

// The position is absolute (dx_sdl_mouse_position): no relative motion.
void dx_input_mouse(int *dx, int *dy, int *wheel)
{
	*dx = 0;
	*dy = 0;
	*wheel = Wheel;
	Wheel = 0;
}

void dx_input_sticks(int16_t axes[4])
{
	memcpy(axes, Sticks, sizeof(Sticks));
}

uint32_t dx_wiipad_buttons(void)
{
	return 0;
}

} // extern "C"
