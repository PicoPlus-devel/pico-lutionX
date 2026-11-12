#!/bin/sh
# Build pico-lutionX for pico_shared HW_CONFIG 18: Liu Szuping's PCB for the
# Waveshare RP2350-PiZero (see pizeroliupcb_cflags.h for the wiring).
#
# PICO_BOARD=waveshare_rp2350_pizero, as pico_shared's bld.sh uses for this
# config. The header is in boards/ (the pico-sdk does not ship it). The PiZero
# needs 8 MB of PSRAM soldered on (GPIO 47).
#
# Uses PICO_SDK_PATH and PICO_PIO_USB_PATH from the environment. BUILD=<dir>
# overrides the build directory (buildAll.sh builds releases in a fresh one).
# Artifact: build_pizeroliupcb/src/pico/pico_lutionx.uf2
set -e
TAG=pizeroliupcb
BUILD=${BUILD:-build_${TAG}}

if [ -z "${PICO_SDK_PATH}" ]; then
    echo "PICO_SDK_PATH is not set" >&2
    exit 1
fi

cmake -S . -B "$BUILD" \
    -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DPICO_SDK_PATH="${PICO_SDK_PATH}" \
    -DPICO_PLATFORM=rp2350-arm-s \
    -DPICO_BOARD=waveshare_rp2350_pizero \
    -DDX_BOARD=${TAG} \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/pico_lutionx.uf2"
