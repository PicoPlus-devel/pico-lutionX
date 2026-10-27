// sd_disk.h -- the card image behind FatFs's disk I/O (sd_disk.c).
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// Opens the image (create_mb > 0: creates it, sparse, with that size). 0 = OK.
int sd_disk_open(const char *path, int create_mb);
// disk_read calls so far.
unsigned sd_disk_reads(void);
// DX_SDCARD_TRACE is on.
int sd_disk_trace(void);

#ifdef __cplusplus
}
#endif
