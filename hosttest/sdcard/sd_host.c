// sd_host.c -- SD card mode of the harness: DX_SDCARD=<card image>.
//
// The engine's files go through the board's own file layer, src/pico/dx_fatfs_io.c (its
// POSIX names renamed to dxio_*, see dx_fatfs_io_host.h), on the board's FatFs and
// ffconf.h, on a card image (sd_disk.c) instead of the card. The engine's path calls are
// linked to the __wrap_ functions below (-Wl,--wrap=fopen,... in CMakeLists.txt); with SD
// card mode off, or for the harness's own files (dx_sdcard_host_files), they pass through.
//
// Above the POSIX calls the board has newlib's stdio, the harness glibc's: a stream here is
// a glibc FILE (fopencookie) on the dxio_ calls, with newlib's 1 KB buffer and its handling
// of append mode. The order and size of the lseek and read calls may still differ from the
// board's; what FatFs and dx_fatfs_io.c do with each call is the board's.
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "tf_card.h"
#include "pico/stdio.h"

#include "dx_fatal.h"
#include "sd_disk.h"
#include "sd_host.h"

int dxio_open(const char *path, int oflag, ...);
int dxio_close(int fd);
ssize_t dxio_read(int fd, void *buf, size_t n);
ssize_t dxio_write(int fd, const void *buf, size_t n);
off_t dxio_lseek(int fd, off_t off, int whence);
int dxio_access(const char *path, int mode);
int dxio_stat(const char *path, struct stat *st);
int dxio_mkdir(const char *path, mode_t mode);
int dxio_remove(const char *path);
int dxio_rename(const char *from, const char *to);
int dxio_truncate(const char *path, off_t length);
ssize_t dxio_readlink(const char *path, char *buf, size_t size);

FILE *__real_fopen(const char *path, const char *mode);
int __real_stat(const char *path, struct stat *st);
int __real_access(const char *path, int mode);
int __real_mkdir(const char *path, mode_t mode);
int __real_remove(const char *path);
int __real_rename(const char *from, const char *to);
int __real_truncate(const char *path, off_t length);
ssize_t __real_readlink(const char *path, char *buf, size_t size);
void *__real_malloc(size_t size);
void __real_free(void *p);

static bool	active;
static int	host_files;

static bool on_card(void)
{
	return active && host_files == 0;
}

int dx_sdcard_attach(const char *image)
{
	if (sd_disk_open(image, 0) != 0)
		return -1;
	fprintf(stderr, "sdcard: card image %s\n", image);
	active = true;
	return 0;
}

void dx_sdcard_host_files(int delta)
{
	host_files += delta;
}

// ---- Board stubs (pico_fatfs, the SDK, dx_fatal) ---------------------------------------

bool pico_fatfs_set_config(pico_fatfs_spi_config_t *config)
{
	(void)config;
	return true;
}

void pico_fatfs_config_spi_pio(PIO pio, uint sm)
{
	(void)pio;
	(void)sm;
}

uint spi_get_index(spi_inst_t *spi)
{
	return spi == spi1 ? 1 : 0;
}

int pio_claim_unused_sm(PIO pio, bool required)
{
	(void)pio;
	(void)required;
	return 0;
}

void sleep_ms(uint32_t ms)
{
	(void)ms;
}

// dx_usb.cpp: the USB timer, held off around each SD transfer (no USB in the harness)
void dx_usb_hold(void)
{
}

void dx_usb_release(void)
{
}

int stdio_put_string(const char *s, int len, bool newline, bool cr_translation)
{
	(void)newline;
	(void)cr_translation;
	return (int)fwrite(s, 1, (size_t)len, stdout);
}

void dx_fatal(const char *fmt, ...)
{
	va_list ap;

	fprintf(stderr, "dx_fatal: ");
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	fprintf(stderr, "\n");
	exit(1);
}

void dx_fatal_halt(void)
{
	exit(1);
}

// ---- Streams ------------------------------------------------------------------------------

typedef struct {
	int fd;
	bool append;
	char path[64];
} card_file;

static const char *whence_name(int whence)
{
	return whence == SEEK_SET ? "SEEK_SET" : whence == SEEK_CUR ? "SEEK_CUR" : whence == SEEK_END ? "SEEK_END" : "?";
}

static ssize_t card_read(void *cookie, char *buf, size_t size)
{
	card_file *f = cookie;
	ssize_t n = dxio_read(f->fd, buf, size);

	if (n < 0)
		fprintf(stderr, "sdcard: read(%s, %zu) failed\n", f->path, size);
	return n;
}

static ssize_t card_write(void *cookie, const char *buf, size_t size)
{
	card_file *f = cookie;
	ssize_t n;

	if (f->append) // newlib's __swrite: every write of an append stream goes to the end
		dxio_lseek(f->fd, 0, SEEK_END);
	n = dxio_write(f->fd, buf, size);
	if (n < 0) {
		fprintf(stderr, "sdcard: write(%s, %zu) failed\n", f->path, size);
		return 0;
	}
	return n;
}

static int card_seek(void *cookie, off64_t *offset, int whence)
{
	card_file *f = cookie;
	off_t pos = dxio_lseek(f->fd, (off_t)*offset, whence);

	if (pos == -1) {
		fprintf(stderr, "sdcard: lseek(%s, %lld, %s) failed\n", f->path, (long long)*offset, whence_name(whence));
		return -1;
	}
	*offset = pos;
	return 0;
}

static int card_close(void *cookie)
{
	card_file *f = cookie;
	int r = dxio_close(f->fd);

	__real_free(f);
	return r;
}

// fopen's mode -> open's flags, as newlib's __sflags
static int open_flags(const char *mode, bool *append)
{
	int flags;

	*append = false;
	switch (*mode++) {
	case 'r': flags = O_RDONLY; break;
	case 'w': flags = O_WRONLY | O_CREAT | O_TRUNC; break;
	case 'a': flags = O_WRONLY | O_CREAT | O_APPEND; *append = true; break;
	default: return -1;
	}
	for (; *mode != '\0'; mode++) {
		if (*mode == '+')
			flags = (flags & ~O_ACCMODE) | O_RDWR;
		else if (*mode == 'x')
			flags |= O_EXCL;
	}
	return flags;
}

FILE *__wrap_fopen(const char *path, const char *mode)
{
	static const cookie_io_functions_t io = { card_read, card_write, card_seek, card_close };
	card_file *f;
	FILE *fp;
	bool append;
	int flags, fd;

	if (!on_card())
		return __real_fopen(path, mode);
	if ((flags = open_flags(mode, &append)) < 0) {
		errno = EINVAL;
		return NULL;
	}
	fd = dxio_open(path, flags);
	if (sd_disk_trace())
		fprintf(stderr, "sdcard: fopen(%s, %s) = fd %d (after read #%u)\n", path, mode, fd, sd_disk_reads());
	if (fd < 0)
		return NULL;
	// newlib's FILE and buffer come from its own heap, not the engine's
	if ((f = __real_malloc(sizeof(*f))) == NULL || (fp = fopencookie(f, mode, io)) == NULL) {
		__real_free(f);
		dxio_close(fd);
		errno = ENOMEM;
		return NULL;
	}
	f->fd = fd;
	f->append = append;
	snprintf(f->path, sizeof(f->path), "%s", path);
	setvbuf(fp, NULL, _IOFBF, 1024); // newlib's BUFSIZ
	if (append)
		fseek(fp, 0, SEEK_END); // newlib's fopen
	return fp;
}

// ---- Path calls ---------------------------------------------------------------------------

int __wrap_stat(const char *path, struct stat *st)
{
	return on_card() ? dxio_stat(path, st) : __real_stat(path, st);
}

int __wrap_access(const char *path, int mode)
{
	return on_card() ? dxio_access(path, mode) : __real_access(path, mode);
}

int __wrap_mkdir(const char *path, mode_t mode)
{
	return on_card() ? dxio_mkdir(path, mode) : __real_mkdir(path, mode);
}

int __wrap_remove(const char *path)
{
	return on_card() ? dxio_remove(path) : __real_remove(path);
}

int __wrap_rename(const char *from, const char *to)
{
	return on_card() ? dxio_rename(from, to) : __real_rename(from, to);
}

int __wrap_truncate(const char *path, off_t length)
{
	return on_card() ? dxio_truncate(path, length) : __real_truncate(path, length);
}

ssize_t __wrap_readlink(const char *path, char *buf, size_t size)
{
	return on_card() ? dxio_readlink(path, buf, size) : __real_readlink(path, buf, size);
}
