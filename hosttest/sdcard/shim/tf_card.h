// Host stand-in for pico_fatfs's tf_card.h (the SD card driver): the card is an image file
// (sd_disk.c), the configuration calls only report what the board would print (sd_host.c).
#pragma once

#include "pico/stdlib.h"
#include "hardware/pio.h"

#define CLK_SLOW_DEFAULT     (100 * KHZ)
#define CLK_FAST_DEFAULT     (50 * MHZ)
#define CLK_FAST_DEFAULT_PIO (20 * MHZ)

typedef struct spi_host spi_inst_t;

#define spi0 ((spi_inst_t *)0)
#define spi1 ((spi_inst_t *)1)

typedef struct _pico_fatfs_spi_config_t {
	spi_inst_t *spi_inst;
	uint clk_slow;
	uint clk_fast;
	uint pin_miso;
	uint pin_cs;
	uint pin_sck;
	uint pin_mosi;
	bool pullup;
} pico_fatfs_spi_config_t;

#ifdef __cplusplus
extern "C" {
#endif

bool pico_fatfs_set_config(pico_fatfs_spi_config_t *config);
void pico_fatfs_config_spi_pio(PIO pio, uint sm);
uint spi_get_index(spi_inst_t *spi);

#ifdef __cplusplus
}
#endif
