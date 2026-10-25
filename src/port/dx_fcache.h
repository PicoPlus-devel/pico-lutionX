// dx_fcache.h -- flash asset cache.
//
// Read-only sprite data (dungeon tiles, monster, player, missile, object and UI sprites
// after their conversion to CLX) is kept in a flash region and used in place through the
// XIP window, so it costs no PSRAM. An asset that is not cached yet is loaded and
// converted as usual into PSRAM, then copied into the cache, and the PSRAM copy is freed.
// The cache survives power cycles; a later load of the same asset reads it from flash
// without touching the SD card.
//
// Lifetime: dx_fcache_find and dx_fcache_store hand out a reference to the entry; the
// engine releases it by freeing the pointer (src/port/dx_heap.cpp passes frees of cache
// memory to dx_fcache_release). Entries with references are never evicted.
//
// Adapted from pico-quake2's qcommon/fcache.c (same region layout and write strategy).

#ifndef DX_FCACHE_H
#define DX_FCACHE_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Platform flash access. Offsets are relative to the start of the region; erase ranges
// are multiples of DX_FC_SECTOR, program ranges multiples of DX_FC_PAGE.
#define DX_FC_SECTOR 4096
#define DX_FC_PAGE 256

typedef struct {
	unsigned char *base; // where the region can be read (the XIP window on the board)
	unsigned size;       // bytes, a multiple of 64 KB
	void (*erase)(unsigned offset, unsigned len);
	void (*program)(unsigned offset, const void *data, unsigned len);
} dx_fcache_flash_t;

typedef struct {
	unsigned entries, used_bytes, free_bytes, capacity;
	unsigned hits, misses, stores, store_failures, evictions;
	unsigned bytes_erased, bytes_programmed;
} dx_fcache_stats_t;

// Hands the region to the cache (NULL: no cache). data_id identifies the game data the
// entries are made from; a different id discards them.
void dx_fcache_set_flash(const dx_fcache_flash_t *flash, uint32_t data_id);
int dx_fcache_enabled(void);

// Returns the cached data for key and its length, or NULL. Takes a reference.
const void *dx_fcache_find(const char *key, unsigned *length);
// Copies len bytes into the cache; returns the cached copy with a reference, or NULL when
// the asset cannot be cached (no cache, key too long, no room).
const void *dx_fcache_store(const char *key, const void *data, unsigned len);
// Streams an entry of known length into the cache in pieces of any size (for files too
// large to hold in PSRAM first). dx_fcache_write_end returns the cached copy with a
// reference, or NULL on failure (also when fewer than len bytes were written).
typedef struct {
	int slot;
	unsigned offset, crc;
	unsigned len, done, carry;
	char name[64];
	unsigned char page[DX_FC_PAGE];
} dx_fcache_writer_t;

int dx_fcache_write_begin(dx_fcache_writer_t *w, const char *key, unsigned len);
void dx_fcache_write(dx_fcache_writer_t *w, const void *data, unsigned n);
const void *dx_fcache_write_end(dx_fcache_writer_t *w);

// True when p points into the cache region.
int dx_fcache_owns(const void *p);
// Drops the reference taken for the entry that starts at p.
void dx_fcache_release(const void *p);

// Starts a new level: entries used from now on count as recently used.
void dx_fcache_begin_level(void);
// Writes the directory to flash if it changed (after a level has loaded).
void dx_fcache_commit(void);
void dx_fcache_get_stats(dx_fcache_stats_t *st);
// Prints the cache contents by kind (the key up to its first ':'): bytes, bytes the engine
// holds references to (which cannot be evicted) and entries.
void dx_fcache_print_usage(void);
// Prints a map of the region, 64 KB per character: in use, evictable or free.
void dx_fcache_print_map(void);

#ifdef __cplusplus
}
#endif

#endif
