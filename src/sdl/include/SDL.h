// SDL.h - the subset of the SDL 1.2 API that DevilutionX 1.5.5 uses (USE_SDL1, NONET).
//
// Written for pico-lutionX from the documented SDL 1.2 interface; it is not derived from
// the SDL sources. Names, structure layouts and numeric values follow SDL 1.2 so that the
// engine's SDL1 code paths (and its SDL2 -> SDL1 key conversion for demos) work unchanged.
// The implementation lives in src/sdl/*.cpp on top of the backend in sdl_backend.h.
#pragma once

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
#define SDLSUB_EXTERN_C extern "C"
extern "C" {
#else
#define SDLSUB_EXTERN_C
#endif

#define SDLCALL
#define DECLSPEC

// ---- Version -------------------------------------------------------------------------

#define SDL_MAJOR_VERSION 1
#define SDL_MINOR_VERSION 2
#define SDL_PATCHLEVEL 15

typedef struct SDL_version {
	uint8_t major;
	uint8_t minor;
	uint8_t patch;
} SDL_version;

#define SDL_VERSION(x)                    \
	{                                     \
		(x)->major = SDL_MAJOR_VERSION;   \
		(x)->minor = SDL_MINOR_VERSION;   \
		(x)->patch = SDL_PATCHLEVEL;      \
	}
#define SDL_VERSIONNUM(X, Y, Z) ((X)*1000 + (Y)*100 + (Z))
#define SDL_COMPILEDVERSION SDL_VERSIONNUM(SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL)
#define SDL_VERSION_ATLEAST(X, Y, Z) (SDL_COMPILEDVERSION >= SDL_VERSIONNUM(X, Y, Z))

const SDL_version *SDL_Linked_Version(void);

// ---- Basic types ---------------------------------------------------------------------

typedef uint8_t Uint8;
typedef int8_t Sint8;
typedef uint16_t Uint16;
typedef int16_t Sint16;
typedef uint32_t Uint32;
typedef int32_t Sint32;
typedef uint64_t Uint64;
typedef int64_t Sint64;

typedef enum {
	SDL_FALSE = 0,
	SDL_TRUE = 1
} SDL_bool;

#define SDL_arraysize(array) (sizeof(array) / sizeof(array[0]))
#define SDL_TABLESIZE(table) SDL_arraysize(table)

#define SDL_malloc malloc
#define SDL_calloc calloc
#define SDL_realloc realloc
#define SDL_free free
#define SDL_memset memset
#define SDL_memcpy memcpy
#define SDL_memmove memmove
#define SDL_memcmp memcmp
#define SDL_strlen strlen
#define SDL_strdup strdup
#define SDL_strchr strchr
#define SDL_strrchr strrchr
#define SDL_strstr strstr
#define SDL_strcmp strcmp
#define SDL_strncmp strncmp
#define SDL_strcasecmp strcasecmp
#define SDL_strncasecmp strncasecmp
#define SDL_snprintf snprintf
#define SDL_vsnprintf vsnprintf
#define SDL_atoi atoi
#define SDL_strtol strtol
#define SDL_getenv getenv
#define SDL_abs abs
#define SDL_min(x, y) (((x) < (y)) ? (x) : (y))
#define SDL_max(x, y) (((x) > (y)) ? (x) : (y))
#define SDL_toupper toupper
#define SDL_tolower tolower

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen);
size_t SDL_strlcat(char *dst, const char *src, size_t maxlen);
char *SDL_iconv_string(const char *tocode, const char *fromcode, const char *inbuf, size_t inbytesleft);

// ---- Byte order ----------------------------------------------------------------------

#define SDL_LIL_ENDIAN 1234
#define SDL_BIG_ENDIAN 4321
#if defined(__BYTE_ORDER__) && __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
#define SDL_BYTEORDER SDL_BIG_ENDIAN
#else
#define SDL_BYTEORDER SDL_LIL_ENDIAN
#endif

static inline Uint16 SDL_Swap16(Uint16 x) { return (Uint16)((x << 8) | (x >> 8)); }
static inline Uint32 SDL_Swap32(Uint32 x) { return __builtin_bswap32(x); }
static inline Uint64 SDL_Swap64(Uint64 x) { return __builtin_bswap64(x); }

#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define SDL_SwapLE16(X) (X)
#define SDL_SwapLE32(X) (X)
#define SDL_SwapLE64(X) (X)
#define SDL_SwapBE16(X) SDL_Swap16(X)
#define SDL_SwapBE32(X) SDL_Swap32(X)
#define SDL_SwapBE64(X) SDL_Swap64(X)
#else
#define SDL_SwapLE16(X) SDL_Swap16(X)
#define SDL_SwapLE32(X) SDL_Swap32(X)
#define SDL_SwapLE64(X) SDL_Swap64(X)
#define SDL_SwapBE16(X) (X)
#define SDL_SwapBE32(X) (X)
#define SDL_SwapBE64(X) (X)
#endif

// ---- Errors --------------------------------------------------------------------------

typedef enum {
	SDL_ENOMEM,
	SDL_EFREAD,
	SDL_EFWRITE,
	SDL_EFSEEK,
	SDL_UNSUPPORTED,
	SDL_LASTERROR
} SDL_errorcode;

int SDL_SetError(const char *fmt, ...);
char *SDL_GetError(void);
void SDL_ClearError(void);
void SDL_Error(SDL_errorcode code);
#define SDL_OutOfMemory() SDL_Error(SDL_ENOMEM)
#define SDL_Unsupported() SDL_Error(SDL_UNSUPPORTED)

// ---- Initialisation ------------------------------------------------------------------

#define SDL_INIT_TIMER 0x00000001
#define SDL_INIT_AUDIO 0x00000010
#define SDL_INIT_VIDEO 0x00000020
#define SDL_INIT_CDROM 0x00000100
#define SDL_INIT_JOYSTICK 0x00000200
#define SDL_INIT_NOPARACHUTE 0x00100000
#define SDL_INIT_EVENTTHREAD 0x01000000
#define SDL_INIT_EVERYTHING 0x0000FFFF

int SDL_Init(Uint32 flags);
int SDL_InitSubSystem(Uint32 flags);
void SDL_QuitSubSystem(Uint32 flags);
Uint32 SDL_WasInit(Uint32 flags);
void SDL_Quit(void);

// ---- Timer ---------------------------------------------------------------------------

Uint32 SDL_GetTicks(void);
void SDL_Delay(Uint32 ms);

// ---- Threads and mutexes (single-threaded port: threads are not available) ------------

typedef struct SDL_Thread SDL_Thread;
typedef struct SDL_mutex SDL_mutex;

SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data);
void SDL_WaitThread(SDL_Thread *thread, int *status);
Uint32 SDL_ThreadID(void);
Uint32 SDL_GetThreadID(SDL_Thread *thread);

SDL_mutex *SDL_CreateMutex(void);
void SDL_DestroyMutex(SDL_mutex *mutex);
int SDL_mutexP(SDL_mutex *mutex);
int SDL_mutexV(SDL_mutex *mutex);
#define SDL_LockMutex(m) SDL_mutexP(m)
#define SDL_UnlockMutex(m) SDL_mutexV(m)

// ---- Video ---------------------------------------------------------------------------

typedef struct SDL_Rect {
	Sint16 x, y;
	Uint16 w, h;
} SDL_Rect;

typedef struct SDL_Color {
	Uint8 r;
	Uint8 g;
	Uint8 b;
	Uint8 unused;
} SDL_Color;
#define SDL_Colour SDL_Color

typedef struct SDL_Palette {
	int ncolors;
	SDL_Color *colors;
} SDL_Palette;

typedef struct SDL_PixelFormat {
	SDL_Palette *palette;
	Uint8 BitsPerPixel;
	Uint8 BytesPerPixel;
	Uint8 Rloss, Gloss, Bloss, Aloss;
	Uint8 Rshift, Gshift, Bshift, Ashift;
	Uint32 Rmask, Gmask, Bmask, Amask;
	Uint32 colorkey;
	Uint8 alpha;
} SDL_PixelFormat;

typedef struct SDL_Surface {
	Uint32 flags;
	SDL_PixelFormat *format;
	int w, h;
	Uint16 pitch;
	void *pixels;
	int offset;
	void *hwdata;
	SDL_Rect clip_rect;
	Uint32 unused1;
	Uint32 locked;
	void *map;
	unsigned int format_version;
	int refcount;
} SDL_Surface;

#define SDL_SWSURFACE 0x00000000
#define SDL_HWSURFACE 0x00000001
#define SDL_ASYNCBLIT 0x00000004
#define SDL_ANYFORMAT 0x10000000
#define SDL_HWPALETTE 0x20000000
#define SDL_DOUBLEBUF 0x40000000
#define SDL_FULLSCREEN 0x80000000
#define SDL_OPENGL 0x00000002
#define SDL_OPENGLBLIT 0x0000000A
#define SDL_RESIZABLE 0x00000010
#define SDL_NOFRAME 0x00000020
#define SDL_HWACCEL 0x00000100
#define SDL_SRCCOLORKEY 0x00001000
#define SDL_RLEACCELOK 0x00002000
#define SDL_RLEACCEL 0x00004000
#define SDL_SRCALPHA 0x00010000
#define SDL_PREALLOC 0x01000000

#define SDL_MUSTLOCK(surface) ((surface)->offset || (((surface)->flags & (SDL_HWSURFACE | SDL_ASYNCBLIT | SDL_RLEACCEL)) != 0))

#define SDL_LOGPAL 0x01
#define SDL_PHYSPAL 0x02

typedef struct SDL_VideoInfo {
	Uint32 hw_available : 1;
	Uint32 wm_available : 1;
	Uint32 UnusedBits1 : 6;
	Uint32 UnusedBits2 : 1;
	Uint32 blit_hw : 1;
	Uint32 blit_hw_CC : 1;
	Uint32 blit_hw_A : 1;
	Uint32 blit_sw : 1;
	Uint32 blit_sw_CC : 1;
	Uint32 blit_sw_A : 1;
	Uint32 blit_fill : 1;
	Uint32 UnusedBits3 : 16;
	Uint32 video_mem;
	SDL_PixelFormat *vfmt;
	int current_w;
	int current_h;
} SDL_VideoInfo;

typedef enum {
	SDL_GRAB_QUERY = -1,
	SDL_GRAB_OFF = 0,
	SDL_GRAB_ON = 1,
	SDL_GRAB_FULLSCREEN
} SDL_GrabMode;

int SDL_VideoInit(const char *driver_name, Uint32 flags);
void SDL_VideoQuit(void);
SDL_Surface *SDL_SetVideoMode(int width, int height, int bpp, Uint32 flags);
SDL_Surface *SDL_GetVideoSurface(void);
const SDL_VideoInfo *SDL_GetVideoInfo(void);
SDL_Rect **SDL_ListModes(SDL_PixelFormat *format, Uint32 flags);
int SDL_VideoModeOK(int width, int height, int bpp, Uint32 flags);
void SDL_WM_SetCaption(const char *title, const char *icon);
SDL_GrabMode SDL_WM_GrabInput(SDL_GrabMode mode);
int SDL_Flip(SDL_Surface *screen);
// pico-lutionX extension: wait until the screen surface may be drawn into again after
// SDL_Flip (the board copies the frame in the background). SDL_FillRect, blits into the
// screen and SDL_LockSurface wait by themselves.
void SDLDX_WaitScreen(void);
void SDL_UpdateRects(SDL_Surface *screen, int numrects, SDL_Rect *rects);
void SDL_UpdateRect(SDL_Surface *screen, Sint32 x, Sint32 y, Uint32 w, Uint32 h);
int SDL_SetColors(SDL_Surface *surface, SDL_Color *colors, int firstcolor, int ncolors);
int SDL_SetPalette(SDL_Surface *surface, int flags, SDL_Color *colors, int firstcolor, int ncolors);
Uint32 SDL_MapRGB(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b);
Uint32 SDL_MapRGBA(const SDL_PixelFormat *format, Uint8 r, Uint8 g, Uint8 b, Uint8 a);

SDL_Surface *SDL_CreateRGBSurface(Uint32 flags, int width, int height, int depth,
    Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
SDL_Surface *SDL_CreateRGBSurfaceFrom(void *pixels, int width, int height, int depth, int pitch,
    Uint32 Rmask, Uint32 Gmask, Uint32 Bmask, Uint32 Amask);
void SDL_FreeSurface(SDL_Surface *surface);
SDL_Surface *SDL_ConvertSurface(SDL_Surface *src, SDL_PixelFormat *fmt, Uint32 flags);
int SDL_LockSurface(SDL_Surface *surface);
void SDL_UnlockSurface(SDL_Surface *surface);
int SDL_UpperBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
int SDL_LowerBlit(SDL_Surface *src, SDL_Rect *srcrect, SDL_Surface *dst, SDL_Rect *dstrect);
#define SDL_BlitSurface SDL_UpperBlit
int SDL_FillRect(SDL_Surface *dst, SDL_Rect *dstrect, Uint32 color);
SDL_bool SDL_SetClipRect(SDL_Surface *surface, const SDL_Rect *rect);
void SDL_GetClipRect(SDL_Surface *surface, SDL_Rect *rect);
int SDL_SetColorKey(SDL_Surface *surface, Uint32 flag, Uint32 key);
int SDL_ShowCursor(int toggle);
void SDL_WarpMouse(Uint16 x, Uint16 y);

#define SDL_QUERY -1
#define SDL_IGNORE 0
#define SDL_DISABLE 0
#define SDL_ENABLE 1

// ---- Keyboard ------------------------------------------------------------------------

typedef enum {
	SDLK_UNKNOWN = 0,
	SDLK_FIRST = 0,
	SDLK_BACKSPACE = 8,
	SDLK_TAB = 9,
	SDLK_CLEAR = 12,
	SDLK_RETURN = 13,
	SDLK_PAUSE = 19,
	SDLK_ESCAPE = 27,
	SDLK_SPACE = 32,
	SDLK_EXCLAIM = 33,
	SDLK_QUOTEDBL = 34,
	SDLK_HASH = 35,
	SDLK_DOLLAR = 36,
	SDLK_AMPERSAND = 38,
	SDLK_QUOTE = 39,
	SDLK_LEFTPAREN = 40,
	SDLK_RIGHTPAREN = 41,
	SDLK_ASTERISK = 42,
	SDLK_PLUS = 43,
	SDLK_COMMA = 44,
	SDLK_MINUS = 45,
	SDLK_PERIOD = 46,
	SDLK_SLASH = 47,
	SDLK_0 = 48,
	SDLK_1 = 49,
	SDLK_2 = 50,
	SDLK_3 = 51,
	SDLK_4 = 52,
	SDLK_5 = 53,
	SDLK_6 = 54,
	SDLK_7 = 55,
	SDLK_8 = 56,
	SDLK_9 = 57,
	SDLK_COLON = 58,
	SDLK_SEMICOLON = 59,
	SDLK_LESS = 60,
	SDLK_EQUALS = 61,
	SDLK_GREATER = 62,
	SDLK_QUESTION = 63,
	SDLK_AT = 64,
	SDLK_LEFTBRACKET = 91,
	SDLK_BACKSLASH = 92,
	SDLK_RIGHTBRACKET = 93,
	SDLK_CARET = 94,
	SDLK_UNDERSCORE = 95,
	SDLK_BACKQUOTE = 96,
	SDLK_a = 97,
	SDLK_b = 98,
	SDLK_c = 99,
	SDLK_d = 100,
	SDLK_e = 101,
	SDLK_f = 102,
	SDLK_g = 103,
	SDLK_h = 104,
	SDLK_i = 105,
	SDLK_j = 106,
	SDLK_k = 107,
	SDLK_l = 108,
	SDLK_m = 109,
	SDLK_n = 110,
	SDLK_o = 111,
	SDLK_p = 112,
	SDLK_q = 113,
	SDLK_r = 114,
	SDLK_s = 115,
	SDLK_t = 116,
	SDLK_u = 117,
	SDLK_v = 118,
	SDLK_w = 119,
	SDLK_x = 120,
	SDLK_y = 121,
	SDLK_z = 122,
	SDLK_DELETE = 127,
	SDLK_WORLD_0 = 160,
	SDLK_WORLD_95 = 255,
	SDLK_KP0 = 256,
	SDLK_KP1 = 257,
	SDLK_KP2 = 258,
	SDLK_KP3 = 259,
	SDLK_KP4 = 260,
	SDLK_KP5 = 261,
	SDLK_KP6 = 262,
	SDLK_KP7 = 263,
	SDLK_KP8 = 264,
	SDLK_KP9 = 265,
	SDLK_KP_PERIOD = 266,
	SDLK_KP_DIVIDE = 267,
	SDLK_KP_MULTIPLY = 268,
	SDLK_KP_MINUS = 269,
	SDLK_KP_PLUS = 270,
	SDLK_KP_ENTER = 271,
	SDLK_KP_EQUALS = 272,
	SDLK_UP = 273,
	SDLK_DOWN = 274,
	SDLK_RIGHT = 275,
	SDLK_LEFT = 276,
	SDLK_INSERT = 277,
	SDLK_HOME = 278,
	SDLK_END = 279,
	SDLK_PAGEUP = 280,
	SDLK_PAGEDOWN = 281,
	SDLK_F1 = 282,
	SDLK_F2 = 283,
	SDLK_F3 = 284,
	SDLK_F4 = 285,
	SDLK_F5 = 286,
	SDLK_F6 = 287,
	SDLK_F7 = 288,
	SDLK_F8 = 289,
	SDLK_F9 = 290,
	SDLK_F10 = 291,
	SDLK_F11 = 292,
	SDLK_F12 = 293,
	SDLK_F13 = 294,
	SDLK_F14 = 295,
	SDLK_F15 = 296,
	SDLK_NUMLOCK = 300,
	SDLK_CAPSLOCK = 301,
	SDLK_SCROLLOCK = 302,
	SDLK_RSHIFT = 303,
	SDLK_LSHIFT = 304,
	SDLK_RCTRL = 305,
	SDLK_LCTRL = 306,
	SDLK_RALT = 307,
	SDLK_LALT = 308,
	SDLK_RMETA = 309,
	SDLK_LMETA = 310,
	SDLK_LSUPER = 311,
	SDLK_RSUPER = 312,
	SDLK_MODE = 313,
	SDLK_COMPOSE = 314,
	SDLK_HELP = 315,
	SDLK_PRINT = 316,
	SDLK_SYSREQ = 317,
	SDLK_BREAK = 318,
	SDLK_MENU = 319,
	SDLK_POWER = 320,
	SDLK_EURO = 321,
	SDLK_UNDO = 322,
	SDLK_LAST
} SDLKey;

typedef enum {
	KMOD_NONE = 0x0000,
	KMOD_LSHIFT = 0x0001,
	KMOD_RSHIFT = 0x0002,
	KMOD_LCTRL = 0x0040,
	KMOD_RCTRL = 0x0080,
	KMOD_LALT = 0x0100,
	KMOD_RALT = 0x0200,
	KMOD_LMETA = 0x0400,
	KMOD_RMETA = 0x0800,
	KMOD_NUM = 0x1000,
	KMOD_CAPS = 0x2000,
	KMOD_MODE = 0x4000,
	KMOD_RESERVED = 0x8000
} SDLMod;
#define KMOD_CTRL (KMOD_LCTRL | KMOD_RCTRL)
#define KMOD_SHIFT (KMOD_LSHIFT | KMOD_RSHIFT)
#define KMOD_ALT (KMOD_LALT | KMOD_RALT)
#define KMOD_META (KMOD_LMETA | KMOD_RMETA)

typedef struct SDL_keysym {
	Uint8 scancode;
	SDLKey sym;
	SDLMod mod;
	Uint16 unicode;
} SDL_keysym;

int SDL_EnableUNICODE(int enable);
int SDL_EnableKeyRepeat(int delay, int interval);
Uint8 *SDL_GetKeyState(int *numkeys);
SDLMod SDL_GetModState(void);
void SDL_SetModState(SDLMod modstate);
char *SDL_GetKeyName(SDLKey key);

// ---- Mouse ---------------------------------------------------------------------------

#define SDL_BUTTON(X) (1 << ((X)-1))
#define SDL_BUTTON_LEFT 1
#define SDL_BUTTON_MIDDLE 2
#define SDL_BUTTON_RIGHT 3
#define SDL_BUTTON_WHEELUP 4
#define SDL_BUTTON_WHEELDOWN 5
#define SDL_BUTTON_X1 6
#define SDL_BUTTON_X2 7
#define SDL_BUTTON_LMASK SDL_BUTTON(SDL_BUTTON_LEFT)
#define SDL_BUTTON_MMASK SDL_BUTTON(SDL_BUTTON_MIDDLE)
#define SDL_BUTTON_RMASK SDL_BUTTON(SDL_BUTTON_RIGHT)
#define SDL_BUTTON_X1MASK SDL_BUTTON(SDL_BUTTON_X1)
#define SDL_BUTTON_X2MASK SDL_BUTTON(SDL_BUTTON_X2)

Uint8 SDL_GetMouseState(int *x, int *y);
Uint8 SDL_GetRelativeMouseState(int *x, int *y);

// ---- Joystick ------------------------------------------------------------------------

typedef struct SDL_Joystick SDL_Joystick;

#define SDL_HAT_CENTERED 0x00
#define SDL_HAT_UP 0x01
#define SDL_HAT_RIGHT 0x02
#define SDL_HAT_DOWN 0x04
#define SDL_HAT_LEFT 0x08
#define SDL_HAT_RIGHTUP (SDL_HAT_RIGHT | SDL_HAT_UP)
#define SDL_HAT_RIGHTDOWN (SDL_HAT_RIGHT | SDL_HAT_DOWN)
#define SDL_HAT_LEFTUP (SDL_HAT_LEFT | SDL_HAT_UP)
#define SDL_HAT_LEFTDOWN (SDL_HAT_LEFT | SDL_HAT_DOWN)

int SDL_NumJoysticks(void);
const char *SDL_JoystickName(int device_index);
SDL_Joystick *SDL_JoystickOpen(int device_index);
int SDL_JoystickOpened(int device_index);
int SDL_JoystickIndex(SDL_Joystick *joystick);
int SDL_JoystickNumAxes(SDL_Joystick *joystick);
int SDL_JoystickNumBalls(SDL_Joystick *joystick);
int SDL_JoystickNumHats(SDL_Joystick *joystick);
int SDL_JoystickNumButtons(SDL_Joystick *joystick);
void SDL_JoystickUpdate(void);
int SDL_JoystickEventState(int state);
Sint16 SDL_JoystickGetAxis(SDL_Joystick *joystick, int axis);
Uint8 SDL_JoystickGetHat(SDL_Joystick *joystick, int hat);
Uint8 SDL_JoystickGetButton(SDL_Joystick *joystick, int button);
void SDL_JoystickClose(SDL_Joystick *joystick);

// ---- Events --------------------------------------------------------------------------

typedef enum {
	SDL_NOEVENT = 0,
	SDL_ACTIVEEVENT,
	SDL_KEYDOWN,
	SDL_KEYUP,
	SDL_MOUSEMOTION,
	SDL_MOUSEBUTTONDOWN,
	SDL_MOUSEBUTTONUP,
	SDL_JOYAXISMOTION,
	SDL_JOYBALLMOTION,
	SDL_JOYHATMOTION,
	SDL_JOYBUTTONDOWN,
	SDL_JOYBUTTONUP,
	SDL_QUIT,
	SDL_SYSWMEVENT,
	SDL_EVENT_RESERVEDA,
	SDL_EVENT_RESERVEDB,
	SDL_VIDEORESIZE,
	SDL_VIDEOEXPOSE,
	SDL_EVENT_RESERVED2,
	SDL_EVENT_RESERVED3,
	SDL_EVENT_RESERVED4,
	SDL_EVENT_RESERVED5,
	SDL_EVENT_RESERVED6,
	SDL_EVENT_RESERVED7,
	SDL_USEREVENT = 24,
	SDL_NUMEVENTS = 32
} SDL_EventType;

#define SDL_PRESSED 1
#define SDL_RELEASED 0

#define SDL_APPMOUSEFOCUS 0x01
#define SDL_APPINPUTFOCUS 0x02
#define SDL_APPACTIVE 0x04

typedef struct SDL_ActiveEvent {
	Uint8 type;
	Uint8 gain;
	Uint8 state;
} SDL_ActiveEvent;

typedef struct SDL_KeyboardEvent {
	Uint8 type;
	Uint8 which;
	Uint8 state;
	SDL_keysym keysym;
} SDL_KeyboardEvent;

typedef struct SDL_MouseMotionEvent {
	Uint8 type;
	Uint8 which;
	Uint8 state;
	Uint16 x, y;
	Sint16 xrel;
	Sint16 yrel;
} SDL_MouseMotionEvent;

typedef struct SDL_MouseButtonEvent {
	Uint8 type;
	Uint8 which;
	Uint8 button;
	Uint8 state;
	Uint16 x, y;
} SDL_MouseButtonEvent;

typedef struct SDL_JoyAxisEvent {
	Uint8 type;
	Uint8 which;
	Uint8 axis;
	Sint16 value;
} SDL_JoyAxisEvent;

typedef struct SDL_JoyBallEvent {
	Uint8 type;
	Uint8 which;
	Uint8 ball;
	Sint16 xrel;
	Sint16 yrel;
} SDL_JoyBallEvent;

typedef struct SDL_JoyHatEvent {
	Uint8 type;
	Uint8 which;
	Uint8 hat;
	Uint8 value;
} SDL_JoyHatEvent;

typedef struct SDL_JoyButtonEvent {
	Uint8 type;
	Uint8 which;
	Uint8 button;
	Uint8 state;
} SDL_JoyButtonEvent;

typedef struct SDL_ResizeEvent {
	Uint8 type;
	int w;
	int h;
} SDL_ResizeEvent;

typedef struct SDL_ExposeEvent {
	Uint8 type;
} SDL_ExposeEvent;

typedef struct SDL_QuitEvent {
	Uint8 type;
} SDL_QuitEvent;

typedef struct SDL_UserEvent {
	Uint8 type;
	int code;
	void *data1;
	void *data2;
} SDL_UserEvent;

typedef struct SDL_SysWMmsg SDL_SysWMmsg;
typedef struct SDL_SysWMEvent {
	Uint8 type;
	SDL_SysWMmsg *msg;
} SDL_SysWMEvent;

typedef union SDL_Event {
	Uint8 type;
	SDL_ActiveEvent active;
	SDL_KeyboardEvent key;
	SDL_MouseMotionEvent motion;
	SDL_MouseButtonEvent button;
	SDL_JoyAxisEvent jaxis;
	SDL_JoyBallEvent jball;
	SDL_JoyHatEvent jhat;
	SDL_JoyButtonEvent jbutton;
	SDL_ResizeEvent resize;
	SDL_ExposeEvent expose;
	SDL_QuitEvent quit;
	SDL_UserEvent user;
	SDL_SysWMEvent syswm;
} SDL_Event;

#define SDL_EVENTMASK(X) (1 << (X))
#define SDL_ALLEVENTS 0xFFFFFFFF

void SDL_PumpEvents(void);
int SDL_PollEvent(SDL_Event *event);
int SDL_WaitEvent(SDL_Event *event);
int SDL_PushEvent(SDL_Event *event);
Uint8 SDL_GetAppState(void);
Uint8 SDL_EventState(Uint8 type, int state);

// ---- Audio formats (the port plays audio through src/aulib, not SDL) -------------------

#define AUDIO_U8 0x0008
#define AUDIO_S8 0x8008
#define AUDIO_U16LSB 0x0010
#define AUDIO_S16LSB 0x8010
#define AUDIO_U16MSB 0x1010
#define AUDIO_S16MSB 0x9010
#define AUDIO_U16 AUDIO_U16LSB
#define AUDIO_S16 AUDIO_S16LSB
#if SDL_BYTEORDER == SDL_LIL_ENDIAN
#define AUDIO_U16SYS AUDIO_U16LSB
#define AUDIO_S16SYS AUDIO_S16LSB
#else
#define AUDIO_U16SYS AUDIO_U16MSB
#define AUDIO_S16SYS AUDIO_S16MSB
#endif

// ---- RWops ---------------------------------------------------------------------------

#define RW_SEEK_SET 0
#define RW_SEEK_CUR 1
#define RW_SEEK_END 2

typedef struct SDL_RWops {
	int (*seek)(struct SDL_RWops *context, int offset, int whence);
	int (*read)(struct SDL_RWops *context, void *ptr, int size, int maxnum);
	int (*write)(struct SDL_RWops *context, const void *ptr, int size, int num);
	int (*close)(struct SDL_RWops *context);
	Uint32 type;
	union {
		struct {
			int autoclose;
			FILE *fp;
		} stdio;
		struct {
			Uint8 *base;
			Uint8 *here;
			Uint8 *stop;
		} mem;
		struct {
			void *data1;
		} unknown;
	} hidden;
} SDL_RWops;

SDL_RWops *SDL_RWFromFile(const char *file, const char *mode);
SDL_RWops *SDL_RWFromFP(FILE *fp, int autoclose);
SDL_RWops *SDL_RWFromMem(void *mem, int size);
SDL_RWops *SDL_RWFromConstMem(const void *mem, int size);
SDL_RWops *SDL_AllocRW(void);
void SDL_FreeRW(SDL_RWops *area);

#define SDL_RWseek(ctx, offset, whence) (ctx)->seek(ctx, offset, whence)
#define SDL_RWtell(ctx) (ctx)->seek(ctx, 0, RW_SEEK_CUR)
#define SDL_RWread(ctx, ptr, size, n) (ctx)->read(ctx, ptr, size, n)
#define SDL_RWwrite(ctx, ptr, size, n) (ctx)->write(ctx, ptr, size, n)
#define SDL_RWclose(ctx) (ctx)->close(ctx)

#ifdef __cplusplus
}
#endif
