// bzlib.h - stand-in for libmpq's bzip2 decompressor. The game MPQs use PKWARE implode
// (retail) or zlib (DevilutionX's spawn.mpq); bzip2 only appears in DevilutionX's own
// asset MPQs, which the port does not read. BZ2_bzDecompressInit() always fails.
#pragma once

#define BZ_OK 0
#define BZ_STREAM_END 4
#define BZ_DATA_ERROR (-4)

typedef struct {
	char *next_in;
	unsigned int avail_in;
	unsigned int total_in_lo32;
	unsigned int total_in_hi32;
	char *next_out;
	unsigned int avail_out;
	unsigned int total_out_lo32;
	unsigned int total_out_hi32;
	void *state;
	void *bzalloc;
	void *bzfree;
	void *opaque;
} bz_stream;

static inline int BZ2_bzDecompressInit(bz_stream *s, int verbosity, int small) { (void)s; (void)verbosity; (void)small; return BZ_DATA_ERROR; }
static inline int BZ2_bzDecompress(bz_stream *s) { (void)s; return BZ_STREAM_END; }
static inline int BZ2_bzDecompressEnd(bz_stream *s) { (void)s; return BZ_OK; }
