// peakheap.c - LD_PRELOAD heap tracker for measuring DevilutionX's heap use on the host.
//
// Build: gcc -O2 -shared -fPIC -o peakheap.so tools/peakheap.c -ldl
// Use:   PEAKHEAP_OUT=heap.log LD_PRELOAD=./peakheap.so ./devilutionx ...
//
// Tracks the live heap (malloc_usable_size of every live block) and its peak. Blocks of
// PEAKHEAP_BIG bytes or more (default 16 KiB) are also recorded with a short backtrace, so
// the largest live blocks can be listed and resolved with addr2line afterwards.
//
// The program under test may call peakheap_mark("label") (looked up with dlsym, so it is
// optional) to print a report: the live heap now, the peak since the previous mark, the
// overall peak and the largest live blocks. A report is also printed at exit and on SIGTERM.

#define _GNU_SOURCE
#include <dlfcn.h>
#include <execinfo.h>
#include <malloc.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

extern void *__libc_malloc(size_t);
extern void *__libc_calloc(size_t, size_t);
extern void *__libc_realloc(void *, size_t);
extern void __libc_free(void *);
extern void *__libc_memalign(size_t, size_t);

#define MAX_BIG 65536
#define DEPTH 10

typedef struct {
	void *ptr;
	size_t size;
	void *frames[DEPTH];
	int nframes;
	int sdl; // allocated from inside a libSDL* library (host-only, not part of the engine budget)
} Big;

static Big big[MAX_BIG];
static atomic_flag lock = ATOMIC_FLAG_INIT;
static size_t cur, peak, markPeak, bigThreshold = 16384;
// Engine-only figures: the live heap minus big blocks allocated inside libSDL*.
static size_t sdlBig, enginePeak, engineMarkPeak;
static int inHook; // per-process guard; the engine is effectively single-threaded
static FILE *out;
static int initialised;
static int topN = 30;

static int fromSdl(void **frames, int n)
{
	for (int f = 2; f < n; f++) {
		Dl_info info;
		if (dladdr(frames[f], &info) && info.dli_fname && strstr(info.dli_fname, "libSDL"))
			return 1;
	}
	return 0;
}

static void updateEnginePeak(void)
{
	size_t e = cur - sdlBig;
	if (e > enginePeak)
		enginePeak = e;
	if (e > engineMarkPeak)
		engineMarkPeak = e;
}

static void lockit(void) { while (atomic_flag_test_and_set(&lock)) { } }
static void unlockit(void) { atomic_flag_clear(&lock); }

static void init(void)
{
	if (initialised)
		return;
	initialised = 1;
	const char *t = getenv("PEAKHEAP_BIG");
	if (t)
		bigThreshold = strtoul(t, NULL, 0);
	const char *k = getenv("PEAKHEAP_TOP");
	if (k)
		topN = atoi(k);
	const char *o = getenv("PEAKHEAP_OUT");
	out = o ? fopen(o, "w") : NULL;
	if (!out)
		out = stderr;
}

static void track(void *p, int add)
{
	if (!p)
		return;
	size_t sz = malloc_usable_size(p);
	lockit();
	if (add) {
		cur += sz;
		if (cur > peak)
			peak = cur;
		if (cur > markPeak)
			markPeak = cur;
	} else {
		cur -= sz;
	}
	if (sz >= bigThreshold) {
		if (add) {
			for (int i = 0; i < MAX_BIG; i++) {
				if (big[i].ptr == NULL) {
					big[i].ptr = p;
					big[i].size = sz;
					big[i].nframes = 0;
					big[i].sdl = 0;
					if (!inHook) {
						inHook = 1;
						unlockit();
						big[i].nframes = backtrace(big[i].frames, DEPTH);
						big[i].sdl = fromSdl(big[i].frames, big[i].nframes);
						lockit();
						inHook = 0;
					}
					if (big[i].sdl)
						sdlBig += sz;
					break;
				}
			}
		} else {
			for (int i = 0; i < MAX_BIG; i++) {
				if (big[i].ptr == p) {
					big[i].ptr = NULL;
					if (big[i].sdl)
						sdlBig -= big[i].size;
					break;
				}
			}
		}
	}
	updateEnginePeak();
	unlockit();
}

void *malloc(size_t n)
{
	void *p = __libc_malloc(n);
	track(p, 1);
	return p;
}

void *calloc(size_t a, size_t b)
{
	void *p = __libc_calloc(a, b);
	track(p, 1);
	return p;
}

void free(void *p)
{
	track(p, 0);
	__libc_free(p);
}

void *realloc(void *p, size_t n)
{
	track(p, 0);
	void *q = __libc_realloc(p, n);
	if (q)
		track(q, 1);
	else if (p && n)
		track(p, 1); // realloc failed, the old block is still live
	return q;
}

void *memalign(size_t a, size_t n)
{
	void *p = __libc_memalign(a, n);
	track(p, 1);
	return p;
}

void *aligned_alloc(size_t a, size_t n) { return memalign(a, n); }

int posix_memalign(void **r, size_t a, size_t n)
{
	void *p = memalign(a, n);
	if (!p)
		return 12;
	*r = p;
	return 0;
}

static int cmpBig(const void *a, const void *b)
{
	const Big *x = a, *y = b;
	return (x->size < y->size) - (x->size > y->size);
}

static void printFrame(void *addr)
{
	Dl_info info;
	if (dladdr(addr, &info) && info.dli_fname) {
		const char *base = strrchr(info.dli_fname, '/');
		fprintf(out, " %s+0x%lx", base ? base + 1 : info.dli_fname,
		    (unsigned long)((uintptr_t)addr - (uintptr_t)info.dli_fbase));
	} else {
		fprintf(out, " %p", addr);
	}
}

void peakheap_mark(const char *label)
{
	init();
	static Big snap[MAX_BIG];
	int n = 0;
	lockit();
	size_t c = cur, p = peak, mp = markPeak;
	size_t ec = cur - sdlBig, ep = enginePeak, emp = engineMarkPeak;
	markPeak = cur;
	engineMarkPeak = ec;
	for (int i = 0; i < MAX_BIG; i++)
		if (big[i].ptr)
			snap[n++] = big[i];
	unlockit();
	qsort(snap, n, sizeof(Big), cmpBig);
	size_t bigTotal = 0;
	for (int i = 0; i < n; i++)
		if (!snap[i].sdl)
			bigTotal += snap[i].size;
	fprintf(out, "MARK %-28s all: live %9zu peak-since-last %9zu peak %9zu | engine: live %9zu peak-since-last %9zu peak %9zu | engine big-blocks %zu bytes\n",
	    label, c, mp, p, ec, emp, ep, bigTotal);
	int shown = 0;
	for (int i = 0; i < n && shown < topN; i++) {
		if (snap[i].sdl)
			continue;
		shown++;
		fprintf(out, "  %9zu", snap[i].size);
		// Skip frame 0 (track) and 1 (the malloc wrapper).
		for (int f = 2; f < snap[i].nframes; f++)
			printFrame(snap[i].frames[f]);
		fprintf(out, "\n");
	}
	fflush(out);
}

static void onTerm(int sig)
{
	(void)sig;
	peakheap_mark("SIGTERM");
	_exit(0);
}

__attribute__((constructor)) static void ctor(void)
{
	init();
	signal(SIGTERM, onTerm);
}

__attribute__((destructor)) static void dtor(void)
{
	peakheap_mark("exit");
}
