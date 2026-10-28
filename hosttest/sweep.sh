#!/bin/sh
# Heap budget per level: for each level, start the demo fixture, warp to the level with the
# debug command +goto and record the heap report after the level has loaded.
#
#   hosttest/sweep.sh <data-dir> [levels...]     (default levels: 0 1 2 3 4, all of spawn)
#
# <data-dir> holds spawn.mpq (the demo fixture is a shareware hero; levels.sh works with
# either game data) and an `assets` folder (DevilutionX assets). Each level is run three
# times:
#   all      PSRAM pool of 64 MiB, no flash cache: the total the engine needs
#   flash    PSRAM pool of the board (8 MiB - static data), all sprite data in a simulated
#            flash pool: what would stay in PSRAM if every sprite set were cached
#   fcache   PSRAM pool of the board with the real flash asset cache on emulated flash,
#            starting empty: what the board does on the first visit of the level
# Output: CSV on stdout (bytes; "demo" is the demo playback buffer, not part of normal play).
# After the warp the demo runs out of input and the engine stops in an error dialog; the
# frame limit (DX_MAX_FRAMES) ends the run once the level report has been written. Without
# a warp (the demo's own level 1) the demo plays on.
# result: ok; out-of-memory (a pool ran out before the level had loaded, and the row holds
# the heap at that moment); out-of-memory-in-level (the level loaded, the row holds its
# report, and the run ran out of memory later in the level); no-level-mark. A run that runs
# out of memory aborts, which the shell reports.
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
HOST="$HERE/dx_host_nosan"
[ -x "$HOST" ] || { echo "build first: NOSAN=1 hosttest/build.sh" >&2; exit 1; }
DATA="$(cd "$1" && pwd)"
shift
LEVELS="${*:-0 1 2 3 4}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

echo "level,mode,result,psram_live,psram_peak_since_previous,resident_live,resident_peak_since_previous,flash_live,demo,dungeon,monster,player,missile,ui,object,towner,other,screen,mpq"
for level in $LEVELS; do
	for mode in all flash fcache; do
		rm -rf "$WORK/save"
		mkdir -p "$WORK/save"
		cp "$HERE"/fixtures/WarriorLevel1to2/* "$WORK/save/"
		case $mode in
		all) ENV="DX_PSRAM_BYTES=67108864" ;;
		flash) ENV="DX_FLASH_SIM=1" ;;
		fcache) ENV="DX_FCACHE=1" ;;
		esac
		set +e
		(cd "$WORK" && env $ENV DX_MAX_FRAMES=8000 DX_HEAP_LOG="$WORK/heap.log" "$HOST" --data-dir "$DATA" \
			--save-dir "$WORK/save" --config-dir "$WORK/save" --demo 0 --timedemo -n --spawn \
			+goto "$level" > "$WORK/run.log" 2>&1)
		set -e
		python3 -I - "$WORK/heap.log" "$WORK/run.log" "$level" "$mode" <<'EOF'
import re
import sys

log, run_log, level, mode = sys.argv[1], sys.argv[2], int(sys.argv[3]), sys.argv[4]
lines = open(log).read().splitlines()
level_mark = oom_mark = None
for i, line in enumerate(lines):
    if line.startswith('MARK level %d ' % level):
        level_mark = line, lines[i + 1]
    elif line.startswith('MARK out of memory'):  # src/port/dx_heap.cpp, then the run aborts
        oom_mark = line, lines[i + 1]
if oom_mark is None:
    result, found = 'ok', level_mark
elif level_mark is None:
    result, found = 'out-of-memory', oom_mark
else:
    result, found = 'out-of-memory-in-level', level_mark
if found is None:
    print(f'{level},{mode},no-level-mark' + ',' * 16)
    sys.exit(0)
mark, cats = found
num = lambda key, text: int(re.search(key + r' +(\d+)', text).group(1))
psram = mark.split('|')[0]
resident = mark.split('|')[1]
flash = num('flash-sim: live', mark) if 'flash-sim' in mark else 0
if mode == 'fcache':
    m = re.search(r'dx_fcache: \d+ entries, (\d+) KB used', open(run_log, errors='replace').read())
    flash = int(m.group(1)) * 1024 if m else 0
live = {}
for name, total, _cacheable, fl in re.findall(r'(\w+) (\d+)/(\d+)(?:\+flash (\d+))?', cats):
    live[name] = int(total) + int(fl or 0)
order = ['demo', 'dungeon', 'monster', 'player', 'missile', 'ui', 'object', 'towner', 'other', 'screen', 'mpq']
print(','.join(str(x) for x in [level, mode, result, num('live', psram), num('peak-since-mark', psram),
                                 num('live', resident), num('peak-since-mark', resident), flash]
               + [live.get(k, 0) for k in order]))
EOF
	done
done
