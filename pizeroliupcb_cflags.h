#ifndef DX_PIZEROLIUPCB_CFLAGS_H
#define DX_PIZEROLIUPCB_CFLAGS_H 1
//
// Board configuration for pico-lutionX on pico_shared HW_CONFIG 18: Liu Szuping's
// PCB for the Waveshare RP2350-PiZero (RP2350B, 16 MB flash), with HSTX video on
// the PCB's HDMI port (the PiZero's own mini HDMI port is not used), a PCM5102
// I2S DAC on the audio jack, two NES/SNES controller ports, the PiZero's SD card
// slot on SPI1 and its PIO-USB port.
//
// Force-included into every translation unit (see CMakeLists.txt). Pin values
// mirror pico_shared BoardConfigs.cmake HW_CONFIG 18.
//
// PSRAM is optional on the PiZero: 8 MB has to be soldered on (QMI CS1 = GPIO
// 47), or Diablo stops with a message. The build uses
// PICO_BOARD=waveshare_rp2350_pizero, as pico_shared does for this config. The
// pico-sdk does not ship that header, so it is in boards/ (copied from
// pico_shared); it declares the RP2350B the SD pins need.
//
// Differences from the Fruit Jam that the code branches on:
//
//   DX_AUDIO_I2S_DRIVER  PCM5102: no codec and no headphone detect, so
//                        dx_audio.c plays to HDMI and I2S at the same time.
//   NES_PIN_*            the two controller ports, polled by dx_nespad.cpp: NES or
//                        SNES pads, or a SNES Mouse (NESPAD_SNES_MOUSE).
//   SDCARD_SPI spi1      the SD card is on SPI1 (GPIO 30/31/40/43).
//   PIN_USB_HOST_DP 28   Pico-PIO-USB on the PiZero's PIO-USB port, no VBUS
//                        switch. A USB hub on that port does not work.
//

#define HW_CONFIG 18		// also selects nespad.cpp's PIO program (separate CLK/LAT per port)
#define DX_BOARD_NAME "Waveshare RP2350-PiZero on Liu Szuping's PCB"

// --- UART (debug console), the board header's default ------------------------
#define PICO_DEFAULT_UART 0
#define PICO_DEFAULT_UART_TX_PIN 0
#define PICO_DEFAULT_UART_RX_PIN 1

// --- HSTX / HDMI video lanes (read by pico_hdmi/video_output.c) -------------
// Same lanes as the Fruit Jam, also inverted (D- = D+ - 1).
#define HSTX_CKP 13
#define HSTX_D0P 15
#define HSTX_D1P 17
#define HSTX_D2P 19
#define GPIOHSTXCK HSTX_CKP
#define GPIOHSTXD0 HSTX_D0P
#define GPIOHSTXD1 HSTX_D1P
#define GPIOHSTXD2 HSTX_D2P
#define GPIOHSTXINVERTED 1
#define USE_HSTX 1

// --- I2S audio: PCM5102 DAC (the PCB's audio jack) ---------------------------
#define DX_AUDIO_I2S_DRIVER PICO_AUDIO_I2S_DRIVER_PCM5000A
#define PICO_AUDIO_I2S_DATA_PIN 9
#define PICO_AUDIO_I2S_CLOCK_PIN_BASE 10
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

// --- SD card (hardware SPI1; PIO-SPI fallback available) --------------------
// The PiZero's own card slot. GPIO 40 is only in the vendored pico_fatfs SPI1
// table on RP2350B, hence the board header above.
#define SD_TX 31   // MOSI
#define SD_RX 40   // MISO
#define SD_SCK 30
#define SD_CS 43
#define USE_SD 1
#define SDCARD_SPI spi1
#define SDCARD_PIO pio1

// --- PSRAM (8 MB on QMI CS1 = GPIO 47, soldered onto the PiZero) -------------
#define PSRAM_CS_PIN 47

// --- LED: blinks while assets are copied into the flash cache (dx_flash.c) ---
#define DX_LED_PIN 25

// --- NES/SNES controller ports (pico_shared nespad, PIO) ---------------------
// Each port has its own CLK and LAT. The PCB's ESP32 socket (BlueRetro, for
// Bluetooth controllers) shares these pins: remove the module to use the ports.
#define NES_PIN_CLK 2
#define NES_PIN_DATA 4
#define NES_PIN_LAT 3
#define NES_PIO pio1
#define NES_PIN_CLK_1 5
#define NES_PIN_DATA_1 7
#define NES_PIN_LAT_1 6
#define NES_PIO_1 pio1
// An original SNES Mouse in either port. Each read clocks 32 bits instead of 18
// (about 390 us instead of 220 us), which dx_nespad.cpp collects on the next tick.
#define NESPAD_SNES_MOUSE 1

// --- USB host on Pico-PIO-USB -----------------------------------------------
// The PiZero's PIO-USB port, D+ on GPIO 28 and D- on GPIO 29. No VBUS-enable
// GPIO, so PIN_USB_HOST_VBUS stays undefined.
#define HAS_USBPIO
#define PIN_USB_HOST_DP (28u)
#define PIN_USB_HOST_DM (29u)

#endif // DX_PIZEROLIUPCB_CFLAGS_H
