// Host stand-in for the Pico SDK header included by src/pico/dx_fatfs_io.c (sd_host.c).
#pragma once

#include <stdbool.h>

int stdio_put_string(const char *s, int len, bool newline, bool cr_translation);
