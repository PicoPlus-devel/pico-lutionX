// dx_heap.h - the engine heap of pico-lutionX.
//
// All engine allocations (malloc family via the linker's --wrap, and C++ new/delete) come
// from a TLSF pool sized like the PSRAM that is left after the engine's static data. In the
// host harness a second pool simulates the flash asset cache: allocations made while the
// "cacheable" flag is set (the final buffers of read-only sprite data) are taken from it
// when DX_FLASH_SIM=1, so the PSRAM pool shows what remains resident on the board.
//
// Allocations are also tagged with a category (set by DxHeapScope in the engine's
// loaders), so reports can say where the memory goes.
#pragma once

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

enum DxHeapCategory {
	DXH_OTHER,
	DXH_SCREEN,   // the 8 bpp back buffer (SDL screen surface)
	DXH_DEMO,     // demo playback messages (timedemo only, not normal play)
	DXH_DUNGEON,  // level tiles: dungeon CEL, MIN, TIL, SOL, AMP
	DXH_MONSTER,  // monster sprites
	DXH_PLAYER,   // player sprites
	DXH_MISSILE,  // missile and spell effect sprites
	DXH_UI,       // panels, cursors, fonts, menu art
	DXH_OBJECT,   // objects and item drop animations
	DXH_TOWNER,   // town NPC sprites
	DXH_SOUND,    // sound effects and music buffers
	DXH_MPQ,      // MPQ tables and stream buffers
	DXH_COUNT
};

// The board: hands over the heap region (PSRAM after the engine's static data). Must run
// before the first allocation, i.e. before the C++ static constructors.
void dx_heap_init(void *base, size_t size);
// Called when the heap is exhausted (bytes = 0: allocation before dx_heap_init). The
// platform shows an error; the host harness aborts. Does not return on the board.
void dx_heap_out_of_memory(size_t bytes);

// The engine's malloc family (the engine library's calls are renamed to these).
void *dx_malloc(size_t bytes);
void *dx_calloc(size_t count, size_t size);
void *dx_realloc(void *p, size_t bytes);
void dx_free(void *p);
// A heap block that owns other heap blocks and flash asset cache entries (from
// dx_fcache_find, dx_fcache_store or dx_fcache_write_end) and frees or releases them when it
// is freed: an engine buffer that is freed in one piece can own several buffers (monster sets
// loaded one animation at a time).
void *dx_heap_refblock(const void *const *refs, unsigned count);
void *dx_memalign(size_t align, size_t bytes);
int dx_posix_memalign(void **result, size_t align, size_t bytes);

// Select the category for the following allocations; returns the previous one.
int dx_heap_set_category(int category);
// Mark the following allocations as flash-cacheable (1) or not (0); returns the previous.
int dx_heap_set_cacheable(int cacheable);
// Print a report with the given label (live, peak since the previous mark, peak).
void dx_heap_mark(const char *label);
// Bytes currently allocated from the PSRAM pool, and its overall peak.
size_t dx_heap_used(void);
size_t dx_heap_peak(void);
size_t dx_heap_size(void);
// The largest block that can be allocated now (walks the heap; for decisions at level load).
size_t dx_heap_largest_free(void);

#ifdef __cplusplus
}

struct DxHeapScope {
	int previous;
	explicit DxHeapScope(int category)
	    : previous(dx_heap_set_category(category))
	{
	}
	~DxHeapScope() { dx_heap_set_category(previous); }
	DxHeapScope(const DxHeapScope &) = delete;
	DxHeapScope &operator=(const DxHeapScope &) = delete;
};

struct DxCacheableScope {
	int previous;
	DxCacheableScope()
	    : previous(dx_heap_set_cacheable(1))
	{
	}
	~DxCacheableScope() { dx_heap_set_cacheable(previous); }
	DxCacheableScope(const DxCacheableScope &) = delete;
	DxCacheableScope &operator=(const DxCacheableScope &) = delete;
};
#endif
