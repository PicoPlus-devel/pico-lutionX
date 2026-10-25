// dx_backend.cpp -- the SDL 1.2 subset's platform backend on the board (src/sdl/sdl_backend.h),
// and the entry into the engine.

#include "pico/stdlib.h"
#include "hardware/xip_cache.h"

#include "diablo.h"
#include "dx_heap.h"
#include "sdl_backend.h"

#include "dx_audio.h"
#include "dx_display.h"
#include "dx_flash.h"
#include "dx_input.h"
#include "dx_platform.h"

extern "C" {

// Milliseconds from a 32-bit timer read without a 64-bit division (pico-quake2's
// QG_Milliseconds; the engine asks for the time many times per frame).
Uint32 sdlb_ticks(void)
{
	static uint32_t last_us, ms, rem_us;
	uint32_t now = time_us_32();

	rem_us += now - last_us;
	last_us = now;
	ms += rem_us / 1000;
	rem_us %= 1000;
	return ms;
}

// Waiting keeps the sound mixed.
void sdlb_delay(Uint32 ms)
{
	const uint32_t start = time_us_32();
	do {
		dx_audio_update();
		sleep_us(1000);
	} while (time_us_32() - start < ms * 1000u);
}

// The engine's back buffer, in PSRAM, which the engine and dx_display_present use through
// the uncached XIP window (0x15000000 for PSRAM, instead of the cached 0x11000000):
//   - a cached write that misses first reads the whole 8-byte line from the PSRAM and
//     writes it back later; an uncached write goes straight to the PSRAM, and writes that
//     follow on in address are chained into the same QSPI transfer (the QMI's chip select
//     cooldown), so a row of pixels costs one burst;
//   - the 300 KB drawn and copied every frame no longer flow through the 16 KB XIP cache
//     that code, the flash asset cache and the rest of PSRAM share.
// The buffer is only ever used through the uncached window. calloc's zeroes, and any dirty
// lines left from earlier use of that memory, are written back before the first uncached
// access, so no later eviction of a cached line can overwrite the picture.
#define DX_UNCACHED_OFFSET (0x15000000u - 0x11000000u)

void *sdlb_screen_alloc(int width, int height, int *pitch)
{
	int previous = dx_heap_set_category(DXH_SCREEN);
	auto *pixels = static_cast<uint8_t *>(dx_calloc(static_cast<size_t>(width) * height, 1));
	dx_heap_set_category(previous);
	if (pixels == nullptr)
		return nullptr;
	xip_cache_clean_all();
	*pitch = width;
	return pixels + DX_UNCACHED_OFFSET;
}

void sdlb_screen_free(void *pixels)
{
	if (pixels != nullptr)
		dx_free(static_cast<uint8_t *>(pixels) - DX_UNCACHED_OFFSET);
}

void sdlb_present(const Uint8 *pixels, int pitch, int width, int height)
{
	dx_audio_update();
	dx_display_present(pixels, pitch, width, height);
	dx_audio_update();
	dx_flash_idle();
	dx_report_status();
}

void sdlb_screen_wait(void)
{
	dx_display_wait_copy();
}

void sdlb_set_palette(const SDL_Color *colors, int first, int count)
{
	dx_display_set_palette(first, count, reinterpret_cast<const uint8_t *>(colors));
}

void sdlb_poll(void)
{
	dx_audio_update();
	dx_input_poll();
}

int devilution_main(int argc, char **argv)
{
	dx_input_init();
	return devilution::DiabloMain(argc, argv);
}

} // extern "C"
