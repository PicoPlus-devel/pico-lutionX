#!/bin/sh
# Makes the test saves in testsaves/ with the test program (hosttest): one saved game per
# dungeon level, each with its own hero, so that every level can be loaded on a board from
# the main menu.
#
#   tools/make_testsaves.sh <full-game-data-dir> [<shareware-data-dir>]
#
# A data directory holds DIABDAT.MPQ or spawn.mpq and an `assets` folder (see TECHNICAL.md,
# "The test program"). Needs NOSAN=1 hosttest/build.sh.
#
# Each hero is a new warrior named LevelNN, taken to level NN with the debug commands
# `+goto NN +givexp 29 +maxstats` (character level 30, all attributes at their maximum)
# and saved there through the game menu. The files use save slots 80-95 so that they do
# not replace a player's own heroes, which take the lowest free slots:
#
#   testsaves/full/single_80.sv ... single_95.sv   levels 1-16 (DIABDAT.MPQ)
#   testsaves/shareware/spawn_80.sv, spawn_81.sv   levels 1-2 (spawn.mpq; the shareware
#                                                  stairs end at level 2)
#
# Every save is loaded again afterwards; the script fails if one does not reach its level.
set -e
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
HOST="$ROOT/hosttest/dx_host_nosan"
[ -x "$HOST" ] || { echo "build first: NOSAN=1 hosttest/build.sh" >&2; exit 1; }
[ -n "$1" ] || { echo "usage: $0 <full-game-data-dir> [<shareware-data-dir>]" >&2; exit 1; }
FULL="$(cd "$1" && pwd)"
SPAWN="${2:+$(cd "$2" && pwd)}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# main menu -> Single Player -> New Hero -> Warrior -> name -> new game; after the debug
# commands: game menu (Escape) -> Save Game (Enter)
make_script() {
	cat > "$WORK/make.script" <<EOF
200 key RETURN
400 key RETURN
600 key RETURN
800 text $1
900 key RETURN
1100 key RETURN
1300 key RETURN
2500 key ESCAPE
2600 key RETURN
3200 exit
EOF
}

# main menu -> Single Player -> the only hero -> Load Game
cat > "$WORK/load.script" <<'EOF'
200 key RETURN
400 key RETURN
600 key RETURN
2500 exit
EOF

# run <data-dir> <log> <script> [args...]: the test program on a new flash cache
run() {
	data=$1 log=$2 script=$3
	shift 3
	rm -f "$WORK/flash.bin" "$WORK/flash.bin.wear"
	(cd "$WORK" && DX_FCACHE=1 DX_FCACHE_FILE="$WORK/flash.bin" DX_SCRIPT="$WORK/$script" \
		"$HOST" --data-dir "$data" --save-dir "$WORK/save" --config-dir "$WORK/save" -n "$@" \
		> "$WORK/$log" 2>&1) || { echo "test program failed, log:" >&2; tail -20 "$WORK/$log" >&2; exit 1; }
}

# make <data-dir> <out-dir> <prefix> <level> [--spawn]
make() {
	data=$1 out=$2 prefix=$3 level=$4 spawn=$5
	name=$(printf 'Level%02d' "$level")
	file="${prefix}_$((79 + level)).sv"
	make_script "$name"
	rm -rf "$WORK/save" && mkdir "$WORK/save"
	run "$data" make.log make.script $spawn +goto "$level" +givexp 29 +maxstats
	grep -q "^MARK level $level " "$WORK/make.log" || { echo "$name: level $level not reached" >&2; exit 1; }
	[ -f "$WORK/save/${prefix}_0.sv" ] || { echo "$name: no save written" >&2; exit 1; }
	mv "$WORK/save/${prefix}_0.sv" "$WORK/$file"

	# load it again from its own slot
	rm -rf "$WORK/save" && mkdir "$WORK/save"
	cp "$WORK/$file" "$WORK/save/"
	run "$data" load.log load.script $spawn
	grep -q "^MARK level $level " "$WORK/load.log" || { echo "$name: the save does not load level $level" >&2; exit 1; }
	mkdir -p "$out"
	mv "$WORK/$file" "$out/$file"
	echo "$out/$file: $name, level $level"
}

for level in 1 2 3 4 5 6 7 8 9 10 11 12 13 14 15 16; do
	make "$FULL" "$ROOT/testsaves/full" single "$level"
done
if [ -n "$SPAWN" ]; then
	for level in 1 2; do
		make "$SPAWN" "$ROOT/testsaves/shareware" spawn "$level" --spawn
	done
fi
