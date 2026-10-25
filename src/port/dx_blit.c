// dx_blit.c -- see dx_blit.h. The profile of the board showed these loops at about 40 %
// of core0's time on a dungeon level: byte by byte, every pixel costs three accesses
// through the XIP cache (the tile in flash, the table and the back buffer in PSRAM), and
// the loop itself runs from flash. Here the loop runs from SRAM and works on runs of up to
// CHUNK words:
//   - the back buffer (read through the uncached XIP window on the board, see
//     src/pico/dx_backend.cpp) is read and written a whole run at a time, so each run is one
//     QSPI burst instead of a new transfer per word whenever reads, writes and the flash
//     reads for the tile take turns;
//   - the source is read a word at a time (unaligned loads are allowed on the Cortex-M33
//     and on the PC), the back buffer written a word at a time (aligned).

#include <stdint.h>
#include <string.h>

#include "dx_blit.h"
#include "dx_sram.h"

#define CHUNK 16 // words per run

static inline uint32_t load32(const uint8_t *p)
{
	uint32_t v;
	memcpy(&v, p, sizeof(v));
	return v;
}

static inline void store32(uint8_t *p, uint32_t v)
{
	memcpy(p, &v, sizeof(v));
}

// byte k of a little-endian word: the pixel at offset k
#define B(w, k) (((w) >> (8 * (k))) & 0xff)

static inline uint32_t map4(uint32_t w, const uint8_t *map)
{
	return map[B(w, 0)] | (uint32_t)map[B(w, 1)] << 8 | (uint32_t)map[B(w, 2)] << 16 | (uint32_t)map[B(w, 3)] << 24;
}

static inline void load_run(uint32_t *buf, const uint8_t *p, unsigned words)
{
	for (unsigned i = 0; i < words; i++)
		buf[i] = load32(p + 4 * i);
}

static inline void store_run(uint8_t *p, const uint32_t *buf, unsigned words)
{
	for (unsigned i = 0; i < words; i++)
		store32(p + 4 * i, buf[i]);
}

void DX_SRAM_CODE dx_blit_map(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *map)
{
	uint32_t buf[CHUNK];

	for (; n != 0 && ((uintptr_t)dst & 3) != 0; n--)
		*dst++ = map[*src++];
	while (n >= 4) {
		const unsigned words = n / 4 < CHUNK ? n / 4 : CHUNK;
		for (unsigned i = 0; i < words; i++)
			buf[i] = map4(load32(src + 4 * i), map);
		store_run(dst, buf, words);
		src += 4 * words;
		dst += 4 * words;
		n -= 4 * words;
	}
	for (; n != 0; n--)
		*dst++ = map[*src++];
}

void DX_SRAM_CODE dx_blit_blended(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *pal)
{
	uint32_t buf[CHUNK];

	for (; n != 0 && ((uintptr_t)dst & 3) != 0; n--, dst++)
		*dst = pal[*src++ << 8 | *dst];
	while (n >= 4) {
		const unsigned words = n / 4 < CHUNK ? n / 4 : CHUNK;
		load_run(buf, dst, words);
		for (unsigned i = 0; i < words; i++) {
			const uint32_t s = load32(src + 4 * i);
			const uint32_t d = buf[i];
			buf[i] = pal[B(s, 0) << 8 | B(d, 0)] | (uint32_t)pal[B(s, 1) << 8 | B(d, 1)] << 8
			    | (uint32_t)pal[B(s, 2) << 8 | B(d, 2)] << 16 | (uint32_t)pal[B(s, 3) << 8 | B(d, 3)] << 24;
		}
		store_run(dst, buf, words);
		src += 4 * words;
		dst += 4 * words;
		n -= 4 * words;
	}
	for (; n != 0; n--, dst++)
		*dst = pal[*src++ << 8 | *dst];
}

void DX_SRAM_CODE dx_blit_blended_map(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *map, const uint8_t *pal)
{
	uint32_t buf[CHUNK];

	for (; n != 0 && ((uintptr_t)dst & 3) != 0; n--, dst++)
		*dst = pal[*dst << 8 | map[*src++]];
	while (n >= 4) {
		const unsigned words = n / 4 < CHUNK ? n / 4 : CHUNK;
		load_run(buf, dst, words);
		for (unsigned i = 0; i < words; i++) {
			const uint32_t s = load32(src + 4 * i);
			const uint32_t d = buf[i];
			buf[i] = pal[B(d, 0) << 8 | map[B(s, 0)]] | (uint32_t)pal[B(d, 1) << 8 | map[B(s, 1)]] << 8
			    | (uint32_t)pal[B(d, 2) << 8 | map[B(s, 2)]] << 16 | (uint32_t)pal[B(d, 3) << 8 | map[B(s, 3)]] << 24;
		}
		store_run(dst, buf, words);
		src += 4 * words;
		dst += 4 * words;
		n -= 4 * words;
	}
	for (; n != 0; n--, dst++)
		*dst = pal[*dst << 8 | map[*src++]];
}

void DX_SRAM_CODE dx_double_pixels(uint8_t *dst, const uint8_t *src, unsigned n)
{
	for (; n >= 2; n -= 2, src += 2, dst += 4)
		store32(dst, src[0] * 0x0101u | src[1] * 0x01010000u);
	if (n != 0)
		dst[0] = dst[1] = src[0];
}

void DX_SRAM_CODE dx_map_in_place(uint8_t *dst, unsigned n, const uint8_t *map)
{
	uint32_t buf[CHUNK];

	for (; n != 0 && ((uintptr_t)dst & 3) != 0; n--, dst++)
		*dst = map[*dst];
	while (n >= 4) {
		const unsigned words = n / 4 < CHUNK ? n / 4 : CHUNK;
		load_run(buf, dst, words);
		for (unsigned i = 0; i < words; i++)
			buf[i] = map4(buf[i], map);
		store_run(dst, buf, words);
		dst += 4 * words;
		n -= 4 * words;
	}
	for (; n != 0; n--, dst++)
		*dst = map[*dst];
}
