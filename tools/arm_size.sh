#!/bin/bash
# Links the complete engine for the RP2350 (tools/sizeprobe) and reports what the firmware
# will need: code and read-only data (flash), static data (PSRAM on the board) and the
# largest stack frames.
#
#   tools/arm_size.sh [variant...]     variants: os o2 (default), o2lto
#
# o2lto does not link yet: pico-sdk's --wrap'ed C library functions and LTO do not mix.
#
# Build directories go to ${TMPDIR:-/tmp}/pico-lutionX-sizeprobe-<variant>.
# Uses PICO_SDK_PATH from the environment.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
VARIANTS="${*:-os o2}"
FRAME_LIMIT=4096

for v in $VARIANTS; do
	BUILD="${TMPDIR:-/tmp}/pico-lutionX-sizeprobe-$v"
	case $v in
	os) ARGS="-DCMAKE_BUILD_TYPE=MinSizeRel -DDX_PROBE_LTO=OFF" ;;
	o2) ARGS="-DCMAKE_BUILD_TYPE=Release -DDX_PROBE_LTO=OFF" ;;
	o2lto) ARGS="-DCMAKE_BUILD_TYPE=Release -DDX_PROBE_LTO=ON" ;;
	*) echo "unknown variant $v" >&2; exit 1 ;;
	esac
	cmake -S "$HERE/sizeprobe" -B "$BUILD" -G Ninja $ARGS \
		-DCMAKE_C_FLAGS_RELEASE="-O2 -DNDEBUG" -DCMAKE_CXX_FLAGS_RELEASE="-O2 -DNDEBUG" > "$BUILD.log" 2>&1 ||
		{ echo "configure failed, see $BUILD.log"; exit 1; }
	cmake --build "$BUILD" -j"$(nproc)" >> "$BUILD.log" 2>&1 || { echo "build failed, see $BUILD.log"; exit 1; }
	ELF="$BUILD/dx_sizeprobe.elf"
	echo "== $v"
	arm-none-eabi-size -A "$ELF" | awk '
		$1 ~ /^\.(text|rodata|ARM\.extab|ARM\.exidx|binary_info)$/ {flash += $2}
		$1 == ".data" {data = $2}
		$1 == ".bss" {bss = $2}
		END {printf "flash (code + read-only data + .data image): %8d bytes\n", flash + data
		     printf "static RAM (.data + .bss):                   %8d bytes\n", data + bss}'
done

# Stack frames and static data of the last variant built.
echo "== stack frames > $FRAME_LIMIT bytes ($v)"
find "$BUILD" -name '*.su' -exec cat {} + | sed 's|^.*/Source/||; s|^.*/pico-lutionX/||' |
	awk -F'\t' -v lim="$FRAME_LIMIT" '$2 + 0 > lim + 0 {printf "%8d  %s\n", $2, $1}' | sort -rn | head -15
echo "== largest static data symbols ($v)"
arm-none-eabi-nm -S -C --size-sort "$ELF" | awk '$3 ~ /^[bBdD]$/' | tail -12 |
	awk '{printf "%8d  %s\n", strtonum("0x" $2), substr($0, index($0, $4))}' | sort -rn
