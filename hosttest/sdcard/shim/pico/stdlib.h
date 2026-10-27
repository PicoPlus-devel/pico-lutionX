// Host stand-in for the Pico SDK header included by src/pico/dx_fatfs_io.c (sd_host.c).
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifndef KHZ
#define KHZ 1000
#define MHZ 1000000
#endif

typedef unsigned int uint;

void sleep_ms(uint32_t ms);
