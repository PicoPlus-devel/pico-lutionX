// dx_fatfs_io.c -- FatFs-backed file I/O. From pico-quake2's q2_fatfs_io.c (itself
// adapted from pico-duke3D's duke_fatfs_io.c).
//
// The engine reaches files through stdio (fopen/fread/fseek: libmpq, saves, diablo.ini)
// and a few POSIX calls (access, stat, mkdir, truncate, rename, remove in
// utils/file_util.cpp), which end here on FatFs. fds 0/1/2 stay with the console.
//
// libmpq seeks around inside the MPQ (up to 517 MB for DIABDAT.MPQ); FatFs would walk the
// FAT chain from the start of the file on each seek, so read-only opens of large files
// get a fast-seek cluster map, built once per file and shared by every later open of it.
//
// FatFs's calls into the SD driver (tf_card.c) come here first (-Wl,--wrap in
// CMakeLists.txt), which holds the USB timer off for each transfer (dx_usb_hold).

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "pico/stdlib.h"
#include "pico/stdio.h"
#include "hardware/pio.h"
#include "ff.h"
#include "diskio.h"
#include "tf_card.h"

#include "dx_fatfs_io.h"
#include "dx_fatal.h"
#include "dx_input.h"
#include "dx_platform.h"
#include "dx_prof.h"

#define DX_MAXFDS		8		// the MPQ archive(s), a save file, diablo.ini, a few spare
#define DX_FD_BASE		3		// clear of stdio fds 0/1/2

#define FASTSEEK_MIN	(1024 * 1024)	// files at least this large get a cluster map
#define FASTSEEK_FILES	8
#define LINKMAP_ATTEMPTS	2	// a cluster map is built again once after a failed SD read

#define SD_MOUNT_ATTEMPTS	5

static FIL		fils[DX_MAXFDS];
static uint8_t	used[DX_MAXFDS];
static FATFS	fs;
static uint32_t	data_id;

static struct
{
	char	path[64];
	DWORD	*table;
} linkmaps[FASTSEEK_FILES];

void dx_fatfs_init (void)
{
	static pico_fatfs_spi_config_t cfg = {
		SDCARD_SPI, CLK_SLOW_DEFAULT, CLK_FAST_DEFAULT_PIO,
		SD_RX, SD_CS, SD_SCK, SD_TX, true
	};
	FILINFO		fno;
	FRESULT		fr;
	const char	*data = NULL;

	if (pico_fatfs_set_config (&cfg))
		printf ("dx_fatfs: hardware SPI%d\n", spi_get_index (SDCARD_SPI));
	else
	{
		pico_fatfs_config_spi_pio (SDCARD_PIO, pio_claim_unused_sm (SDCARD_PIO, true));
		printf ("dx_fatfs: PIO-SPI\n");
	}
	// The Fruit Jam's card sometimes does not answer its first initialisation after a
	// reset (FR_NOT_READY). FatFs initialises the card again on the next mount, so a
	// few retries ride that out (pico-quake2).
	for (int attempt = 1; ; attempt++)
	{
		fr = f_mount (&fs, "", 1);
		if (fr == FR_OK || attempt == SD_MOUNT_ATTEMPTS)
			break;
		printf ("dx_fatfs: mount failed (FatFs error %d), retrying\n", fr);
		sleep_ms (250);
	}
	if (fr != FR_OK)
		dx_fatal ("No SD card (FatFs error %d).\n"
			"Insert a FAT/exFAT card with the game\ndata in " DX_DATA_DIR ".", fr);
	fr = f_chdir (DX_DATA_DIR);
	if (fr != FR_OK)
		dx_fatal (DX_DATA_DIR " not found on the SD card.\n"
			"Create it and copy spawn.mpq or\nDIABDAT.MPQ and the assets folder\ninto it.");
	if (f_stat ("DIABDAT.MPQ", &fno) == FR_OK)
		data = "DIABDAT.MPQ";
	else if (f_stat ("spawn.mpq", &fno) == FR_OK)
		data = "spawn.mpq";
	if (!data)
		dx_fatal ("Neither DIABDAT.MPQ nor spawn.mpq\nwas found in " DX_DATA_DIR ".");
	printf ("dx_fatfs: " DX_DATA_DIR "/%s, %lu KB\n", data, (unsigned long)(fno.fsize / 1024));
	// identity of the game data for the flash asset cache: another MPQ discards its entries
	data_id = (uint32_t)fno.fsize ^ 0x5a17c0deu;
	if (f_stat ("assets", &fno) != FR_OK || !(fno.fattrib & AM_DIR))
		dx_fatal ("The DevilutionX assets folder is\nmissing in " DX_DATA_DIR ".");
}

uint32_t dx_fatfs_data_id (void)
{
	return data_id;
}

/*
==============================================================================

SD driver calls

==============================================================================
*/

DRESULT __real_disk_read (BYTE drv, BYTE *buff, LBA_t sector, UINT count);
DRESULT __real_disk_write (BYTE drv, const BYTE *buff, LBA_t sector, UINT count);
DRESULT __real_disk_ioctl (BYTE drv, BYTE cmd, void *buff);

DRESULT __wrap_disk_read (BYTE drv, BYTE *buff, LBA_t sector, UINT count)
{
	DRESULT	r;

	dx_usb_hold ();
	r = __real_disk_read (drv, buff, sector, count);
	dx_usb_release ();
	return r;
}

DRESULT __wrap_disk_write (BYTE drv, const BYTE *buff, LBA_t sector, UINT count)
{
	DRESULT	r;

	dx_usb_hold ();
	r = __real_disk_write (drv, buff, sector, count);
	dx_usb_release ();
	return r;
}

// CTRL_SYNC waits for the card to finish writing (500 ms at most)
DRESULT __wrap_disk_ioctl (BYTE drv, BYTE cmd, void *buff)
{
	DRESULT	r;

	dx_usb_hold ();
	r = __real_disk_ioctl (drv, cmd, buff);
	dx_usb_release ();
	return r;
}

static const char *strip_dotslash (const char *p)
{
	if (p && p[0] == '.' && (p[1] == '/' || p[1] == '\\'))
		return p + 2;
	return p;
}

/*
==============================================================================

fd table

==============================================================================
*/

static FIL *fil_for (int fd)
{
	int		slot = fd - DX_FD_BASE;

	if (slot < 0 || slot >= DX_MAXFDS || !used[slot])
		return NULL;
	return &fils[slot];
}

static BYTE fatfs_mode (int oflag)
{
	BYTE	m;

	switch (oflag & O_ACCMODE)
	{
	case O_WRONLY:	m = FA_WRITE; break;
	case O_RDWR:	m = FA_READ | FA_WRITE; break;
	default:		m = FA_READ; break;
	}
	if (oflag & O_CREAT)
	{
		if (oflag & O_EXCL)
			m |= FA_CREATE_NEW;
		else if (oflag & O_TRUNC)
			m |= FA_CREATE_ALWAYS;
		else
			m |= FA_OPEN_ALWAYS;
	}
	else if (oflag & O_TRUNC)
		m |= FA_CREATE_ALWAYS;
	else
		m |= FA_OPEN_EXISTING;
	if (oflag & O_APPEND)
		m |= FA_OPEN_APPEND;
	return m;
}

// Attaches a fast-seek cluster map to a large file opened read-only. FR_OK also when the
// file gets none (small, no memory). An error means that an SD read failed while the map
// was built; FatFs keeps that error with the file and fails every later call on it.
static FRESULT attach_linkmap (FIL *f, const char *path)
{
	int		i, freeslot = -1;
	DWORD	*t;
	FRESULT	fr;

	if (f_size (f) < FASTSEEK_MIN || strlen (path) >= sizeof(linkmaps[0].path))
		return FR_OK;
	for (i = 0; i < FASTSEEK_FILES; i++)
	{
		if (linkmaps[i].table && !strcasecmp (linkmaps[i].path, path))
		{
			f->cltbl = linkmaps[i].table;
			return FR_OK;
		}
		if (!linkmaps[i].table && freeslot < 0)
			freeslot = i;
	}
	if (freeslot < 0)
		return FR_OK;

	// the table needs 2 DWORDs per fragment plus one; FatFs reports the size needed
	t = malloc (16 * sizeof(DWORD));
	if (!t)
		return FR_OK;
	t[0] = 16;
	f->cltbl = t;
	fr = f_lseek (f, CREATE_LINKMAP);
	if (fr == FR_NOT_ENOUGH_CORE)
	{
		DWORD	need = t[0];
		DWORD	*bigger = realloc (t, need * sizeof(DWORD));

		if (!bigger)
		{
			free (t);
			f->cltbl = NULL;
			return FR_OK;
		}
		t = bigger;
		t[0] = need;
		f->cltbl = t;
		fr = f_lseek (f, CREATE_LINKMAP);
	}
	if (fr != FR_OK)
	{
		free (t);
		f->cltbl = NULL;
		return fr;
	}
	f_lseek (f, 0);
	strcpy (linkmaps[freeslot].path, path);
	linkmaps[freeslot].table = t;
	return FR_OK;
}

static int dx_open (const char *path, int oflag)
{
	int		slot = -1, i;
	FRESULT	fr;

	for (i = 0; i < DX_MAXFDS; i++)
		if (!used[i])
		{
			slot = i;
			break;
		}
	if (slot < 0)
	{
		errno = EMFILE;
		return -1;
	}
	path = strip_dotslash (path);
	fr = f_open (&fils[slot], path, fatfs_mode (oflag));
	if (fr != FR_OK)
	{
		// a card that does not answer is no missing file (libmpq: "open error", not "missing")
		errno = (fr == FR_DISK_ERR || fr == FR_INT_ERR || fr == FR_NOT_READY) ? EIO : ENOENT;
		return -1;
	}
	// A failed SD read while the cluster map is built leaves the file unusable: open it
	// again (which clears the error) and build the map once more. If that fails too, the
	// file goes without one; its seeks then follow the FAT chain.
	if ((oflag & O_ACCMODE) == O_RDONLY)
		for (i = 1; (fr = attach_linkmap (&fils[slot], path)) != FR_OK; i++)
		{
			printf ("dx_fatfs: cluster map of %s failed (FatFs error %d)\n", path, fr);
			f_close (&fils[slot]);
			if (f_open (&fils[slot], path, FA_READ) != FR_OK)
			{
				errno = EIO;
				return -1;
			}
			if (i == LINKMAP_ATTEMPTS)
				break;
		}
	used[slot] = 1;
	return slot + DX_FD_BASE;
}

static int dx_close (int fd)
{
	FIL		*f = fil_for (fd);
	FRESULT	fr;

	if (!f)
		return -1;
	fr = f_close (f);
	used[fd - DX_FD_BASE] = 0;
	return fr == FR_OK ? 0 : -1;
}

static _ssize_t dx_read (int fd, void *buf, size_t nbyte)
{
	FIL		*f = fil_for (fd);
	UINT	br = 0;
	int		prof = dx_prof_enter (DXP_SD);
	FRESULT	fr = f ? f_read (f, buf, (UINT)nbyte, &br) : FR_INVALID_OBJECT;

	dx_prof_leave (prof);
	return fr == FR_OK ? (_ssize_t)br : -1;
}

static _ssize_t dx_write (int fd, const void *buf, size_t nbyte)
{
	FIL		*f = fil_for (fd);
	UINT	bw = 0;
	int		prof = dx_prof_enter (DXP_SD);
	FRESULT	fr = f ? f_write (f, buf, (UINT)nbyte, &bw) : FR_INVALID_OBJECT;

	dx_prof_leave (prof);
	return fr == FR_OK ? (_ssize_t)bw : -1;	// short when the card is full
}

static off_t dx_lseek (int fd, off_t offset, int whence)
{
	FIL		*f = fil_for (fd);
	FSIZE_t	pos;

	if (!f)
		return -1;
	switch (whence)
	{
	case SEEK_SET:	pos = (FSIZE_t)offset; break;
	case SEEK_CUR:	pos = f_tell (f) + offset; break;
	case SEEK_END:	pos = f_size (f) + offset; break;
	default:		return -1;
	}
	if (f_lseek (f, pos) != FR_OK)
		return -1;
	return (off_t)pos;
}

static int console_write (const char *buf, int len)
{
	return stdio_put_string (buf, len, false, true);
}

/*
==============================================================================

POSIX names

==============================================================================
*/

int open (const char *path, int oflag, ...)		{ return dx_open (path, oflag); }
int close (int fd)								{ return dx_close (fd); }
_ssize_t read (int fd, void *buf, size_t n)		{ return dx_read (fd, buf, n); }
off_t lseek (int fd, off_t off, int whence)		{ return dx_lseek (fd, off, whence); }

_ssize_t write (int fd, const void *buf, size_t n)
{
	if (fd == 1 || fd == 2)
		return console_write ((const char *)buf, (int)n);
	return dx_write (fd, buf, n);
}

int access (const char *path, int mode)
{
	FILINFO	fno;

	if (f_stat (strip_dotslash (path), &fno) != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	if ((mode & W_OK) && (fno.fattrib & AM_RDO))
	{
		errno = EACCES;
		return -1;
	}
	return 0;
}

static int dx_stat (const char *path, struct stat *st)
{
	FILINFO	fno;

	if (!st)
	{
		errno = EFAULT;
		return -1;
	}
	if (f_stat (strip_dotslash (path), &fno) != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	memset (st, 0, sizeof(*st));
	st->st_mode = (fno.fattrib & AM_DIR) ? S_IFDIR : S_IFREG;
	st->st_size = (off_t)fno.fsize;
	return 0;
}
int stat (const char *path, struct stat *st)	{ return dx_stat (path, st); }
int _stat (const char *path, struct stat *st)	{ return dx_stat (path, st); }

int mkdir (const char *path, mode_t mode)
{
	FRESULT	fr = f_mkdir (strip_dotslash (path));

	(void)mode;
	if (fr == FR_OK || fr == FR_EXIST)
		return 0;
	errno = (fr == FR_NO_PATH) ? ENOENT : EIO;
	return -1;
}

int unlink (const char *path)		{ return f_unlink (strip_dotslash (path)) == FR_OK ? 0 : -1; }
int _unlink (const char *path)		{ return unlink (path); }
int remove (const char *path)		{ return unlink (path); }

int rename (const char *from, const char *to)
{
	const char	*dst = strip_dotslash (to);

	// POSIX rename replaces an existing target; FatFs refuses (save files are replaced)
	f_unlink (dst);
	return f_rename (strip_dotslash (from), dst) == FR_OK ? 0 : -1;
}

// utils/file_util.cpp ResizeFile (the MPQ save writer shrinks its file)
int truncate (const char *path, off_t length)
{
	FIL		f;
	FRESULT	fr = f_open (&f, strip_dotslash (path), FA_WRITE | FA_OPEN_EXISTING);

	if (fr != FR_OK)
	{
		errno = ENOENT;
		return -1;
	}
	fr = f_lseek (&f, (FSIZE_t)length);
	if (fr == FR_OK)
		fr = f_truncate (&f);
	f_close (&f);
	return fr == FR_OK ? 0 : -1;
}

// No symbolic links on FAT (only the desktop path code would ask).
ssize_t readlink (const char *path, char *buf, size_t size)
{
	(void)path;
	(void)buf;
	(void)size;
	errno = ENOSYS;
	return -1;
}

/*
==============================================================================

newlib syscalls (stdio's FILE * layer)

==============================================================================
*/

int _open (const char *fn, int oflag, ...)		{ return dx_open (fn, oflag); }
int _close (int fd)								{ return dx_close (fd); }
off_t _lseek (int fd, off_t pos, int whence)	{ return dx_lseek (fd, pos, whence); }

int _read (int handle, char *buffer, int length)
{
	if (handle == 0)
		return 0;	// no console input
	return (int)dx_read (handle, buffer, (size_t)length);
}

int _write (int handle, char *buffer, int length)
{
	if (handle == 1 || handle == 2)
		return console_write (buffer, length);
	return (int)dx_write (handle, buffer, (size_t)length);
}

int _fstat (int fd, struct stat *buf)
{
	FIL		*f;

	if (!buf)
		return -1;
	memset (buf, 0, sizeof(*buf));
	if (fd >= 0 && fd <= 2)
	{
		buf->st_mode = S_IFCHR;
		return 0;
	}
	f = fil_for (fd);
	if (!f)
		return -1;
	buf->st_mode = S_IFREG;
	buf->st_size = (off_t)f_size (f);
	return 0;
}

int _isatty (int fd)
{
	// only the console is a tty; a file reported as one would be line buffered
	return (fd >= 0 && fd <= 2) ? 1 : 0;
}
