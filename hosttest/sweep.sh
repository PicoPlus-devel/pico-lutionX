#!/bin/sh
# Heap budget per level: for each level, start the demo fixture, warp to the level with the
# debug command +goto and record the heap report after the level has loaded.
#
#   hosttest/sweep.sh <data-dir> [levels...]     (default levels: 0 1 2 3 4, all of spawn)
#
# <data-dir> holds spawn.mpq or DIABDAT.MPQ and an `assets` folder (DevilutionX assets).
# Each level is run twice:
#   all      PSRAM pool of 64 MiB, no flash cache: the total the engine needs
#   flash    PSRAM pool of the board (8 MiB - static data), all sprite data in a simulated
#            flash pool: what would stay in PSRAM if every sprite set were cached
#   fcache   PSRAM pool of the board with the real flash asset cache on emulated flash,
#            starting empty: what the board does on the first visit of the level
# Output: CSV on stdout (bytes; "demo" is the demo playback buffer, not part of normal play).
# After the warp the demo runs out of input and the engine stops in an error dialog; the
# frame limit (DX_MAX_FRAMES) ends the run once the level report has been written.
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
		python3 -I - "$WORK/heap.log" "$level" "$mode" <<'EOF'
import re
import sys

log, level, mode = sys.argv[1], int(sys.argv[2]), sys.argv[3]
lines = open(log).read().splitlines()
result = 'ok'
mark = cats = None
for i, line in enumerate(lines):
    if 'OUT OF MEMORY' in line:
        result = 'out-of-memory'
    if line.startswith('MARK level %d ' % level) or line.startswith('MARK out of memory'):
        mark, cats = line, lines[i + 1]
if mark is None:
    print(f'{level},{mode},no-level-mark' + ',' * 17)
    sys.exit(0)
num = lambda key, text: int(re.search(key + r' +(\d+)', text).group(1))
psram = mark.split('|')[0]
resident = mark.split('|')[1]
flash = num('flash-sim: live', mark) if 'flash-sim' in mark else 0
if mode == 'fcache':
    import re as _re
    m = _re.search(r'dx_fcache: \d+ entries, (\d+) KB used', open(log.replace('heap.log', 'run.log')).read())
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
