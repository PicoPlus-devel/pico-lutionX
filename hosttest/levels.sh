#!/bin/sh
# Loads dungeon levels in the test program with the board's PSRAM and flash asset cache, and
# reports per level what is left. Works with either game data, unlike sweep.sh (whose demo
# fixture is a shareware hero): a new hero is created through the menus and the debug
# command +goto takes it to the level.
#
#   hosttest/levels.sh <data-dir> [cold|warm] [levels...]   (default: cold, levels 1-16)
#
#   cold   every level on an empty flash cache (a first visit with nothing cached)
#   warm   the levels in order on one flash cache kept from run to run, as in a game; levels
#          may repeat (warm 3 1 4 1 5 ...). Each run is one trip: start, town, the level.
#
# <data-dir> holds DIABDAT.MPQ (or spawn.mpq, levels 1-4; --spawn is then added) and an
# `assets` folder. Needs NOSAN=1 hosttest/build.sh (DX_HOST=<binary> runs another build).
# Output per level: the PSRAM heap after the level loaded (live, largest free block), the
# flash cache (used, erased, written, estimated time of the flash work on the board, stores
# that found no room) and the result; a failed level's log ends in the cache's usage report
# and map (dx_fcache_print_usage, dx_fcache_print_map).
#
# Warm runs end with a summary of the flash wear once every listed level has been visited
# (the steady state; with fewer than 10 such trips, all trips after the first): per trip
# the flash erased, directory commits, erases of the most and of the average erased 4 KB
# sector, and the evictions of recently used entries.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="${DX_HOST:-$HERE/dx_host_nosan}"
[ -x "$HOST" ] || { echo "build first: NOSAN=1 hosttest/build.sh" >&2; exit 1; }
DATA="$(cd "$1" && pwd)"
shift
MODE=cold
case "$1" in cold | warm) MODE=$1; shift ;; esac
LEVELS="${*:-1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16}"
SPAWN=
if [ -e "$DATA/spawn.mpq" ] && [ ! -e "$DATA/DIABDAT.MPQ" ] && [ ! -e "$DATA/diabdat.mpq" ]; then
	SPAWN=--spawn
fi
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# main menu -> Single Player -> (no heroes yet) New Hero -> Warrior -> name -> new game
cat > "$WORK/newhero.script" <<'EOF'
200 key RETURN
400 key RETURN
600 key RETURN
800 text Tester
900 key RETURN
1100 key RETURN
1300 key RETURN
3000 exit
EOF

trip=0
for level in $LEVELS; do
	trip=$((trip + 1))
	rm -rf "$WORK/save"
	mkdir -p "$WORK/save"
	[ "$MODE" = cold ] && rm -f "$WORK/flash.bin" "$WORK/flash.bin.wear"
	set +e
	(cd "$WORK" && DX_FCACHE=1 DX_FCACHE_FILE="$WORK/flash.bin" DX_SCRIPT="$WORK/newhero.script" \
		"$HOST" --data-dir "$DATA" --save-dir "$WORK/save" --config-dir "$WORK/save" -n $SPAWN \
		+goto "$level" > "$WORK/run.log" 2>&1)
	rc=$?
	set -e
	[ -e "$WORK/flash.bin.wear" ] && cp "$WORK/flash.bin.wear" "$WORK/wear.$trip"
	python3 -I - "$WORK/run.log" "$level" "$rc" "$trip" "$MODE" "$WORK/trips.txt" <<'EOF'
import re
import sys

log, level, rc, trip, mode, trips = sys.argv[1], int(sys.argv[2]), int(sys.argv[3]), int(sys.argv[4]), sys.argv[5], sys.argv[6]
text = open(log, errors='replace').read()
mark = re.search(r'^MARK level %d .*' % level, text, re.M)
cache = re.search(r'dx_fcache: \d+ entries, (\d+) KB used of (\d+) KB.* (\d+) evicted(?: \((\d+) recent\))?.*?; '
                  r'(\d+) KB erased, (\d+) KB programmed \(about ([\d.]+) s', text)
noroom = len(re.findall(r'no room for', text))
ok = bool(mark) and rc == 0
if ok:
    live = int(re.search(r'psram: live +(\d+)', mark.group(0)).group(1))
    free = int(re.search(r'largest-free +(\d+)', mark.group(0)).group(1))
    psram = f'PSRAM live {live // 1024:5d} KB, largest free {free // 1024:5d} KB'
    result = 'ok'
else:
    psram = 'PSRAM ' + ('out of memory' if 'PSRAM heap full' in text else 'level not loaded')
    result = f'FAILED (exit {rc})'
if cache:
    used, cap, evicted, recent, erased, written, secs = cache.groups()
    flash = f'cache {used}/{cap} KB, {erased} KB erased, {written} KB written ({secs} s)'
else:
    recent, erased, written, secs = None, 0, 0, 0
    flash = 'cache -'
prefix = f'trip {trip:3d} ' if mode == 'warm' else ''
print(f'{prefix}level {level:2d}: {psram} | {flash}, {noroom} without room | {result}')
with open(trips, 'a') as f:
    f.write(f'{level} {int(ok)} {erased} {written} {secs} {recent if recent is not None else -1}\n')
EOF
	if [ "$rc" -ne 0 ] || ! grep -q "^MARK level $level " "$WORK/run.log"; then
		cp "$WORK/run.log" "levels-failed-$level.log"
		echo "  log: levels-failed-$level.log"
	fi
done

[ "$MODE" = warm ] || exit 0
python3 -I - "$WORK" <<'EOF'
import os
import struct
import sys

work = sys.argv[1]
trips = [line.split() for line in open(os.path.join(work, 'trips.txt'))]
n = len(trips)
levels = [int(t[0]) for t in trips]
failed = [i + 1 for i, t in enumerate(trips) if t[1] == '0']

# steady state: the trips after every listed level has been visited once
seen, warm = set(), n
for i, level in enumerate(levels):
    seen.add(level)
    if len(seen) == len(set(levels)):
        warm = i + 1
        break
if n - warm < 10:
    warm = 1 if n > 1 else 0  # too few repeats: all trips after the first (an empty cache)


def wear(i):
    path = os.path.join(work, f'wear.{i}')
    if i == 0 or not os.path.exists(path):
        return None
    b = open(path, 'rb').read()
    return struct.unpack(f'<{len(b) // 4}I', b)


end = wear(n)
if end is None:
    sys.exit(0)
start = wear(warm) or (0,) * len(end)
steady = n - warm
d = [b - a for a, b in zip(start, end)]
DIR = 64  # two 128 KB directory slots of 4 KB sectors
data = d[DIR:]
commits = d[0] + d[32]
hot = max(data)
steady_trips = trips[warm:]
erased = sum(int(t[2]) for t in steady_trips)
secs = sum(float(t[4]) for t in steady_trips)
recent = [int(t[5]) for t in steady_trips if int(t[5]) >= 0]
print(f'\nsteady state: trips {warm + 1}-{n} ({steady} trips), {len(failed)} failed of {n}'
      + (f' (trips {", ".join(map(str, failed))})' if failed else ''))
print(f'  per trip: {erased / 1024 / steady:.2f} MB erased, about {secs / steady:.1f} s of flash work, '
      f'{commits / steady:.2f} directory commits'
      + (f', {sum(recent) / steady:.1f} recently used entries evicted' if recent else ''))
print(f'  erases per 4 KB sector per trip: hottest {hot / steady:.3f}, mean {sum(data) / len(data) / steady:.3f}'
      + (f'; the hottest reaches 100,000 cycles after {100000 * steady / hot:,.0f} trips' if hot else ''))
EOF
