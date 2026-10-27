// dx_fatfs_io_host.h -- included ahead of src/pico/dx_fatfs_io.c in the host harness.
//
// On the board that file provides the POSIX and newlib names (open, read, lseek, stat, ...)
// that the C library and the engine end in. On the host they would replace glibc's for the
// whole process, so here they get assembler names of their own (dxio_open, ...); sd_host.c
// sends the engine's file calls there. The file itself is compiled unchanged.
#pragma once

#pragma redefine_extname open dxio_open
#pragma redefine_extname close dxio_close
#pragma redefine_extname read dxio_read
#pragma redefine_extname write dxio_write
#pragma redefine_extname lseek dxio_lseek
#pragma redefine_extname access dxio_access
#pragma redefine_extname stat dxio_stat
#pragma redefine_extname mkdir dxio_mkdir
#pragma redefine_extname unlink dxio_unlink
#pragma redefine_extname remove dxio_remove
#pragma redefine_extname rename dxio_rename
#pragma redefine_extname truncate dxio_truncate
#pragma redefine_extname readlink dxio_readlink
#pragma redefine_extname _open dxio__open
#pragma redefine_extname _close dxio__close
#pragma redefine_extname _read dxio__read
#pragma redefine_extname _write dxio__write
#pragma redefine_extname _lseek dxio__lseek
#pragma redefine_extname _fstat dxio__fstat
#pragma redefine_extname _isatty dxio__isatty
#pragma redefine_extname _stat dxio__stat
#pragma redefine_extname _unlink dxio__unlink

#include <sys/types.h>

typedef ssize_t _ssize_t; // newlib's name

// newlib's syscall names: the pragma applies through a declaration, and glibc has none
struct stat;
int _open(const char *fn, int oflag, ...);
int _close(int fd);
int _read(int handle, char *buffer, int length);
int _write(int handle, char *buffer, int length);
off_t _lseek(int fd, off_t pos, int whence);
int _fstat(int fd, struct stat *buf);
int _isatty(int fd);
int _stat(const char *path, struct stat *st);
int _unlink(const char *path);

// the board's SD card pins (<board>_cflags.h); only printed or ignored on the host
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1
#define SD_RX 4
#define SD_CS 5
#define SD_SCK 6
#define SD_TX 7
