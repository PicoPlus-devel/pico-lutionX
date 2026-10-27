// FatFs configuration of the image builder (sd_mkimg.c): the board's, plus f_mkfs.
#include BOARD_FFCONF

#undef FF_USE_MKFS
#define FF_USE_MKFS 1
