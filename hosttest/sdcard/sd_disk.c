// sd_disk.c -- FatFs disk I/O on a card image file: the host's tf_card.c.
//
// Used by the harness (DX_SDCARD, see sd_host.c) and by the image builder (sd_mkimg.c).
// For the harness, reads can be made to fail like a card that does not answer:
//   DX_SDCARD_FAIL=n[,n...]   disk_read calls number n fail (counted from 1 at the mount)
//   DX_SDCARD_TRACE=1         every disk_read and disk_write on stderr, with its number
#define _FILE_OFFSET_BITS 64 // images beyond 2 GB in the -m32 harness
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff.h"
#include "diskio.h"

#include "sd_disk.h"

#define SECTOR		512
#define MAX_FAILS	32

static int		image_fd = -1;
static LBA_t	image_sectors;
static unsigned	reads, writes;
static unsigned	fail_at[MAX_FAILS];
static int		fails;
static int		trace;

int sd_disk_open (const char *path, int create_mb)
{
	struct stat	st;
	const char	*env;

	image_fd = open (path, create_mb ? O_RDWR | O_CREAT | O_TRUNC : O_RDWR, 0644);
	if (image_fd < 0)
	{
		fprintf (stderr, "sdcard: cannot open %s: %s\n", path, strerror (errno));
		return -1;
	}
	if (create_mb && ftruncate (image_fd, (off_t)create_mb * 1024 * 1024) != 0)
	{
		fprintf (stderr, "sdcard: cannot size %s: %s\n", path, strerror (errno));
		return -1;
	}
	fstat (image_fd, &st);
	image_sectors = (LBA_t)(st.st_size / SECTOR);

	if ((env = getenv ("DX_SDCARD_FAIL")) != NULL)
		for (char *end; *env && fails < MAX_FAILS; env = *end ? end + 1 : end)
			fail_at[fails++] = (unsigned)strtoul (env, &end, 10);
	env = getenv ("DX_SDCARD_TRACE");
	trace = env && env[0] == '1';
	return 0;
}

unsigned sd_disk_reads (void)
{
	return reads;
}

int sd_disk_trace (void)
{
	return trace;
}

DSTATUS disk_initialize (BYTE drv)
{
	return (drv || image_fd < 0) ? STA_NOINIT : 0;
}

DSTATUS disk_status (BYTE drv)
{
	return (drv || image_fd < 0) ? STA_NOINIT : 0;
}

DRESULT disk_read (BYTE drv, BYTE *buff, LBA_t sector, UINT count)
{
	size_t	len = (size_t)count * SECTOR;

	if (drv || !count)
		return RES_PARERR;
	reads++;
	for (int i = 0; i < fails; i++)
		if (fail_at[i] == reads)
		{
			fprintf (stderr, "sdcard: read #%u (sector %lu, %u sectors) fails (DX_SDCARD_FAIL)\n",
				reads, (unsigned long)sector, count);
			return RES_ERROR;
		}
	if (trace)
		fprintf (stderr, "sdcard: read #%u sector %lu, %u sectors\n", reads, (unsigned long)sector, count);
	if (sector + count > image_sectors
		|| pread (image_fd, buff, len, (off_t)sector * SECTOR) != (ssize_t)len)
		return RES_ERROR;
	return RES_OK;
}

DRESULT disk_write (BYTE drv, const BYTE *buff, LBA_t sector, UINT count)
{
	size_t	len = (size_t)count * SECTOR;

	if (drv || !count)
		return RES_PARERR;
	writes++;
	if (trace)
		fprintf (stderr, "sdcard: write #%u sector %lu, %u sectors\n", writes, (unsigned long)sector, count);
	if (sector + count > image_sectors
		|| pwrite (image_fd, buff, len, (off_t)sector * SECTOR) != (ssize_t)len)
		return RES_ERROR;
	return RES_OK;
}

DRESULT disk_ioctl (BYTE drv, BYTE cmd, void *buff)
{
	if (drv)
		return RES_PARERR;
	switch (cmd)
	{
	case CTRL_SYNC:
		return RES_OK;
	case GET_SECTOR_COUNT:
		*(LBA_t *)buff = image_sectors;
		return RES_OK;
	case GET_BLOCK_SIZE:
		*(DWORD *)buff = 8192;	// 4 MB erase blocks: f_mkfs aligns the data area like the SD Formatter
		return RES_OK;
	default:
		return RES_PARERR;
	}
}
