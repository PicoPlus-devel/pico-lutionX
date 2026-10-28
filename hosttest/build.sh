#!/bin/sh
# Build the Linux host harness from the same engine sources as the firmware (see README.md).
#   hosttest/build.sh           -> hosttest/dx_host   (-m32, ASan + UBSan)
#                                  hosttest/dx_host_sdl (the same in a window, when the
#                                  32-bit SDL2 package libsdl2-dev:i386 is installed)
#                                  hosttest/dx_sdimg  (SD card images)
#   NOSAN=1 hosttest/build.sh   -> hosttest/dx_host_nosan, hosttest/dx_host_sdl_nosan
#                                  (no sanitizers; faster)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SAN=ON
SUFFIX=
BUILD="$HERE/build"
if [ -n "$NOSAN" ]; then
	SAN=OFF
	SUFFIX=_nosan
	BUILD="$HERE/build-nosan"
fi
cmake -S "$HERE" -B "$BUILD" -G Ninja -DDX_SANITIZE=$SAN -DDX_NOSOUND=OFF > /dev/null
cmake --build "$BUILD" -j"$(nproc)"
echo "built $HERE/dx_host$SUFFIX"
grep -q '^SDL2_I386_FOUND:INTERNAL=1' "$BUILD/CMakeCache.txt" && echo "built $HERE/dx_host_sdl$SUFFIX"
echo "built $HERE/dx_sdimg"
