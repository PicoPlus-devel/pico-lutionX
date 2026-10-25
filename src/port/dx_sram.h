// dx_sram.h -- hot data and code in the RP2350's SRAM instead of PSRAM and flash, where
// every access goes through the 16 KB XIP cache that both share. No effect in the test
// program.
//
//   DX_SRAM_BSS   zero-initialised data (cmake/psram_linker.cmake: .sram_bss, which crt0
//                 zeroes); never for data with an initial value
//   DX_SRAM_CODE  a function, copied into SRAM at boot (the SDK's .time_critical)

#pragma once

#ifdef __arm__
#define DX_SRAM_BSS __attribute__((section(".sram_bss")))
#define DX_SRAM_CODE __attribute__((section(".time_critical.dx"), noinline))
#else
#define DX_SRAM_BSS
#define DX_SRAM_CODE
#endif
