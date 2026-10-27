// Host stand-in for the Pico SDK header included by src/pico/dx_fatfs_io.c (sd_host.c).
#pragma once

#include <stdbool.h>

typedef struct pio_host *PIO;

#define pio0 ((PIO)0)
#define pio1 ((PIO)1)

int pio_claim_unused_sm(PIO pio, bool required);
