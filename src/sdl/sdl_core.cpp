// sdl_core.cpp - initialisation, errors, timer, threads, mutexes and string helpers of the
// pico-lutionX SDL 1.2 subset.
#include "SDL.h"
#include "sdl_backend.h"

namespace {

Uint32 InitFlags;
char ErrorText[256];

struct DummyMutex {
	int locked;
};

} // namespace

extern "C" {

const SDL_version *SDL_Linked_Version(void)
{
	static const SDL_version version = { SDL_MAJOR_VERSION, SDL_MINOR_VERSION, SDL_PATCHLEVEL };
	return &version;
}

int SDL_SetError(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vsnprintf(ErrorText, sizeof(ErrorText), fmt, ap);
	va_end(ap);
	return -1;
}

char *SDL_GetError(void)
{
	return ErrorText;
}

void SDL_ClearError(void)
{
	ErrorText[0] = '\0';
}

void SDL_Error(SDL_errorcode code)
{
	switch (code) {
	case SDL_ENOMEM:
		SDL_SetError("Out of memory");
		break;
	case SDL_EFREAD:
		SDL_SetError("Error reading from datastream");
		break;
	case SDL_EFWRITE:
		SDL_SetError("Error writing to datastream");
		break;
	case SDL_EFSEEK:
		SDL_SetError("Error seeking in datastream");
		break;
	default:
		SDL_SetError("Unknown SDL error");
		break;
	}
}

int SDL_Init(Uint32 flags)
{
	InitFlags |= flags;
	return 0;
}

int SDL_InitSubSystem(Uint32 flags)
{
	InitFlags |= flags;
	return 0;
}

void SDL_QuitSubSystem(Uint32 flags)
{
	InitFlags &= ~flags;
}

Uint32 SDL_WasInit(Uint32 flags)
{
	return InitFlags & (flags != 0 ? flags : SDL_INIT_EVERYTHING);
}

void SDL_Quit(void)
{
	InitFlags = 0;
}

Uint32 SDL_GetTicks(void)
{
	return sdlb_ticks();
}

void SDL_Delay(Uint32 ms)
{
	sdlb_delay(ms);
}

// The port is single-threaded. DevilutionX only starts a thread for multiplayer
// (nthread.cpp), which NONET builds never reach.
SDL_Thread *SDL_CreateThread(int (*fn)(void *), void *data)
{
	(void)fn;
	(void)data;
	SDL_SetError("Threads are not supported");
	return nullptr;
}

void SDL_WaitThread(SDL_Thread *thread, int *status)
{
	(void)thread;
	if (status != nullptr)
		*status = 0;
}

Uint32 SDL_ThreadID(void)
{
	return 1;
}

Uint32 SDL_GetThreadID(SDL_Thread *thread)
{
	(void)thread;
	return 1;
}

SDL_mutex *SDL_CreateMutex(void)
{
	return reinterpret_cast<SDL_mutex *>(calloc(1, sizeof(DummyMutex)));
}

void SDL_DestroyMutex(SDL_mutex *mutex)
{
	free(mutex);
}

int SDL_mutexP(SDL_mutex *mutex)
{
	if (mutex == nullptr)
		return SDL_SetError("Passed a NULL mutex");
	reinterpret_cast<DummyMutex *>(mutex)->locked++;
	return 0;
}

int SDL_mutexV(SDL_mutex *mutex)
{
	if (mutex == nullptr)
		return SDL_SetError("Passed a NULL mutex");
	reinterpret_cast<DummyMutex *>(mutex)->locked--;
	return 0;
}

size_t SDL_strlcpy(char *dst, const char *src, size_t maxlen)
{
	size_t srclen = strlen(src);
	if (maxlen > 0) {
		size_t len = srclen < maxlen - 1 ? srclen : maxlen - 1;
		memcpy(dst, src, len);
		dst[len] = '\0';
	}
	return srclen;
}

size_t SDL_strlcat(char *dst, const char *src, size_t maxlen)
{
	size_t dstlen = strnlen(dst, maxlen);
	size_t srclen = strlen(src);
	if (dstlen < maxlen)
		SDL_strlcpy(dst + dstlen, src, maxlen - dstlen);
	return dstlen + srclen;
}

// Only UTF-8 text passes through here; return a copy.
char *SDL_iconv_string(const char *tocode, const char *fromcode, const char *inbuf, size_t inbytesleft)
{
	(void)tocode;
	(void)fromcode;
	char *out = static_cast<char *>(malloc(inbytesleft + 1));
	if (out == nullptr)
		return nullptr;
	memcpy(out, inbuf, inbytesleft);
	out[inbytesleft] = '\0';
	return out;
}

} // extern "C"
