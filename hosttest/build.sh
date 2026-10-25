#!/bin/sh
# Build the Linux host harness from the same engine sources as the firmware.
#   hosttest/build.sh           -> hosttest/dx_host   (-m32, ASan + UBSan)
#   NOSAN=1 hosttest/build.sh   -> hosttest/dx_host_nosan (no sanitizers; faster)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
SAN=ON
BUILD="$HERE/build"
if [ -n "$NOSAN" ]; then
	SAN=OFF
	BUILD="$HERE/build-nosan"
fi
cmake -S "$HERE" -B "$BUILD" -G Ninja -DDX_SANITIZE=$SAN -DDX_NOSOUND=OFF > /dev/null
cmake --build "$BUILD" -j"$(nproc)"
[ "$SAN" = ON ] && echo "built $HERE/dx_host" || echo "built $HERE/dx_host_nosan"
