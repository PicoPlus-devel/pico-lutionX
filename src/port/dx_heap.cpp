// dx_heap.cpp - TLSF engine heap with per-category accounting (see dx_heap.h).
//
// Every block carries an 8-byte header with its category, pool and cacheable flag. Blocks
// that do not come from one of the pools (e.g. strdup() inside the C library, which
// allocates with the C library's own malloc) are passed to that malloc's free().
//
// Two builds:
//   DX_HEAP_HOST (host harness): the pools come from the C library on first use, the
//     engine reaches the heap through the linker's --wrap=malloc etc., and the
//     environment selects the sizes:
//       DX_PSRAM_BYTES  size of the PSRAM pool (default: 8 MiB minus DX_ENGINE_STATIC_BYTES)
//       DX_FLASH_SIM=1  take cacheable allocations from the simulated flash cache pool
//       DX_HEAP_LOG     file for the reports (default: stderr)
//   otherwise (the board): dx_heap_init() hands over the PSRAM heap region before the
//     C++ static constructors run, and the engine library's malloc family is renamed to
//     dx_malloc etc. at build time (cmake/dx_engine.cmake). Reports go to stdout.
#include "dx_heap.h"
#include "dx_fcache.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

#include "tlsf.h"

#ifdef DX_HEAP_HOST
#if __has_include("dx_static_bytes.h")
#include "dx_static_bytes.h"
#endif
#ifndef DX_ENGINE_STATIC_BYTES
#define DX_ENGINE_STATIC_BYTES 0
#endif
extern "C" {
void *__real_malloc(size_t);
void __real_free(void *);
size_t malloc_usable_size(void *);
}
#define FOREIGN_FREE __real_free
#else
extern "C" size_t malloc_usable_size(void *);
#define FOREIGN_FREE free
#endif

namespace {

constexpr size_t HeaderBytes = 8;

struct Header {
	uint8_t category;
	uint8_t pool; // PoolFlashSim: flash simulation, else PSRAM; PoolRefBlock: see dx_heap_refblock
	uint8_t cacheable;
	uint8_t pad;   // alignment padding in front of the header, in units of HeaderBytes
	uint32_t size; // TLSF block size, header included
};
static_assert(sizeof(Header) == HeaderBytes, "header size");

constexpr uint8_t PoolFlashSim = 1;
constexpr uint8_t PoolRefBlock = 0x80;

// dx_heap_refblock: heap blocks and flash asset cache references, freed or released with it
struct RefBlock {
	unsigned count;
	const void *refs[1];
};

// sizes for printf: the board's C library (newlib-nano) has no %zu
unsigned long Ul(size_t v)
{
	return static_cast<unsigned long>(v);
}

const char *const CategoryNames[DXH_COUNT] = {
	"other", "screen", "demo", "dungeon", "monster", "player", "missile", "ui", "object", "towner", "sound", "mpq"
};

struct Pool {
	tlsf_t tlsf = nullptr;
	uint8_t *base = nullptr;
	size_t size = 0;
	size_t used = 0;
	size_t peak = 0;
	size_t markPeak = 0;
};

Pool Psram;
Pool FlashSim;
bool Initialised;
bool UseFlashSim;
int Category = DXH_OTHER;
int Cacheable;
FILE *Log;

size_t Live[DXH_COUNT][2]; // [category][cacheable], PSRAM pool only
size_t FlashLive[DXH_COUNT];
// "Resident": the PSRAM pool minus cacheable blocks, i.e. what would stay in PSRAM if all
// cacheable data were moved to the flash cache.
size_t ResidentPeak;
size_t ResidentMarkPeak;

bool InPool(const Pool &pool, const void *p)
{
	return pool.base != nullptr && static_cast<const uint8_t *>(p) >= pool.base
	    && static_cast<const uint8_t *>(p) < pool.base + pool.size;
}

void SetupPool(Pool &pool, void *base, size_t size)
{
	pool.base = static_cast<uint8_t *>(base);
	pool.size = size;
	pool.tlsf = tlsf_create_with_pool(base, size);
}

#ifdef DX_HEAP_HOST
constexpr size_t PsramBytes = 8u * 1024 * 1024;
constexpr size_t FlashSimBytes = 16u * 1024 * 1024;

void ReservePool(Pool &pool, size_t size)
{
	void *base = __real_malloc(size);
	if (base == nullptr) {
		fprintf(stderr, "dx_heap: cannot reserve %zu bytes\n", size);
		abort();
	}
	SetupPool(pool, base, size);
}

void Init()
{
	if (Initialised)
		return;
	Initialised = true;
	size_t psram = PsramBytes - DX_ENGINE_STATIC_BYTES;
	if (const char *env = getenv("DX_PSRAM_BYTES"))
		psram = strtoul(env, nullptr, 0);
	const char *sim = getenv("DX_FLASH_SIM");
	UseFlashSim = sim != nullptr && sim[0] == '1';
	ReservePool(Psram, psram);
	if (UseFlashSim)
		ReservePool(FlashSim, FlashSimBytes);
	const char *log = getenv("DX_HEAP_LOG");
	Log = log != nullptr ? fopen(log, "w") : nullptr;
	if (Log == nullptr)
		Log = stderr;
	fprintf(Log, "dx_heap: PSRAM pool %zu bytes (engine static data %u bytes), flash simulation %s\n",
	    psram, static_cast<unsigned>(DX_ENGINE_STATIC_BYTES), UseFlashSim ? "on" : "off");
}
#else
void Init()
{
	if (!Initialised)
		dx_heap_out_of_memory(0); // allocation before dx_heap_init
	if (Log == nullptr)
		Log = stdout;
}
#endif

size_t Resident()
{
	size_t cacheable = 0;
	for (int c = 0; c < DXH_COUNT; c++)
		cacheable += Live[c][1];
	return Psram.used - cacheable;
}

void LargestFree(void *ptr, size_t size, int used, void *user)
{
	(void)ptr;
	auto *largest = static_cast<size_t *>(user);
	if (!used && size > *largest)
		*largest = size;
}

void *Allocate(size_t bytes, size_t align)
{
	Init();
	const bool toFlash = UseFlashSim && Cacheable != 0;
	Pool &pool = toFlash ? FlashSim : Psram;
	if (align < HeaderBytes)
		align = HeaderBytes;
	// The header sits directly in front of the returned pointer; for larger alignments
	// allocate an aligned block with room for a whole alignment unit in front.
	const size_t front = align > HeaderBytes ? align : HeaderBytes;
	uint8_t *raw = static_cast<uint8_t *>(align > HeaderBytes
	        ? tlsf_memalign(pool.tlsf, align, bytes + front)
	        : tlsf_malloc(pool.tlsf, bytes + front));
	if (raw == nullptr) {
		fprintf(Log, "dx_heap: %s full allocating %lu bytes (category %s%s)\n", toFlash ? "flash simulation" : "PSRAM heap",
		    Ul(bytes), CategoryNames[Category], Cacheable ? ", cacheable" : "");
		dx_heap_mark("out of memory");
		dx_heap_out_of_memory(bytes);
		return nullptr;
	}
	uint8_t *user = raw + front;
	auto *header = reinterpret_cast<Header *>(user - HeaderBytes);
	header->category = static_cast<uint8_t>(Category);
	header->pool = toFlash ? PoolFlashSim : 0;
	header->cacheable = static_cast<uint8_t>(Cacheable != 0);
	header->pad = static_cast<uint8_t>(front / HeaderBytes);
	header->size = static_cast<uint32_t>(tlsf_block_size(raw));
	pool.used += header->size;
	if (pool.used > pool.peak)
		pool.peak = pool.used;
	if (pool.used > pool.markPeak)
		pool.markPeak = pool.used;
	if (toFlash) {
		FlashLive[header->category] += header->size;
	} else {
		Live[header->category][header->cacheable] += header->size;
		size_t resident = Resident();
		if (resident > ResidentPeak)
			ResidentPeak = resident;
		if (resident > ResidentMarkPeak)
			ResidentMarkPeak = resident;
	}
	return user;
}

void Release(void *p)
{
	if (p == nullptr)
		return;
	if (dx_fcache_owns(p)) {
		dx_fcache_release(p); // sprite data the engine got from the flash asset cache
		return;
	}
	if (!InPool(Psram, p) && !InPool(FlashSim, p)) {
		FOREIGN_FREE(p);
		return;
	}
	auto *header = reinterpret_cast<Header *>(static_cast<uint8_t *>(p) - HeaderBytes);
	if ((header->pool & PoolRefBlock) != 0) {
		const auto *block = static_cast<const RefBlock *>(p);
		for (unsigned i = 0; i < block->count; i++)
			Release(const_cast<void *>(block->refs[i]));
	}
	Pool &pool = (header->pool & PoolFlashSim) != 0 ? FlashSim : Psram;
	pool.used -= header->size;
	if ((header->pool & PoolFlashSim) != 0)
		FlashLive[header->category] -= header->size;
	else
		Live[header->category][header->cacheable] -= header->size;
	tlsf_free(pool.tlsf, static_cast<uint8_t *>(p) - header->pad * HeaderBytes);
}

size_t UsableSize(void *p)
{
	auto *header = reinterpret_cast<Header *>(static_cast<uint8_t *>(p) - HeaderBytes);
	return header->size - header->pad * HeaderBytes;
}

} // namespace

extern "C" {

void dx_heap_init(void *base, size_t size)
{
	SetupPool(Psram, base, size);
	Initialised = true;
}

int dx_heap_set_category(int category)
{
	int previous = Category;
	Category = (category >= 0 && category < DXH_COUNT) ? category : DXH_OTHER;
	return previous;
}

int dx_heap_set_cacheable(int cacheable)
{
	int previous = Cacheable;
	Cacheable = cacheable;
	return previous;
}

size_t dx_heap_used(void)
{
	return Psram.used;
}

size_t dx_heap_peak(void)
{
	return Psram.peak;
}

size_t dx_heap_size(void)
{
	return Psram.size;
}

size_t dx_heap_largest_free(void)
{
	if (Psram.tlsf == nullptr)
		return 0;
	size_t largest = 0;
	tlsf_walk_pool(tlsf_get_pool(Psram.tlsf), LargestFree, &largest);
	return largest;
}

void dx_heap_mark(const char *label)
{
	Init();
	size_t largest = 0;
	tlsf_walk_pool(tlsf_get_pool(Psram.tlsf), LargestFree, &largest);
	fprintf(Log, "MARK %-26s psram: live %8lu peak-since-mark %8lu peak %8lu largest-free %8lu | resident: live %8lu peak-since-mark %8lu peak %8lu",
	    label, Ul(Psram.used), Ul(Psram.markPeak), Ul(Psram.peak), Ul(largest), Ul(Resident()), Ul(ResidentMarkPeak), Ul(ResidentPeak));
	if (UseFlashSim)
		fprintf(Log, " | flash-sim: live %8lu peak %8lu", Ul(FlashSim.used), Ul(FlashSim.peak));
	fprintf(Log, "\n  live by category (psram total/cacheable):");
	for (int c = 0; c < DXH_COUNT; c++) {
		if (Live[c][0] + Live[c][1] + FlashLive[c] == 0)
			continue;
		fprintf(Log, " %s %lu/%lu", CategoryNames[c], Ul(Live[c][0] + Live[c][1]), Ul(Live[c][1]));
		if (UseFlashSim)
			fprintf(Log, "+flash %lu", Ul(FlashLive[c]));
	}
	fprintf(Log, "\n");
	fflush(Log);
	Psram.markPeak = Psram.used;
	ResidentMarkPeak = Resident();
}

void *dx_malloc(size_t bytes)
{
	return Allocate(bytes, 0);
}

void *dx_calloc(size_t count, size_t size)
{
	size_t bytes = count * size;
	void *p = Allocate(bytes, 0);
	if (p != nullptr)
		memset(p, 0, bytes);
	return p;
}

void *dx_realloc(void *p, size_t bytes)
{
	if (p == nullptr)
		return Allocate(bytes, 0);
	if (bytes == 0) {
		Release(p);
		return nullptr;
	}
	if (!InPool(Psram, p) && !InPool(FlashSim, p)) {
		// A block from the C library's malloc; move it into the pool.
		void *q = Allocate(bytes, 0);
		if (q == nullptr)
			return nullptr;
		size_t old = malloc_usable_size(p);
		memcpy(q, p, old < bytes ? old : bytes);
		FOREIGN_FREE(p);
		return q;
	}
	size_t old = UsableSize(p);
	if (bytes <= old)
		return p;
	void *q = Allocate(bytes, 0);
	if (q == nullptr)
		return nullptr;
	memcpy(q, p, old);
	Release(p);
	return q;
}

void *dx_heap_refblock(const void *const *refs, unsigned count)
{
	auto *block = static_cast<RefBlock *>(Allocate(sizeof(RefBlock) + (count > 0 ? count - 1 : 0) * sizeof(void *), 0));
	if (block == nullptr)
		return nullptr;
	reinterpret_cast<Header *>(reinterpret_cast<uint8_t *>(block) - HeaderBytes)->pool |= PoolRefBlock;
	block->count = count;
	memcpy(block->refs, refs, count * sizeof(void *));
	return block;
}

void dx_free(void *p)
{
	Release(p);
}

void *dx_memalign(size_t align, size_t bytes)
{
	return Allocate(bytes, align);
}

int dx_posix_memalign(void **result, size_t align, size_t bytes)
{
	*result = Allocate(bytes, align);
	return *result != nullptr ? 0 : 12; // ENOMEM
}

#ifdef DX_HEAP_HOST
void dx_heap_out_of_memory(size_t bytes)
{
	(void)bytes;
	dx_fcache_print_usage();
	dx_fcache_print_map();
	fflush(stdout);
	fflush(Log);
	abort();
}

void *__wrap_malloc(size_t bytes) { return dx_malloc(bytes); }
void *__wrap_calloc(size_t count, size_t size) { return dx_calloc(count, size); }
void *__wrap_realloc(void *p, size_t bytes) { return dx_realloc(p, bytes); }
void __wrap_free(void *p) { dx_free(p); }
void *__wrap_memalign(size_t align, size_t bytes) { return dx_memalign(align, bytes); }
void *__wrap_aligned_alloc(size_t align, size_t bytes) { return dx_memalign(align, bytes); }
int __wrap_posix_memalign(void **result, size_t align, size_t bytes) { return dx_posix_memalign(result, align, bytes); }
#else
__attribute__((weak)) void dx_heap_out_of_memory(size_t bytes)
{
	printf("dx_heap: out of memory (%u bytes)\n", static_cast<unsigned>(bytes));
	abort();
}
#endif

} // extern "C"

// ---- C++ allocation --------------------------------------------------------------------

void *operator new(size_t bytes) { return Allocate(bytes, 0); }
void *operator new[](size_t bytes) { return Allocate(bytes, 0); }
void *operator new(size_t bytes, const std::nothrow_t &) noexcept { return Allocate(bytes, 0); }
void *operator new[](size_t bytes, const std::nothrow_t &) noexcept { return Allocate(bytes, 0); }
void *operator new(size_t bytes, std::align_val_t align) { return Allocate(bytes, static_cast<size_t>(align)); }
void *operator new[](size_t bytes, std::align_val_t align) { return Allocate(bytes, static_cast<size_t>(align)); }
void operator delete(void *p) noexcept { Release(p); }
void operator delete[](void *p) noexcept { Release(p); }
void operator delete(void *p, size_t) noexcept { Release(p); }
void operator delete[](void *p, size_t) noexcept { Release(p); }
void operator delete(void *p, std::align_val_t) noexcept { Release(p); }
void operator delete[](void *p, std::align_val_t) noexcept { Release(p); }
void operator delete(void *p, size_t, std::align_val_t) noexcept { Release(p); }
void operator delete[](void *p, size_t, std::align_val_t) noexcept { Release(p); }
