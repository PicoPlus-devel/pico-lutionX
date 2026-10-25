#!/bin/sh
# ====================================================================================
# pico-lutionX build-all script
#
# Builds every released file into releases/. This is the whole body of the release
# workflow (.github/workflows/BuildAndRelease.yml), so running it locally rehearses a
# release.
#
#   export PICO_SDK_PATH=/path/to/pico-sdk
#   export PICO_PIO_USB_PATH=/path/to/Pico-PIO-USB
#   ./buildAll.sh
#
# Optional:
#   DX_RELEASE_VERSION=v0.1-alpha   version stamp in the UF2 binary info (picotool info)
#
# Released:
#   pico-lutionX_<board>.uf2        the firmware, standalone build, one per board
#   pico-lutionX_sdcard.zip         DevilutionX's assets for the SD card (no game data)
#
# pico-bootLoader builds (<tag>-build-forbootloader.sh) are not released here: the
# pico-bootLoader repo builds them from this repo's latest release tag.
# ====================================================================================
cd "$(dirname "$0")" || exit 1

RELEASE_BOARDS="fruitjam adafruitdvisd"

# --- tools -------------------------------------------------------------------
if [ -z "$PICO_SDK_PATH" ] || [ ! -f "$PICO_SDK_PATH/pico_sdk_init.cmake" ]; then
    echo "PICO_SDK_PATH is not set or is not a Pico SDK checkout" >&2
    exit 1
fi
# Every board drives USB host over PIO.
if [ -z "$PICO_PIO_USB_PATH" ]; then
    echo "PICO_PIO_USB_PATH is not set" >&2
    exit 1
fi
for TOOL in ninja picotool python3; do
    if ! command -v $TOOL > /dev/null 2>&1; then
        echo "$TOOL could not be found" >&2
        exit 1
    fi
done

[ -d releases ] && rm -rf releases
mkdir releases || exit 1

# --- firmware ------------------------------------------------------------------
# A fresh build directory per release build: CMake options are sticky in
# CMakeCache.txt, so reusing a development tree could carry options into a release.
for TAG in $RELEASE_BOARDS; do
    echo ""
    echo "=================== $TAG ==================="
    BUILD="build_release_${TAG}"
    rm -rf "$BUILD"
    BUILD="$BUILD" CMAKE_ARGS="${CMAKE_ARGS} -DDX_RELEASE_VERSION=${DX_RELEASE_VERSION:-dev}" \
        ./${TAG}-build.sh || exit 1
    cp "$BUILD/src/pico/pico_lutionx.uf2" "releases/pico-lutionX_${TAG}.uf2" || exit 1
done

# --- SD card -------------------------------------------------------------------
echo ""
echo "=================== SD card ==================="
python3 -I tools/make_sdcard_zip.py releases/pico-lutionX_sdcard.zip || exit 1

# -------------------------------------------------------------------------------
echo ""
echo "=================== releases/ ==================="
for UF2 in releases/*.uf2; do
    ls -l "$UF2"
    picotool info "$UF2"
    echo ""
done
ls -l releases/*.zip
echo "$(ls releases | wc -l) files in releases/"
