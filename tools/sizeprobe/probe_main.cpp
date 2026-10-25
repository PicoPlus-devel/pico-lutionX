// probe_main.cpp - stub platform for the size probe (never run).
#include <cstdlib>
#include <sys/types.h>

#include "diablo.h"
#include "sdl_backend.h"

extern "C" {
// File system calls that the board's FatFs layer will provide.
int mkdir(const char *, mode_t) { return -1; }
ssize_t readlink(const char *, char *, size_t) { return -1; }
int truncate(const char *, off_t) { return -1; }

Uint32 sdlb_ticks(void) { return 0; }
void sdlb_delay(Uint32) { }
void *sdlb_screen_alloc(int width, int height, int *pitch)
{
	*pitch = width;
	return calloc(static_cast<size_t>(width) * height, 1);
}
void sdlb_screen_free(void *pixels) { free(pixels); }
void sdlb_present(const Uint8 *, int, int, int) { }
void sdlb_set_palette(const SDL_Color *, int, int) { }
void sdlb_poll(void) { }
}

int main(int argc, char **argv)
{
	return devilution::DiabloMain(argc, argv);
}
