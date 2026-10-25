// dx_blit.h -- the engine's per-pixel colour map loops (engine/render/blit_impl.hpp:
// lighting, blended transparency, colour translations), four pixels per step and in SRAM
// on the board (dx_blit.c). The tables are 256 bytes, or 256 x 256 (pal[a][b] at
// pal[a * 256 + b], the engine's paletteTransparencyLookup).

#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// dst[i] = map[src[i]]
void dx_blit_map(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *map);
// dst[i] = pal[src[i]][dst[i]]
void dx_blit_blended(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *pal);
// dst[i] = pal[dst[i]][map[src[i]]]
void dx_blit_blended_map(uint8_t *dst, const uint8_t *src, unsigned n, const uint8_t *map, const uint8_t *pal);
// dst[i] = map[dst[i]]
void dx_map_in_place(uint8_t *dst, unsigned n, const uint8_t *map);
// dst[2i] = dst[2i + 1] = src[i] for n source pixels (the engine's Zoom); both buffers in
// SRAM and word aligned
void dx_double_pixels(uint8_t *dst, const uint8_t *src, unsigned n);

#ifdef __cplusplus
}
#endif
