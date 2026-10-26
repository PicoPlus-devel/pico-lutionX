# Test saves

Saved games for testing each dungeon level on a board. Each file holds one hero, named
after its level, saved on that level:

| Folder | Files | Heroes | Game data |
|---|---|---|---|
| `full/` | `single_80.sv` … `single_95.sv` | `Level01` … `Level16`, levels 1–16 | `DIABDAT.MPQ` |
| `shareware/` | `spawn_80.sv`, `spawn_81.sv` | `Level01`, `Level02`, levels 1–2 | `spawn.mpq` |

The shareware version has no stairs below level 2.

## Use

1. Copy the files of the folder that matches the game data to `roms/devilutionx` on the SD
   card, next to the game data. The numbers 80–95 are save slots; a player's own heroes
   use the lowest free slots, so the test saves do not replace them.
2. Start the game and choose Single Player. The heroes appear in the hero list with their
   names, class and character level.
3. Select a hero, for example `Level16`, and choose Load Game. The game starts on that
   level, at the stairs or entrance where the hero arrived.

To change to another level, choose Quit Game in the game menu (on the board this restarts
it) and select another hero. Load Game in the game menu only reloads the current hero.

## The heroes

Each hero is a new warrior, taken to its level with the debug commands
`+goto <level> +givexp 29 +maxstats`: character level 30 and all attributes at their
maximum, with the starting equipment. This lets the hero survive long enough to look
around and fight on the deep levels; it is not the progress of a normal game. The quests
are those of a new game.

Saving over a test hero changes its file. To restore it, copy the file again.

## Making them again

The saves are made with the test program on a PC:

```sh
NOSAN=1 hosttest/build.sh
tools/make_testsaves.sh <full-game-data-dir> [<shareware-data-dir>]
```

A data directory holds `DIABDAT.MPQ` or `spawn.mpq` and an `assets` folder (see
[TECHNICAL.md](../TECHNICAL.md), "The test program"). The script loads every save again and
stops if one does not reach its level.
