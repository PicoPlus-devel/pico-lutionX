#!/bin/sh
# Build pico-lutionX for pico_shared HW_CONFIG 18 (Liu Szuping's PCB for the
# Waveshare RP2350-PiZero), linked to run from the pico-bootLoader's app
# partition instead of from the start of flash.
#
# Differences from ./pizeroliupcb-build.sh, which does the actual build:
#   * -DBUILD_FOR_BOOTLOADER=ON, which moves FLASH to ORIGIN 0x10080000 (see
#     cmake/BootPartition.cmake for the map) and defines BUILD_FOR_BOOTLOADER=1
#     for the C code.
#   * a separate build directory, so the two variants never share a CMake cache.
#
# pico-bootLoader's build_emulators.sh runs this script. To install a build by
# hand, copy the UF2 to /emu/18/ on the bootloader's SD card; the game data stays
# in /roms/devilutionx. Quitting the game, or any reset, returns to the boot menu.
#
# BUILD=<dir> overrides the build directory.
# Artifact: build_bl_pizeroliupcb/src/pico/pico_lutionx.uf2
set -e
cd "$(dirname "$0")"
BUILD=${BUILD:-build_bl_pizeroliupcb}
export BUILD
exec ./pizeroliupcb-build.sh -DBUILD_FOR_BOOTLOADER=ON "$@"
