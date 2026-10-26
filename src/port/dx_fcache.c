// dx_fcache.c -- flash asset cache (see dx_fcache.h).
//
// Adapted from pico-quake2's qcommon/fcache.c. Region layout: two directory slots of
// 128 KB (alternating, the newer valid one wins, so an interrupted commit loses only the
// last level's changes), then the data area. Every asset occupies a contiguous run of
// 256-byte pages so it can be used in place; small assets share 4 KB erase sectors. NOR
// flash can program still-erased pages without an erase, so a sector is only erased when
// a new asset needs pages in it that were written before, and only if nothing live
// remains in it. The directory is mirrored in RAM (PSRAM: this file is part of the engine
// library); the page maps are rebuilt from it at boot.
//
// Placement and eviction (see allocate): entries up to 128 KB (sound effects, item and object
// sprites) fill the data area from the top, larger ones (tiles, player and monster
// animations) from the bottom. Each takes free room or room held by data unused for more
// than eight level loads, whichever lies nearest its end; only when there is neither,
// data used more recently is evicted, the oldest first (in a few tiers of age). Entries in
// use for a whole level or game so tend to gather at both ends, keeping the free and
// evictable room between them together for the next large entry. An eviction removes the
// entries in one window of the needed size, so it never leaves scattered holes.
//
// The directory is written after a level load when entries were stored or removed. A
// level load that only uses entries changes their age stamps alone; those are written
// with the next change, or after FC_STAMP_LEVELS level loads.
//
// Differences from pico-quake2: entries are protected from eviction by reference counts
// (the engine holds sprite data across levels), keys are strings of up to 59 characters
// (longer keys are shortened with a hash), there is a single kind of entry, and the
// placement and eviction above.

#include "dx_fcache.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FC_MAGIC (('C' << 24) | ('F' << 16) | ('X' << 8) | 'D')
#define FC_VERSION 2 // 2: placement from both ends (version 1 caches are discarded)
#define FC_NAMELEN 60
#define FC_SLOT_BYTES (128 * 1024)
#define FC_MAX_ENTRIES 1600 // header + entries fit one 128 KB slot
#define FC_DATA_START (2 * FC_SLOT_BYTES)
#define FC_MAX_PAGES (16 * 1024 * 1024 / DX_FC_PAGE)
#define FC_SECTOR_PAGES (DX_FC_SECTOR / DX_FC_PAGE)
#define FC_MAX_SECTORS (FC_MAX_PAGES / FC_SECTOR_PAGES)
#define FC_STAMP_LEVELS 8 // level loads after which unchanged entries' age stamps are written

// Entries up to FC_SMALL_PAGES fill the data area from the top, larger ones from the bottom.
#define FC_SMALL_PAGES (128 * 1024 / DX_FC_PAGE)
// Data unused for more than eight level loads (entry_tier 3) is evicted as readily as free
// room is used.
#define FC_OLD_TIER 3

typedef unsigned char byte;

typedef struct {
	unsigned magic, version, generation, num_entries;
	unsigned data_id; // identity of the game data the entries came from
	unsigned seq;     // level sequence at the last commit
	unsigned dir_crc; // CRC-32 of the entries
	unsigned pad[9];
} fc_header_t;

typedef struct {
	char name[FC_NAMELEN];
	unsigned offset; // from the region base, page aligned; 0 = unused slot
	unsigned length;
	unsigned crc; // CRC-32 of the data
	unsigned short flags; // on flash: 0
	unsigned short pad;
	unsigned last_seq; // level sequence that last used the entry
} fc_entry_t;

_Static_assert(sizeof(fc_header_t) + FC_MAX_ENTRIES * sizeof(fc_entry_t) <= FC_SLOT_BYTES, "directory slot");

// RAM-only entry flag
#define FCF_VERIFIED 1 // data CRC checked this boot

static const dx_fcache_flash_t *flash;
static unsigned data_identity;
static int loaded;
static fc_header_t hdr;
static fc_entry_t dir[FC_MAX_ENTRIES];
static unsigned short refs[FC_MAX_ENTRIES]; // references held by the engine (RAM only)
static int numentries;
static int active_slot;
static int dirty;        // entries stored or removed since the last commit
static int stamps_dirty; // only age stamps changed since the last commit
static unsigned cur_seq;
static unsigned data_pages;
static byte page_used[FC_MAX_PAGES / 8];  // holds live data
static byte page_blank[FC_MAX_PAGES / 8]; // erased, can be programmed as is
static byte sector_live[FC_MAX_SECTORS];  // pages with live data per sector
static unsigned short by_offset[FC_MAX_ENTRIES]; // evict_window: live entries in address order
static dx_fcache_stats_t stats;

/*
==============================================================================

CRC-32 (IEEE, reflected)

==============================================================================
*/

static unsigned crc_table[256];

static void crc_init(void)
{
	for (unsigned i = 0; i < 256; i++) {
		unsigned v = i;
		for (int k = 0; k < 8; k++)
			v = (v & 1) ? 0xedb88320 ^ (v >> 1) : v >> 1;
		crc_table[i] = v;
	}
}

static unsigned crc32(const void *data, unsigned len)
{
	const byte *p = data;
	unsigned c = 0xffffffff;

	while (len--)
		c = crc_table[(c ^ *p++) & 0xff] ^ (c >> 8);
	return ~c;
}

/*
==============================================================================

PAGE MAPS

==============================================================================
*/

#define BIT_GET(map, i) (((map)[(i) >> 3] >> ((i)&7)) & 1)
#define BIT_SET(map, i) ((map)[(i) >> 3] |= 1 << ((i)&7))
#define BIT_CLR(map, i) ((map)[(i) >> 3] &= ~(1 << ((i)&7)))

static unsigned page_of(unsigned offset)
{
	return (offset - FC_DATA_START) / DX_FC_PAGE;
}

static unsigned pages_of(unsigned length)
{
	return (length + DX_FC_PAGE - 1) / DX_FC_PAGE;
}

static void mark_pages(unsigned offset, unsigned length, int used)
{
	unsigned p = page_of(offset);
	unsigned n = pages_of(length);

	for (; n--; p++) {
		if (used) {
			if (!BIT_GET(page_used, p)) {
				BIT_SET(page_used, p);
				sector_live[p / FC_SECTOR_PAGES]++;
			}
			BIT_CLR(page_blank, p);
		} else if (BIT_GET(page_used, p)) {
			BIT_CLR(page_used, p);
			sector_live[p / FC_SECTOR_PAGES]--;
		}
	}
}

static int sector_has_live(unsigned page)
{
	return sector_live[page / FC_SECTOR_PAGES] != 0;
}

// A page can take new data if it is free and either still erased, or in a sector with
// nothing live in it (which can then be erased).
static int page_available(unsigned p)
{
	if (BIT_GET(page_used, p))
		return 0;
	return BIT_GET(page_blank, p) || !sector_has_live(p);
}

// Returns the first page of a place for n pages, or -1: with high, the highest run of
// available pages that fits, at its end; else the lowest, at its start.
static int best_fit(unsigned n, int high)
{
	unsigned run_start = 0, run = 0;
	int best = -1;

	for (unsigned p = 0; p <= data_pages; p++) {
		if (p < data_pages && page_available(p)) {
			if (!run)
				run_start = p;
			run++;
			continue;
		}
		if (run >= n) {
			best = (int)(high ? run_start + run - n : run_start);
			if (!high)
				break; // the first fitting run is the lowest
		}
		run = 0;
	}
	return best;
}

// Erases the sectors under pages [first, first+n) that hold written pages.
static void prepare_run(unsigned first, unsigned n)
{
	unsigned erase_from = 0, erase_count = 0;
	unsigned end_s = (first + n - 1) / FC_SECTOR_PAGES;

	for (unsigned s = first / FC_SECTOR_PAGES; s <= end_s + 1; s++) {
		int need = 0;

		if (s <= end_s)
			for (unsigned p = s * FC_SECTOR_PAGES; p < (s + 1) * FC_SECTOR_PAGES; p++)
				if (p >= first && p < first + n && !BIT_GET(page_blank, p))
					need = 1;
		if (need) {
			if (!erase_count)
				erase_from = s;
			erase_count++;
			continue;
		}
		if (erase_count) {
			flash->erase(FC_DATA_START + erase_from * DX_FC_SECTOR, erase_count * DX_FC_SECTOR);
			stats.bytes_erased += erase_count * DX_FC_SECTOR;
			for (unsigned p = erase_from * FC_SECTOR_PAGES; p < (erase_from + erase_count) * FC_SECTOR_PAGES; p++)
				BIT_SET(page_blank, p);
			erase_count = 0;
		}
	}
}

static void rebuild_page_maps(void)
{
	memset(page_used, 0, sizeof(page_used));
	memset(sector_live, 0, sizeof(sector_live));
	stats.used_bytes = 0;
	for (int i = 0; i < numentries; i++) {
		if (!dir[i].offset)
			continue;
		mark_pages(dir[i].offset, dir[i].length, 1);
		stats.used_bytes += dir[i].length;
	}
	// which free pages are still erased (one read of the free space per boot)
	memset(page_blank, 0, sizeof(page_blank));
	for (unsigned p = 0; p < data_pages; p++) {
		if (BIT_GET(page_used, p))
			continue;
		const uint32_t *d = (const uint32_t *)(flash->base + FC_DATA_START + p * DX_FC_PAGE);
		unsigned i;
		for (i = 0; i < DX_FC_PAGE / 4 && d[i] == 0xffffffffu; i++)
			;
		if (i == DX_FC_PAGE / 4)
			BIT_SET(page_blank, p);
	}
}

// How long ago an entry was used, in tiers: 0 in this level load, 1 in the two before it, 2
// in the six before those, 3 earlier.
static unsigned entry_tier(int i)
{
	if (dir[i].last_seq >= cur_seq)
		return 0;
	const unsigned age = cur_seq - dir[i].last_seq;
	return age <= 2 ? 1 : age <= 8 ? 2 : 3;
}

/*
==============================================================================

DIRECTORY

==============================================================================
*/

static int slot_valid(int slot, fc_header_t *h)
{
	const byte *p = flash->base + slot * FC_SLOT_BYTES;

	memcpy(h, p, sizeof(*h));
	if (h->magic != FC_MAGIC || h->version != FC_VERSION || h->num_entries > FC_MAX_ENTRIES)
		return 0;
	return crc32(p + sizeof(*h), h->num_entries * sizeof(fc_entry_t)) == h->dir_crc;
}

static void load_directory(void)
{
	fc_header_t h0, h1;
	int v0, v1;

	loaded = 1;
	crc_init();
	data_pages = (flash->size - FC_DATA_START) / DX_FC_PAGE;
	if (data_pages > FC_MAX_PAGES)
		data_pages = FC_MAX_PAGES;
	stats.capacity = data_pages * DX_FC_PAGE;

	v0 = slot_valid(0, &h0);
	v1 = slot_valid(1, &h1);
	numentries = 0;
	memset(&hdr, 0, sizeof(hdr));
	active_slot = 1; // so the first commit goes to slot 0

	if (v0 || v1) {
		active_slot = (v1 && (!v0 || h1.generation > h0.generation)) ? 1 : 0;
		hdr = active_slot ? h1 : h0;
		if (hdr.data_id != data_identity) {
			printf("dx_fcache: game data changed, discarding cached assets\n");
			hdr.num_entries = 0;
			dirty = 1;
		}
		numentries = (int)hdr.num_entries;
		memcpy(dir, flash->base + active_slot * FC_SLOT_BYTES + sizeof(hdr), numentries * sizeof(fc_entry_t));
		for (int i = 0; i < numentries; i++) {
			dir[i].flags = 0;
			if (dir[i].offset && (dir[i].offset < FC_DATA_START || dir[i].offset + dir[i].length > FC_DATA_START + data_pages * DX_FC_PAGE))
				dir[i].offset = 0; // does not fit this region (size changed)
		}
	}
	memset(refs, 0, sizeof(refs));
	hdr.data_id = data_identity;
	cur_seq = hdr.seq;
	rebuild_page_maps();
	printf("dx_fcache: %u KB region, %d cached assets, %u KB used\n",
	    flash->size / 1024, numentries, stats.used_bytes / 1024);
}

static int ready(void)
{
	if (!flash)
		return 0;
	if (!loaded)
		load_directory();
	return 1;
}

static void remove_entry(int i)
{
	if (!dir[i].offset)
		return;
	mark_pages(dir[i].offset, dir[i].length, 0);
	stats.used_bytes -= dir[i].length;
	dir[i].offset = 0;
	refs[i] = 0;
	dirty = 1;
}

// Keys longer than a directory name keep their start and get a hash of the whole key.
static void make_name(char *out, const char *key)
{
	size_t len = strlen(key);

	if (len < FC_NAMELEN) {
		memcpy(out, key, len + 1);
		return;
	}
	memcpy(out, key, FC_NAMELEN - 10);
	snprintf(out + FC_NAMELEN - 10, 10, "#%08x", crc32(key, (unsigned)len));
}

static int lookup(const char *name)
{
	for (int i = 0; i < numentries; i++)
		if (dir[i].offset && !strcmp(dir[i].name, name))
			return i;
	return -1;
}

static void touch(int i)
{
	if (dir[i].last_seq != cur_seq) {
		dir[i].last_seq = cur_seq;
		stamps_dirty = 1;
	}
}

/*
==============================================================================

ALLOCATION

==============================================================================
*/

static int is_protected(int i)
{
	return refs[i] != 0;
}

static void evict(int i)
{
	if (entry_tier(i) <= 1)
		stats.recent_evictions++;
	stats.evictions++;
	remove_entry(i);
}

// Evicts one entry nothing refers to (when the directory is full): the oldest, the
// smallest of those.
static int evict_one(void)
{
	int best = -1;

	for (int i = 0; i < numentries; i++) {
		if (!dir[i].offset || is_protected(i))
			continue;
		if (best < 0 || entry_tier(i) > entry_tier(best)
		    || (entry_tier(i) == entry_tier(best)
		        && (dir[i].last_seq < dir[best].last_seq
		            || (dir[i].last_seq == dir[best].last_seq && dir[i].length < dir[best].length))))
			best = i;
	}
	if (best < 0)
		return 0;
	evict(best);
	return 1;
}

static int compare_offset(const void *a, const void *b)
{
	const unsigned oa = dir[*(const unsigned short *)a].offset, ob = dir[*(const unsigned short *)b].offset;

	return oa < ob ? -1 : oa > ob;
}

typedef struct {
	int sector;   // first sector of the window, -1: none
	unsigned a, b; // the entries in it: by_offset[a..b)
} fc_window_t;

// For every tier t, the window of need sectors that holds no entry in use and no entry
// younger than tier t, but at least one entry (an empty window is free room, which
// best_fit finds): the highest such window with high, else the lowest.
static void find_windows(unsigned need, int high, fc_window_t win[4])
{
	const unsigned nsect = data_pages / FC_SECTOR_PAGES;
	unsigned m = 0;

	for (int t = 0; t < 4; t++)
		win[t].sector = -1;
	if (need > nsect)
		return;

	// live entries in address order; they do not overlap, so the entries touching a window
	// are one run of this list, and both ends of the run only move forward with the window
	for (int i = 0; i < numentries; i++)
		if (dir[i].offset)
			by_offset[m++] = (unsigned short)i;
	qsort(by_offset, m, sizeof(by_offset[0]), compare_offset);

	unsigned a = 0, b = 0; // entries [a, b) touch the window
	unsigned held = 0, tiers[4] = { 0, 0, 0, 0 };

	for (unsigned s = 0; s + need <= nsect; s++) {
		const unsigned last = s + need - 1;

		while (b < m && page_of(dir[by_offset[b]].offset) / FC_SECTOR_PAGES <= last) {
			const int i = by_offset[b++];
			held += is_protected(i);
			tiers[entry_tier(i)]++;
		}
		while (a < b) {
			const int i = by_offset[a];
			if ((page_of(dir[i].offset) + pages_of(dir[i].length) - 1) / FC_SECTOR_PAGES >= s)
				break;
			held -= is_protected(i);
			tiers[entry_tier(i)]--;
			a++;
		}
		if (held || a == b)
			continue;

		unsigned youngest = 0;
		while (!tiers[youngest])
			youngest++;
		for (unsigned t = 0; t <= youngest; t++) {
			if (high || win[t].sector < 0) {
				win[t].sector = (int)s;
				win[t].a = a;
				win[t].b = b;
			}
		}
	}
}

// Finds room for len bytes and prepares it; returns the directory slot or -1.
static int allocate(const char *name, unsigned len, unsigned *offset_out)
{
	unsigned pages;
	int first, slot, i;

	i = lookup(name);
	if (i >= 0) {
		if (is_protected(i)) {
			stats.store_failures++;
			return -1;
		}
		remove_entry(i);
	}

	for (slot = 0; slot < numentries; slot++)
		if (!dir[slot].offset)
			break;
	while (slot == FC_MAX_ENTRIES) {
		if (!evict_one()) {
			stats.store_failures++;
			return -1;
		}
		for (slot = 0; slot < numentries; slot++)
			if (!dir[slot].offset)
				break;
	}

	// a contiguous run of pages: small entries from the top, large ones from the bottom, in
	// free room or room held by old data, whichever is nearer that end; else by evicting
	// younger data, the oldest tier first
	pages = pages_of(len);
	const int small = pages <= FC_SMALL_PAGES;
	const unsigned need = (pages + FC_SECTOR_PAGES - 1) / FC_SECTOR_PAGES;
	fc_window_t win[4];

	first = best_fit(pages, small);
	find_windows(need, small, win);
	for (int t = FC_OLD_TIER; t >= 0; t--) {
		if (win[t].sector < 0)
			continue;
		// a small entry goes to the top of the window, a large one to its bottom
		const int wfirst = small ? (win[t].sector + (int)need) * FC_SECTOR_PAGES - (int)pages : win[t].sector * FC_SECTOR_PAGES;
		if (first >= 0 && (t < FC_OLD_TIER || (small ? first >= wfirst : first <= wfirst)))
			break; // free room, nearer the end than old data, or the only choice without evicting younger data
		for (unsigned k = win[t].a; k < win[t].b; k++)
			evict(by_offset[k]);
		first = wfirst;
		break;
	}
	if (first < 0) {
		printf("dx_fcache: no room for %s (%u KB)\n", name, len / 1024);
		stats.store_failures++;
		return -1;
	}

	prepare_run((unsigned)first, pages);
	for (i = first; i < first + (int)pages; i++)
		BIT_CLR(page_blank, i); // about to be written, whether or not it verifies
	stats.bytes_programmed += pages * DX_FC_PAGE;
	*offset_out = FC_DATA_START + (unsigned)first * DX_FC_PAGE;
	return slot;
}

// Programs len bytes at offset; a partial last page is padded with 0xff.
static void program(unsigned offset, const void *data, unsigned len)
{
	byte page[DX_FC_PAGE];
	unsigned full = len & ~(unsigned)(DX_FC_PAGE - 1);

	if (full)
		flash->program(offset, data, full);
	if (len > full) {
		memset(page, 0xff, sizeof(page));
		memcpy(page, (const byte *)data + full, len - full);
		flash->program(offset + full, page, DX_FC_PAGE);
	}
}

// Records a written entry if the flash holds what was meant to be written.
static const void *finish(int slot, const char *name, unsigned offset, unsigned len, unsigned crc)
{
	memset(&dir[slot], 0, sizeof(dir[slot]));
	if (crc32(flash->base + offset, len) != crc) {
		printf("dx_fcache: write of %s did not verify\n", name);
		stats.store_failures++;
		return NULL; // the pages stay marked written; the entry is not recorded
	}
	strcpy(dir[slot].name, name);
	dir[slot].offset = offset;
	dir[slot].length = len;
	dir[slot].crc = crc;
	dir[slot].flags = FCF_VERIFIED;
	dir[slot].last_seq = cur_seq;
	refs[slot] = 1;
	if (slot == numentries)
		numentries++;
	mark_pages(offset, len, 1);
	stats.used_bytes += len;
	stats.stores++;
	dirty = 1;
	return flash->base + offset;
}

/*
==============================================================================

PUBLIC

==============================================================================
*/

void dx_fcache_set_flash(const dx_fcache_flash_t *f, uint32_t data_id)
{
	flash = f;
	data_identity = data_id;
	loaded = 0;
}

int dx_fcache_enabled(void)
{
	return flash != NULL;
}

const void *dx_fcache_find(const char *key, unsigned *length)
{
	char name[FC_NAMELEN];
	int i;

	if (!ready())
		return NULL;
	make_name(name, key);
	i = lookup(name);
	if (i < 0) {
		stats.misses++;
		return NULL;
	}
	if (!(dir[i].flags & FCF_VERIFIED)) {
		// the region may have been overwritten by another application
		if (crc32(flash->base + dir[i].offset, dir[i].length) != dir[i].crc) {
			printf("dx_fcache: %s is damaged, reloading\n", name);
			remove_entry(i);
			stats.misses++;
			return NULL;
		}
		dir[i].flags |= FCF_VERIFIED;
	}
	touch(i);
	if (refs[i] < 0xffff)
		refs[i]++;
	stats.hits++;
	if (length)
		*length = dir[i].length;
	return flash->base + dir[i].offset;
}

const void *dx_fcache_store(const char *key, const void *data, unsigned len)
{
	char name[FC_NAMELEN];
	unsigned offset, crc;
	int slot;

	if (!ready() || len == 0)
		return NULL;
	make_name(name, key);
	slot = allocate(name, len, &offset);
	if (slot < 0)
		return NULL;
	crc = crc32(data, len);
	program(offset, data, len);
	return finish(slot, name, offset, len, crc);
}

int dx_fcache_write_begin(dx_fcache_writer_t *w, const char *key, unsigned len)
{
	memset(w, 0, sizeof(*w));
	w->slot = -1;
	if (!ready() || len == 0)
		return 0;
	make_name(w->name, key);
	w->slot = allocate(w->name, len, &w->offset);
	if (w->slot < 0)
		return 0;
	w->len = len;
	w->crc = 0xffffffff;
	return 1;
}

void dx_fcache_write(dx_fcache_writer_t *w, const void *data, unsigned n)
{
	const byte *src = data;

	if (w->slot < 0 || w->done + w->carry + n > w->len) {
		w->slot = -1; // overrun: dx_fcache_write_end fails
		return;
	}
	for (unsigned i = 0; i < n; i++)
		w->crc = crc_table[(w->crc ^ src[i]) & 0xff] ^ (w->crc >> 8);

	// fill the carried partial page, then program whole pages straight from data
	while (n > 0) {
		unsigned take;
		if (w->carry || n < DX_FC_PAGE) {
			take = DX_FC_PAGE - w->carry < n ? DX_FC_PAGE - w->carry : n;
			memcpy(w->page + w->carry, src, take);
			w->carry += take;
			src += take;
			n -= take;
			if (w->carry == DX_FC_PAGE) {
				flash->program(w->offset + w->done, w->page, DX_FC_PAGE);
				w->done += DX_FC_PAGE;
				w->carry = 0;
			}
			continue;
		}
		take = n & ~(unsigned)(DX_FC_PAGE - 1);
		flash->program(w->offset + w->done, src, take);
		w->done += take;
		src += take;
		n -= take;
	}
}

const void *dx_fcache_write_end(dx_fcache_writer_t *w)
{
	if (w->slot < 0)
		return NULL;
	if (w->carry) {
		memset(w->page + w->carry, 0xff, DX_FC_PAGE - w->carry);
		flash->program(w->offset + w->done, w->page, DX_FC_PAGE);
		w->done += w->carry;
		w->carry = 0;
	}
	if (w->done != w->len)
		return NULL; // the pages stay marked written; the entry is not recorded
	return finish(w->slot, w->name, w->offset, w->len, ~w->crc);
}

int dx_fcache_owns(const void *p)
{
	return flash != NULL && (const byte *)p >= flash->base && (const byte *)p < flash->base + flash->size;
}

void dx_fcache_release(const void *p)
{
	if (!dx_fcache_owns(p) || !loaded)
		return;
	unsigned offset = (unsigned)((const byte *)p - flash->base);
	for (int i = 0; i < numentries; i++) {
		if (dir[i].offset == offset) {
			if (refs[i] > 0)
				refs[i]--;
			return;
		}
	}
}

void dx_fcache_begin_level(void)
{
	if (!ready())
		return;
	cur_seq++;
	stamps_dirty = 1;
}

void dx_fcache_commit(void)
{
	int n, slot;
	unsigned bytes;
	fc_entry_t *packed;

	if (!ready())
		return;
	// age stamps alone wait for the next change, or FC_STAMP_LEVELS level loads; those of a
	// session without a change are not kept (the entries look as old as they were)
	if (!dirty && !(stamps_dirty && cur_seq - hdr.seq >= FC_STAMP_LEVELS))
		return;

	// drop unused slots at the end of the table (none of them can be referenced)
	n = numentries;
	while (n > 0 && !dir[n - 1].offset)
		n--;
	numentries = n;

	packed = malloc(FC_SLOT_BYTES);
	if (packed == NULL)
		return;
	slot = active_slot ^ 1;
	hdr.magic = FC_MAGIC;
	hdr.version = FC_VERSION;
	hdr.generation++;
	hdr.num_entries = (unsigned)numentries;
	hdr.seq = cur_seq;

	// entries go to flash with their RAM-only flags cleared; the header takes the place
	// of entry 0 in the buffer (it has the same size as an entry or less)
	memset(packed, 0xff, FC_SLOT_BYTES);
	byte *entries = (byte *)packed + sizeof(hdr);
	for (int i = 0; i < numentries; i++) {
		fc_entry_t e = dir[i];
		e.flags = 0;
		memcpy(entries + i * sizeof(fc_entry_t), &e, sizeof(e));
	}
	hdr.dir_crc = crc32(entries, numentries * sizeof(fc_entry_t));
	memcpy(packed, &hdr, sizeof(hdr));

	bytes = (sizeof(hdr) + numentries * sizeof(fc_entry_t) + DX_FC_PAGE - 1) & ~(unsigned)(DX_FC_PAGE - 1);
	flash->erase(slot * FC_SLOT_BYTES, FC_SLOT_BYTES);
	// entries first, header page last, so a torn write leaves the slot invalid
	if (bytes > DX_FC_PAGE)
		flash->program(slot * FC_SLOT_BYTES + DX_FC_PAGE, (byte *)packed + DX_FC_PAGE, bytes - DX_FC_PAGE);
	flash->program(slot * FC_SLOT_BYTES, packed, DX_FC_PAGE);
	stats.bytes_erased += FC_SLOT_BYTES;
	stats.bytes_programmed += bytes;
	free(packed);

	active_slot = slot;
	dirty = 0;
	stamps_dirty = 0;
}

void dx_fcache_print_usage(void)
{
	struct {
		char kind[12];
		unsigned bytes, held, count;
	} kinds[16];
	int numkinds = 0;

	if (!flash || !loaded)
		return;
	for (int i = 0; i < numentries; i++) {
		if (!dir[i].offset)
			continue;
		char kind[12];
		size_t n = strcspn(dir[i].name, ":");
		if (n >= sizeof(kind))
			n = sizeof(kind) - 1;
		memcpy(kind, dir[i].name, n);
		kind[n] = '\0';
		int k = 0;
		while (k < numkinds && strcmp(kinds[k].kind, kind) != 0)
			k++;
		if (k == numkinds) {
			if (numkinds == (int)(sizeof(kinds) / sizeof(kinds[0])))
				k = numkinds - 1; // lump the rest together
			else {
				memcpy(kinds[k].kind, kind, sizeof(kind));
				kinds[k].bytes = kinds[k].held = kinds[k].count = 0;
				numkinds++;
			}
		}
		kinds[k].bytes += dir[i].length;
		kinds[k].count++;
		if (refs[i])
			kinds[k].held += dir[i].length;
	}
	printf("dx_fcache usage (KB, KB in use, entries):");
	for (int k = 0; k < numkinds; k++)
		printf(" %s %u/%u/%u", kinds[k].kind, kinds[k].bytes / 1024, kinds[k].held / 1024, kinds[k].count);
	printf("\n");
}

void dx_fcache_print_map(void)
{
	enum { CellPages = 64 * 1024 / DX_FC_PAGE };
	char line[80];
	int n = 0;

	if (!flash || !loaded)
		return;
	printf("dx_fcache map (64 KB: # in use, + evictable, . free):\n");
	for (unsigned c = 0; c * CellPages < data_pages; c++) {
		char mark = '.';
		for (int i = 0; i < numentries; i++) {
			if (!dir[i].offset)
				continue;
			const unsigned first = page_of(dir[i].offset);
			const unsigned last = first + pages_of(dir[i].length);
			if (last <= c * CellPages || first >= (c + 1) * CellPages)
				continue;
			if (refs[i]) {
				mark = '#';
				break;
			}
			mark = '+';
		}
		line[n++] = mark;
		if (n == 64) {
			line[n] = '\0';
			printf("  %s\n", line);
			n = 0;
		}
	}
	if (n) {
		line[n] = '\0';
		printf("  %s\n", line);
	}
}

void dx_fcache_get_stats(dx_fcache_stats_t *st)
{
	*st = stats;
	st->entries = 0;
	if (!flash || !loaded)
		return;
	for (int i = 0; i < numentries; i++)
		if (dir[i].offset)
			st->entries++;
	st->free_bytes = st->capacity - st->used_bytes;
}
