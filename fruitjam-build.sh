#!/bin/sh
# Build pico-lutionX for the Adafruit Fruit Jam (HW_CONFIG 8).
#
# Uses PICO_SDK_PATH from the environment. BUILD=<dir> overrides the build
# directory (buildAll.sh builds releases in a fresh one).
# Artifact: build_fruitjam/src/pico/pico_lutionx.uf2
set -e
TAG=fruitjam
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
    -DPICO_BOARD=adafruit_fruit_jam \
    -DDX_BOARD=${TAG} \
    ${CMAKE_ARGS} "$@"

cmake --build "$BUILD" -j"$(nproc)"

echo
echo "Artifact: $BUILD/src/pico/pico_lutionx.uf2"
