#ifndef DX_ADAFRUITDVISD_CFLAGS_H
#define DX_ADAFRUITDVISD_CFLAGS_H 1
//
// Board configuration for pico-lutionX on pico_shared HW_CONFIG 2: a Pimoroni Pico
// Plus 2 on a breadboard or Frank's PCB, with the Adafruit DVI Breakout (product
// 4984) on HSTX, the Adafruit MicroSD breakout (product 254) on SPI0, an optional
// PCM5100A I2S DAC and two NES/SNES controller ports.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico_shared BoardConfigs.cmake HW_CONFIG 2 and pico-duke3D's
// adafruitdvisd_cflags.h.
//
// The module must carry 8 MB of PSRAM on QMI CS1 = GPIO 47, i.e. be an RP2350B
// such as the Pico Plus 2. The build uses PICO_BOARD=pico2, as pico_shared's bld.sh
// does for this config; that header declares 4 MB of flash, which the build
// overrides (cmake/BootPartition.cmake).
//
// Differences from the Fruit Jam that the code branches on:
//
//   GPIOHSTXINVERTED 0   the DVI breakout wires D- = D+ + 1.
//   DX_AUDIO_I2S_DRIVER  PCM5100A: no codec and no headphone detect, so
//                        dx_audio.c plays to HDMI and I2S at the same time.
//                        Harmless with no DAC fitted.
//   NES_PIN_*            the two controller ports, polled by dx_nespad.cpp: NES or
//                        SNES pads, or a SNES Mouse (NESPAD_SNES_MOUSE).
//   no HAS_USBPIO        the USB host is the RP2350's own controller, on the
//                        module's USB-C port (through an OTG adapter or Y-cable),
//                        as pico_shared builds this config by default and as
//                        pico-doom and pico-duke3D do. PLL_USB stays at 48 MHz
//                        for it, and clk_hstx runs from clk_sys (dx_boot.c).
//

#define HW_CONFIG 2		// also selects nespad.cpp's PIO program (separate CLK/LAT per port)
#define DX_BOARD_NAME "Pico Plus 2 + Adafruit DVI and SD breakouts"

// --- UART (debug console), the pico2 default ----------------------------------
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Adafruit DVI Breakout 4984, not inverted.
#define GPIOHSTXCK 14
#define GPIOHSTXD0 12
#define GPIOHSTXD1 18
#define GPIOHSTXD2 16
#define GPIOHSTXINVERTED 0
#define USE_HSTX 1

// --- I2S audio: optional PCM5100A DAC ---------------------------------------
#define DX_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_PCM5000A
#define PICO_AUDIO_I2S_DATA_PIN 26
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 27
#define PICO_AUDIO_I2S_CLOCK_PINS_SWAPPED 0
#define PICO_AUDIO_I2S_PIO 1
#define PICO_AUDIO_I2S_RESET_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_PIN -1
#define PICO_AUDIO_I2S_INTERRUPT_IS_BUTTON 0
#define I2S_AUDIO_COMPENSATE_DC_OFFSET 1	// PCM510x: DC blocker in audio_i2s.c
#define I2S_AUDIO_RING_SIZE 4096   // samples (16 KB); dx_audio.c keeps half of it queued
// No TLV320 and no Wii port. tlv320dac3100.c is still compiled and uses
// WIIPAD_I2C as an i2c_inst_t *, so it must name a valid instance.
#define WIIPAD_I2C i2c1
#define WII_PIN_SDA -1
#define WII_PIN_SCL -1

// --- SD card (hardware SPI0; PIO-SPI fallback available) --------------------
// Adafruit MicroSD breakout 254
#define SD_TX 3    // MOSI
#define SD_RX 4    // MISO
#define SD_SCK 2
#define SD_CS 5
#define USE_SD 1
#define SDCARD_SPI spi0
#define SDCARD_PIO pio1

// --- PSRAM (8 MB on QMI CS1 = GPIO 47) --------------------------------------
#define PSRAM_CS_PIN 47

// --- LED: blinks while assets are copied into the flash cache (dx_flash.c) ---
#define DX_LED_PIN 25

// --- NES/SNES controller ports (pico_shared nespad, PIO) ---------------------
// Each port has its own CLK and LAT.
#define NES_PIN_CLK 6
#define NES_PIN_DATA 7
#define NES_PIN_LAT 8
#define NES_PIO pio1
#define NES_PIN_CLK_1 9
#define NES_PIN_DATA_1 10
#define NES_PIN_LAT_1 11
#define NES_PIO_1 pio1
// An original SNES Mouse in either port. Each read clocks 32 bits instead of 18
// (about 390 us instead of 220 us), which dx_nespad.cpp collects on the next tick.
#define NESPAD_SNES_MOUSE 1

// --- USB host: the native controller (no HAS_USBPIO, see above) ---------------
// The PicoNES PCB has no USB socket of its own: USB devices go on the module's
// USB-C port. Pico-PIO-USB on GPIO 20/21 (pico_shared's -u build of this
// config) would need HAS_USBPIO and PIN_USB_HOST_DP/DM (20u/21u) here.

#endif // DX_ADAFRUITDVISD_CFLAGS_H
