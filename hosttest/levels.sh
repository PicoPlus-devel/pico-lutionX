#!/bin/sh
# Loads dungeon levels in the test program with the board's PSRAM and flash asset cache, and
# reports per level what is left. Works with either game data, unlike sweep.sh (whose demo
# fixture is a shareware hero): a new hero is created through the menus and the debug
# command +goto takes it to the level.
#
#   hosttest/levels.sh <data-dir> [cold|warm] [levels...]   (default: cold, levels 1-16)
#
#   cold   every level on an empty flash cache (a first visit with nothing cached)
#   warm   the levels in order on one flash cache kept from run to run, as in a game
#
# <data-dir> holds DIABDAT.MPQ (or spawn.mpq, levels 1-4 with --spawn added by hand) and
# an `assets` folder. Needs NOSAN=1 hosttest/build.sh. Output per level: the PSRAM heap
# after the level loaded (live, largest free block), the flash cache (used, written, stores
# that found no room) and the result; a failed level's log ends in the cache's usage report
# and map (dx_fcache_print_usage, dx_fcache_print_map).
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="$HERE/dx_host_nosan"
[ -x "$HOST" ] || { echo "build first: NOSAN=1 hosttest/build.sh" >&2; exit 1; }
DATA="$(cd "$1" && pwd)"
shift
MODE=cold
case "$1" in cold | warm) MODE=$1; shift ;; esac
LEVELS="${*:-1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16}"
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

for level in $LEVELS; do
	rm -rf "$WORK/save"
	mkdir -p "$WORK/save"
	[ "$MODE" = cold ] && rm -f "$WORK/flash.bin"
	set +e
	(cd "$WORK" && DX_FCACHE=1 DX_FCACHE_FILE="$WORK/flash.bin" DX_SCRIPT="$WORK/newhero.script" \
		"$HOST" --data-dir "$DATA" --save-dir "$WORK/save" --config-dir "$WORK/save" -n \
		+goto "$level" > "$WORK/run.log" 2>&1)
	rc=$?
	set -e
	python3 -I - "$WORK/run.log" "$level" "$rc" <<'EOF'
import re
import sys

log, level, rc = sys.argv[1], int(sys.argv[2]), int(sys.argv[3])
text = open(log, errors='replace').read()
mark = re.search(r'^MARK level %d .*' % level, text, re.M)
cache = re.search(r'dx_fcache: \d+ entries, (\d+) KB used of (\d+) KB.* (\d+) KB programmed', text)
noroom = len(re.findall(r'no room for', text))
if mark and rc == 0:
    live = int(re.search(r'psram: live +(\d+)', mark.group(0)).group(1))
    free = int(re.search(r'largest-free +(\d+)', mark.group(0)).group(1))
    psram = f'PSRAM live {live // 1024:5d} KB, largest free {free // 1024:5d} KB'
    result = 'ok'
else:
    psram = 'PSRAM ' + ('out of memory' if 'PSRAM heap full' in text else 'level not loaded')
    result = f'FAILED (exit {rc})'
flash = f'cache {cache.group(1)}/{cache.group(2)} KB, {cache.group(3)} KB written' if cache else 'cache -'
print(f'level {level:2d}: {psram} | {flash}, {noroom} without room | {result}')
EOF
	if [ "$rc" -ne 0 ] || ! grep -q "^MARK level $level " "$WORK/run.log"; then
		cp "$WORK/run.log" "levels-failed-$level.log"
		echo "  log: levels-failed-$level.log"
	fi
done
