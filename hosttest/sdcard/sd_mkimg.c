// sd_mkimg.c -- builds a card image for the harness's SD card mode (DX_SDCARD).
//
//   dx_sdimg [-t fat32|exfat] [-s size-MB] [-c cluster-KB] [-f fragments] <image> <host-dir>=<card-dir>...
//
// Formats the image with FatFs (one MBR partition, like a card from the shop or the SD
// Formatter) and copies each host directory, following symbolic links, into the card
// directory. Defaults: fat32, 8192 MB (sparse), 32 KB clusters for FAT32 and 128 KB for exFAT.
// -f N splits every file of 1 MB or more into N fragments (a one-cluster gap after each),
// as on a card that was written to before; exFAT then keeps a FAT chain for the file.
#define _XOPEN_SOURCE 700 // nftw
#include <errno.h>
#include <ftw.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include "ff.h"

#include "sd_disk.h"

#define CHUNK		(64 * 1024)
#define FRAG_MIN	(1024 * 1024)

static FATFS	fs;
static unsigned	fragments = 1;
static unsigned	fillers;
static unsigned	copied, skipped;

static void usage(void)
{
	fprintf(stderr, "usage: dx_sdimg [-t fat32|exfat] [-s size-MB] [-c cluster-KB] [-f fragments] <image> <host-dir>=<card-dir>...\n");
	exit(2);
}

static void die(const char *what, const char *path, FRESULT fr)
{
	fprintf(stderr, "dx_sdimg: %s %s: FatFs error %d\n", what, path, fr);
	exit(1);
}

// mkdir -p on the card
static void card_mkdirs(const char *path)
{
	char buf[512];

	snprintf(buf, sizeof(buf), "%s", path);
	for (char *p = buf + 1; ; p++) {
		if (*p == '/' || *p == '\0') {
			char c = *p;
			FRESULT fr;

			*p = '\0';
			fr = f_mkdir(buf);
			if (fr != FR_OK && fr != FR_EXIST)
				die("mkdir", buf, fr);
			*p = c;
			if (c == '\0')
				break;
		}
	}
}

// a one-cluster file right behind what was written last: the next cluster of the file
// being copied comes after it
static void add_filler(void)
{
	char name[32];
	FIL f;
	UINT bw;

	snprintf(name, sizeof(name), "/~frag%04u.tmp", fillers++);
	if (f_open(&f, name, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK || f_write(&f, "x", 1, &bw) != FR_OK)
		die("write", name, FR_DISK_ERR);
	f_close(&f);
}

static void remove_fillers(void)
{
	for (unsigned i = 0; i < fillers; i++) {
		char name[32];

		snprintf(name, sizeof(name), "/~frag%04u.tmp", i);
		f_unlink(name);
	}
}

static void copy_file(const char *src, const char *dst, off_t size)
{
	static char buf[CHUNK];
	FILE *in = fopen(src, "rb");
	off_t done = 0, part = size, next;
	FRESULT fr;
	FIL out;

	if (in == NULL) {
		fprintf(stderr, "dx_sdimg: cannot read %s: %s\n", src, strerror(errno));
		exit(1);
	}
	if ((fr = f_open(&out, dst, FA_WRITE | FA_CREATE_ALWAYS)) != FR_OK) {
		// e.g. a Windows "file:Zone.Identifier" copied along: no valid name on FAT
		fprintf(stderr, "dx_sdimg: skipped %s (FatFs error %d)\n", dst, fr);
		fclose(in);
		skipped++;
		return;
	}
	if (fragments > 1 && size >= FRAG_MIN)
		part = (size + fragments - 1) / fragments;
	next = part;
	while (done < size) {
		size_t want = (size_t)(size - done < CHUNK ? size - done : CHUNK);
		size_t n;
		UINT bw;

		if (done + (off_t)want > next)
			want = (size_t)(next - done);
		n = fread(buf, 1, want, in);
		if (n != want || f_write(&out, buf, (UINT)n, &bw) != FR_OK || bw != n)
			die("write", dst, FR_DENIED);
		done += (off_t)n;
		if (done == next && done < size) {
			f_sync(&out);
			add_filler();
			next += part;
		}
	}
	fclose(in);
	if ((fr = f_close(&out)) != FR_OK)
		die("close", dst, fr);
	copied++;
}

static size_t		src_len;
static const char	*dst_root;

static int copy_entry(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
	char t[512];

	(void)ftw;
	snprintf(t, sizeof(t), "%s%s", dst_root, path + src_len);
	if (type == FTW_D)
		card_mkdirs(t);
	else if (type == FTW_F && S_ISREG(st->st_mode))
		copy_file(path, t, st->st_size);
	return 0;
}

// symbolic links are followed (no FTW_PHYS)
static void copy_tree(const char *src, const char *dst)
{
	src_len = strlen(src);
	dst_root = dst;
	if (nftw(src, copy_entry, 16, 0) != 0) {
		fprintf(stderr, "dx_sdimg: cannot list %s: %s\n", src, strerror(errno));
		exit(1);
	}
}

int main(int argc, char **argv)
{
	static BYTE work[CHUNK];
	MKFS_PARM opt = { FM_FAT32, 0, 0, 0, 0 };
	unsigned cluster_kb = 0;
	int size_mb = 8192, c;
	FRESULT fr;

	while ((c = getopt(argc, argv, "t:s:c:f:")) != -1) {
		switch (c) {
		case 't':
			if (strcmp(optarg, "fat32") == 0)
				opt.fmt = FM_FAT32;
			else if (strcmp(optarg, "exfat") == 0)
				opt.fmt = FM_EXFAT;
			else
				usage();
			break;
		case 's': size_mb = atoi(optarg); break;
		case 'c': cluster_kb = (unsigned)atoi(optarg); break;
		case 'f': fragments = (unsigned)atoi(optarg); break;
		default: usage();
		}
	}
	if (optind + 2 > argc || size_mb <= 0 || fragments == 0)
		usage();
	if (cluster_kb == 0)
		cluster_kb = opt.fmt == FM_EXFAT ? 128 : 32;
	opt.au_size = cluster_kb * 1024;

	if (sd_disk_open(argv[optind], size_mb) != 0)
		return 1;
	if ((fr = f_mkfs("", &opt, work, sizeof(work))) != FR_OK)
		die("format", argv[optind], fr);
	if ((fr = f_mount(&fs, "", 1)) != FR_OK)
		die("mount", argv[optind], fr);
	for (int i = optind + 1; i < argc; i++) {
		char *eq = strchr(argv[i], '=');

		if (eq == NULL || eq[1] != '/')
			usage();
		*eq = '\0';
		copy_tree(argv[i], eq + 1);
	}
	remove_fillers();
	f_unmount("");
	printf("dx_sdimg: %s, %s, %d MB, %u KB clusters: %u files copied, %u skipped%s\n",
	    argv[optind], opt.fmt == FM_EXFAT ? "exFAT" : "FAT32", size_mb, cluster_kb, copied, skipped,
	    fragments > 1 ? ", large files fragmented" : "");
	return 0;
}
