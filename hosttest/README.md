# Test programs for a PC

The programs in this directory run the firmware's DevilutionX engine on Linux. They compile
the same engine sources, the same SDL 1.2 subset (`src/sdl/`) and, in the SD card mode, the
same file layer (`src/pico/dx_fatfs_io.c` on FatFs) as the RP2350 build. Only the
platform below the SDL subset is replaced. Engine and memory problems can therefore be
investigated without flashing a board.

The programs model the board's constraints:

- They are built with `-m32`, so pointer-heavy structures have the size they have on the
  32-bit Cortex-M33.
- The engine's heap is a TLSF pool of the size the board's 8 MB of PSRAM leaves after the
  engine's static data. Running out of memory here means running out of memory on the
  board.
- The flash asset cache runs on an emulated NOR flash region with the board's erase and
  program rules.
- The screen surface may not be drawn into while the board would still be copying it;
  a write that breaks this rule faults.
- By default they are built with AddressSanitizer and UndefinedBehaviorSanitizer.

## Programs

| Program | Purpose |
|---|---|
| `dx_host` | Headless. Waits cost no time, so a run is as fast as the host allows. Writes frames as PPM files and sound as a WAV file, takes scripted input. Used for memory budgets, the flash cache, the SD card mode and regression checks. |
| `dx_host_sdl` | Interactive. The same program in a window, with sound, keyboard, mouse and gamepads, on real time. Every feature of `dx_host` is also available. |
| `dx_host_nosan`, `dx_host_sdl_nosan` | The same without sanitizers: several times faster, used by the scripts. |
| `dx_sdimg` | Builds SD card images for the SD card mode. |

## Requirements

- CMake 3.16 or later, Ninja, GCC and G++ with 32-bit support, and Python 3.
- For `dx_host_sdl`: the 32-bit SDL2 development package. On Debian and Ubuntu:

  ```sh
  sudo dpkg --add-architecture i386
  sudo apt update
  sudo apt install cmake ninja-build gcc-multilib g++-multilib libsdl2-dev:i386
  ```

- The game data: `spawn.mpq` (shareware) or `DIABDAT.MPQ` (full game), see below.

## Building

```sh
hosttest/build.sh            # dx_host, dx_host_sdl, dx_sdimg (with ASan and UBSan)
NOSAN=1 hosttest/build.sh    # dx_host_nosan, dx_host_sdl_nosan, dx_sdimg
```

The build directories are `hosttest/build/` and `hosttest/build-nosan/`; later runs of
the script build incrementally. When the 32-bit SDL2 package is not installed, the script
builds the other programs and says that `dx_host_sdl` was not built.

## The game data directory

The programs take a directory with the game data and the DevilutionX assets, laid out
as the `roms/devilutionx` folder on the board's SD card. The examples below use one
directory for each kind of game data; run these commands from the repository's root
directory:

```sh
mkdir -p ~/dx/diablo ~/dx/spawn
ln -s /path/to/DIABDAT.MPQ ~/dx/diablo/
ln -s "$PWD/3rdparty/devilutionX/assets" ~/dx/diablo/assets
ln -s /path/to/spawn.mpq ~/dx/spawn/
ln -s "$PWD/3rdparty/devilutionX/assets" ~/dx/spawn/assets
```

Only one of the two is needed to play. DevilutionX's reference demo, and with it the
timedemo test and `sweep.sh`, needs the shareware data.

Unlike the SD card, a Linux file system distinguishes case: the engine opens exactly
`DIABDAT.MPQ`, `diabdat.mpq` or `spawn.mpq`, so a link may carry the name the engine
expects. When the directory given with `--data-dir` holds none of them, the programs
stop at the start:

```
dx_host: no DIABDAT.MPQ, diabdat.mpq or spawn.mpq in /home/user/dx/diablo
```

Without `--data-dir` the engine searches the board's folder `/roms/devilutionx/`,
`~/.local/share/diasurgical/devilution/` and the current directory, and reports a data
file error when none of them holds an MPQ.

## Examples

The examples are run from the repository's root directory, after `hosttest/build.sh`,
and use the data directories above. They write only below `~/dx`. The output shown is
that of the current version; the figures change with the code.

Arguments other than the programs' own are DevilutionX's (`hosttest/dx_host --help`):
`--data-dir` is the directory of the game data, `--save-dir` and `--config-dir` receive
the saved games and `diablo.ini`, and `-n` skips the intro videos. The programs are
built with `_DEBUG`, so DevilutionX's debug commands can be given as `+<command>`, for
example `+goto <level>`. At exit the programs report the heap (`MARK exit`), the flash
cache and the sound.

### Playing the game in a window

```sh
mkdir -p ~/dx/play
hosttest/dx_host_sdl --data-dir ~/dx/diablo --save-dir ~/dx/play --config-dir ~/dx/play -n
```

With `--data-dir ~/dx/spawn` the shareware game is played instead, and `-f` shows the
frame rate. The first lines of output name the SDL video driver, the renderer, the window
size and the sound driver:

```
dx_heap: PSRAM pool 7632611 bytes (engine static data 755997 bytes), flash simulation off
dx_host_sdl: x11 video, software renderer, window 1280x960
dx_host_sdl: pulseaudio sound
INFO: Adding joystick 0: Gamepad
```

The joystick is the one gamepad the firmware declares to the engine; it is listed also
when no gamepad is attached. Closing the window ends the program. The controls are
described in [The window](#the-window).

### Keeping the flash cache between sessions

On the board the flash asset cache survives a restart. `DX_FCACHE_FILE` keeps the
emulated flash in a file, so a session starts with the cache of the previous one:

```sh
DX_FCACHE_FILE=~/dx/play/flash.bin \
    hosttest/dx_host_sdl --data-dir ~/dx/diablo --save-dir ~/dx/play --config-dir ~/dx/play -n
```

At exit the program reports the cache. A second session that goes into the town finds
almost everything cached:

```
dx_fcache: 287 entries, 8405 KB used of 13056 KB, 446 hits, 5 misses, 5 stored, 0 failed, 0 evicted (0 recent); 148 KB erased, 47 KB programmed (about 0.6 s on the board)
```

As on the board, the cache's directory is written after a level load, so a session that
ends in the menus adds nothing to the file. A file made with the other game data is
emptied at the start ("game data changed"); use one file for each data directory.

### Playing from a card image, as on the board

```sh
hosttest/dx_sdimg ~/dx/card.img ~/dx/diablo=/roms/devilutionx
DX_SDCARD=~/dx/card.img hosttest/dx_host_sdl
```

`dx_sdimg` formats a sparse 8 GB image as FAT32 and copies `~/dx/diablo`, following the
links, into `/roms/devilutionx` on it:

```
dx_sdimg: /home/user/dx/card.img, FAT32, 8192 MB, 32 KB clusters: 129 files copied, 0 skipped
```

The program then starts as the board does: the board's file layer mounts the card and
finds the game data, and the engine receives the board's arguments. Saved games and
`diablo.ini` are written into the image.

```
sdcard: card image /home/user/dx/card.img
dx_fatfs: hardware SPI0
dx_fatfs: /roms/devilutionx/DIABDAT.MPQ, 505372 KB
```

`dx_sdimg -t exfat` builds an exFAT card, and `-f 8` splits every file of 1 MB or more into
8 fragments, as on a card that has been written to before.

### Watching DevilutionX's reference demo

```sh
mkdir -p ~/dx/demo
cp hosttest/fixtures/WarriorLevel1to2/* ~/dx/demo/
hosttest/dx_host_sdl --data-dir ~/dx/spawn --save-dir ~/dx/demo --config-dir ~/dx/demo \
    --demo 0 -n --spawn
```

The demo is a warrior's recorded game on the first two dungeon levels of the shareware
data. It plays in real time and takes about six minutes. At its end the engine reports
whether the outcome matches the recording (`Timedemo: Same outcome as initial run.`), and
the program ends.

### Checking the engine after a change

```sh
mkdir -p ~/dx/fixture
cp hosttest/fixtures/WarriorLevel1to2/* ~/dx/fixture/
hosttest/dx_host --timedemo-test ~/dx/fixture ~/dx/spawn
```

This is DevilutionX's own regression test: it plays the reference demo headless, as fast
as possible, and compares the hero at the end with the stored one. It takes about a
second, prints `TIMEDEMO: Same` and returns 0 when the game logic behaves as before. The
test writes its result into the fixture directory, hence the copy.

### Recording frames and sound

```sh
mkdir -p ~/dx/demo ~/dx/out
cp hosttest/fixtures/WarriorLevel1to2/* ~/dx/demo/
DX_FCACHE=1 DX_DUMP_EVERY=300 DX_DUMP_DIR=~/dx/out DX_AUDIO_WAV=~/dx/out/sound.wav \
    hosttest/dx_host --data-dir ~/dx/spawn --save-dir ~/dx/demo --config-dir ~/dx/demo \
    --demo 0 --timedemo -n --spawn
python3 hosttest/ppm2png.py ~/dx/out/frame_00600.ppm
```

The reference demo again, headless and with the flash cache. Every 300th frame is
written as `~/dx/out/frame_NNNNN.ppm` and the mixed sound as `~/dx/out/sound.wav`;
`ppm2png.py` writes `frame_00600.png` next to the frame. The run takes about 16 seconds
with the sanitizers. The end of the output:

```
INFO: 6414 frames, 16.06 seconds: 399.4 fps
dx_fcache: 410 entries, 12720 KB used of 13056 KB, 418 hits, 447 misses, 417 stored, 0 failed, 7 evicted (7 recent); 1808 KB erased, 14620 KB programmed (about 30.1 s on the board)
dx_host: audio 2377927 frames (107.8 s), peak 32768
```

### A scripted run: a new hero on a dungeon level

```sh
cat > ~/dx/newhero.script <<'EOF'
# main menu -> Single Player -> New Hero -> Warrior -> name -> new game
200 key RETURN
400 key RETURN
600 key RETURN
800 text Tester
900 key RETURN
1100 key RETURN
1300 key RETURN
3000 exit
EOF
rm -rf ~/dx/scripted
mkdir -p ~/dx/scripted
DX_FCACHE=1 DX_SCRIPT=~/dx/newhero.script \
    hosttest/dx_host --data-dir ~/dx/diablo --save-dir ~/dx/scripted --config-dir ~/dx/scripted \
    -n +goto 5
```

The script creates a warrior named Tester and starts a new game; `+goto 5` then takes
the hero to dungeon level 5. The script expects an empty save directory: with a saved
hero, the menu offers the list of heroes instead of the new hero screen. The heap
reports show the memory after each level load:

```
MARK level 0 (type 0)           psram: live  1059268 peak-since-mark  1802096 peak  1802096 largest-free  6411056 | ...
MARK level 5 (type 2)           psram: live  4340576 peak-since-mark  5221804 peak  5221804 largest-free  3223632 | ...
```

The same command with `hosttest/dx_host_sdl` shows the run in the window; there the 3000
frames take 50 seconds.

### PSRAM and flash cache per level

```sh
NOSAN=1 hosttest/build.sh
hosttest/levels.sh ~/dx/diablo cold 1 9 16
```

`levels.sh` creates a new hero with the same menu script as above and takes it to each
level, starting each time with an empty flash cache. It prints one line per level:

```
level  1: PSRAM live  4578 KB, largest free  2444 KB | cache 11529/13056 KB, 256 KB erased, 11639 KB written (19.2 s), 0 without room | ok
level  9: PSRAM live  4149 KB, largest free  2372 KB | cache 11542/13056 KB, 256 KB erased, 11644 KB written (19.2 s), 0 without room | ok
level 16: PSRAM live  4237 KB, largest free  2783 KB | cache 12585/13056 KB, 3584 KB erased, 16426 KB written (37.5 s), 0 without room | ok
```

With `warm`, one cache is kept over all trips, as in a game, and the run ends with a
summary of the flash wear:

```sh
hosttest/levels.sh ~/dx/diablo warm 1 5 9 13 16 1 5 9 13 16
```

```
steady state: trips 2-10 (9 trips), 0 failed of 10
  per trip: 4.30 MB erased, about 24.4 s of flash work, 1.67 directory commits, 35.3 recently used entries evicted
  erases per 4 KB sector per trip: hottest 1.111, mean 0.321; the hottest reaches 100,000 cycles after 90,000 trips
```

The log of a level that fails is kept as `levels-failed-<level>.log` in the current
directory.

### The heap per level in three configurations

```sh
hosttest/sweep.sh ~/dx/spawn > ~/dx/sweep.csv
```

For each level of the shareware data (0 to 4), `sweep.sh` starts the reference demo,
moves the hero to the level and records the heap per category: in a 64 MiB pool (`all`,
what the engine needs in total), in the board's pool with every cacheable allocation in a
separate pool (`flash`, what would stay in PSRAM if every asset were cached), and in the
board's pool with the real flash cache (`fcache`). The result is a CSV table. A run
that runs out of PSRAM ends with "Aborted" from the shell (see [Memory](#memory)); with
the shareware data this happens in some `flash` runs.

### A card that fails to answer

```sh
DX_SDCARD_TRACE=1 DX_SDCARD=~/dx/card.img DX_MAX_FRAMES=300 hosttest/dx_host 2> ~/dx/trace.txt
grep fopen ~/dx/trace.txt
DX_SDCARD_FAIL=100 DX_SDCARD=~/dx/card.img DX_MAX_FRAMES=300 hosttest/dx_host
```

The first run lists every card read and write on standard error, numbered, with the
files the engine opens between them:

```
sdcard: read #1 sector 0, 1 sectors
...
sdcard: fopen(/roms/devilutionx/DIABDAT.MPQ, rb) = fd 3 (after read #153)
```

The second run makes read number 100 fail, as a card that does not answer. That read
belongs to the cluster map the file layer builds when it opens `DIABDAT.MPQ`. The file
layer opens the file again and builds the map once more, so the game starts normally:

```
sdcard: read #100 (sector 6213, 1 sectors) fails (DX_SDCARD_FAIL)
dx_fatfs: cluster map of /roms/devilutionx/DIABDAT.MPQ failed (FatFs error 1)
```

The numbers of the reads depend on the image; the trace shows which read belongs to
which step. `DX_SDCARD_FAIL` takes several numbers, separated by commas.

### Without a display

```sh
SDL_VIDEODRIVER=dummy SDL_AUDIODRIVER=dummy DX_MAX_FRAMES=600 \
    hosttest/dx_host_sdl --data-dir ~/dx/diablo --save-dir ~/dx/play --config-dir ~/dx/play -n
```

SDL's dummy drivers run `dx_host_sdl` without a display or a sound device, for example
to check a change to `dx_sdl.cpp` in a script:

```
dx_host_sdl: dummy video, software renderer, window 640x480
dx_host_sdl: dummy sound
```

## The window

- **Window:** the 640x480 picture, at twice the size when the desktop has room for it
  (`DX_SDL_SCALE=n` chooses the factor). The window can be resized and keeps the 4:3
  aspect ratio. Closing it ends the program, as closing DevilutionX's window does on a
  PC.
- **Keyboard:** keys are mapped by position with the firmware's table, as for a USB
  keyboard on the board, with the characters of the US layout. On another layout, letters
  and symbols are those printed on a US keyboard at the same place. Keys do not repeat
  when held, as on the board.
- **Mouse:** the engine's cursor follows the pointer; the system pointer is hidden over the
  window. The buttons and the wheel work as on the board.
- **Gamepads:** every game controller SDL recognises is merged into the one gamepad the
  engine sees, as the board merges its USB pads and controller ports. Buttons are named by
  position, so the actions are those in the gamepad table of the main
  [README](../README.md) (column Xbox): the bottom face button is Xbox A, Back is Select,
  Guide is Select and Start together, and the triggers count as pressed past the board's
  threshold.
- **Focus:** keys and mouse buttons that are held when the window loses the focus are
  released.
- **Sound:** 22050 Hz stereo, converted by SDL to the device's format.

Without `--save-dir` and `--config-dir`, DevilutionX writes saved games and `diablo.ini`
to `~/.local/share/diasurgical/devilution/`. A directory of its own keeps interactive
sessions apart from the scripts' runs.

On WSL2, WSLg provides the X11 display and the sound. USB gamepads are not visible in
WSL2 unless they are attached to it (usbipd-win).

## Scripted input

`DX_SCRIPT=<file>` reads one command per line; each runs when the given number of frames
has been presented. Lines starting with `#` are comments. The example
[above](#a-scripted-run-a-new-hero-on-a-dungeon-level) creates a new hero.

| Command | Effect |
|---|---|
| `<frame> key <name> [down\|up]` | A key press and release, or only one of them. Names: `RETURN`, `ESCAPE`, `SPACE`, `TAB`, `BACKSPACE`, `UP`, `DOWN`, `LEFT`, `RIGHT`, `F1` to `F12`, `LSHIFT`, `LCTRL`, `LALT`, or a single character. |
| `<frame> text <characters>` | Types the characters. |
| `<frame> move <x> <y>` | Moves the mouse to a screen position. |
| `<frame> click <x> <y> [right]` | Moves the mouse there and clicks the left (or right) button. |
| `<frame> mark <label>` | Writes a heap report. |
| `<frame> exit` | Writes a heap report and ends the run. |

## Environment variables

| Variable | Effect |
|---|---|
| `DX_DUMP_EVERY=n` | Writes every nth presented frame as `frame_NNNNN.ppm`. |
| `DX_DUMP_DIR=dir` | Directory for the frames (default: the current directory). |
| `DX_MAX_FRAMES=n` | Ends the run after n presented frames, with a heap report. |
| `DX_SCRIPT=file` | Scripted input, see above. |
| `DX_AUDIO_WAV=file` | Writes the mixed sound to a WAV file. |
| `DX_FCACHE=1` | Runs the flash asset cache on emulated flash. On by default in `dx_host_sdl`, where `DX_FCACHE=0` turns it off. |
| `DX_FCACHE_KB=n` | Size of the emulated flash region in KB (default 13312). |
| `DX_FCACHE_FILE=file` | Keeps the flash region in a file between runs, and its wear counts in `<file>.wear`. |
| `DX_SDCARD=image` | The SD card mode, on a card image from `dx_sdimg`. |
| `DX_SDCARD_FAIL=n[,n...]` | In the SD card mode, card reads number n fail (counted from 1 at the mount). |
| `DX_SDCARD_TRACE=1` | In the SD card mode, lists every card read and write, with its number. |
| `DX_PSRAM_BYTES=n` | Size of the PSRAM pool (default: 8 MiB minus the engine's static data). |
| `DX_FLASH_SIM=1` | Takes cacheable allocations from a separate 16 MiB simulated pool. |
| `DX_HEAP_LOG=file` | File for the heap reports (default: standard error). |
| `DX_SDL_SCALE=n` | `dx_host_sdl` only: the initial window size, n times 640x480. |

## Scripts

| Script | Purpose |
|---|---|
| `sweep.sh <data-dir> [levels...]` | Heap per level with the shareware demo fixture: in a large pool, with every sprite cached, and with the real flash cache. CSV on standard output. |
| `levels.sh <data-dir> [cold\|warm] [levels...]` | Takes a new hero to each level with either game data and reports PSRAM, flash cache and the result per level; `warm` keeps one cache from run to run and ends with a summary of the flash wear. |
| `ppm2png.py [--hdmi] <ppm...>` | Converts frames to PNG. `--hdmi` also truncates each colour channel to 5 bits, as the board's RGB555 output does, and doubles the picture to 1280x960. |
| `dx_sdimg [-t fat32\|exfat] [-s MB] [-c cluster-KB] [-f fragments] <image> <dir>=<card-dir>...` | Builds a card image: formats it with FatFs and copies each directory into the card directory. `-f` splits every file of 1 MB or more into fragments, as on a card that has been written to before. |

The scripts use `dx_host_nosan` (`NOSAN=1 hosttest/build.sh`); `levels.sh` runs another
build when it is given in `DX_HOST`. `tools/make_testsaves.sh` also uses `dx_host_nosan`,
to make the saved games in `testsaves/`.

## How it works

### The platform below the SDL subset

The engine is built with DevilutionX's SDL 1.2 configuration and talks to the port's SDL
subset in `src/sdl/`. The subset reaches the platform through the `sdlb_*` functions of
`src/sdl/sdl_backend.h` (clock, waiting, the screen surface, presenting a frame, the
palette, polling input) and receives input through its `sdlp_*` functions. There are three
implementations:

```
engine -> SDL 1.2 subset (src/sdl) -> sdlb_* platform functions
   board:        src/pico/dx_backend.cpp
   dx_host:      hosttest/dx_host.cpp
   dx_host_sdl:  hosttest/dx_host.cpp built with DX_HOST_SDL
                 + hosttest/dx_sdl.cpp          window, sound, raw input (SDL2)
                 + src/pico/dx_input.cpp        the firmware's input translation
```

The sound mixer (`src/aulib/`, the port's SDL_audiolib subset) is pulled by the platform
with `aulib_mix()` on the engine's thread, as on the board.

### Clock

- `dx_host`: the clock is real time plus a virtual offset. `SDL_Delay` advances the offset
  instead of sleeping, so frame limiting and waits cost no time. The real-time part means
  that two runs are not identical frame for frame; DevilutionX's demo playback
  (`--timedemo-test`) is deterministic.
- `dx_host_sdl`: the clock is real time. `SDL_Delay` sleeps, and each present waits for
  the next vertical blank of a 60 Hz output, as the board's present does. The PC is
  faster than the board, so the frame rate is higher than on the board (up to 60 frames
  per second instead of about 30); the program does not model the board's speed.

### Screen

The engine renders 640x480 at 8 bits per pixel into the screen surface. On the board the
frame is copied to the display after `SDL_Flip` returns, and the engine must call
`sdlb_screen_wait()` before it draws again. Both programs check this: from each present
until the next wait the surface's memory pages are read-only, so a write that skipped the
wait faults with its stack trace.

`dx_host` writes every Nth frame as a PPM file (`DX_DUMP_EVERY`, `DX_DUMP_DIR`).
`dx_host_sdl` also shows each frame in its window, with each colour channel truncated
to 5 bits as in the board's RGB555 output.

### Sound

The mixer produces 16-bit stereo at 22050 Hz.

- `dx_host` mixes as much as the clock has advanced since the last call.
- `dx_host_sdl` models the board's sound ring (`src/pico/dx_audio.c`): the ring is played
  at 22050 Hz in real time, but never past the last mixed frame, and is kept 2048 frames
  (93 ms) ahead of the play position. When the engine does not run the platform for a
  while (a level load), the sound is delayed and the device plays silence, as on the
  board. What is mixed is also queued on the sound device, which is kept at least 2048
  frames ahead as well. The device does not set the pace: when it plays more slowly than
  it is fed, its queue is dropped once it holds more than 8192 frames (371 ms). The
  engine therefore runs the same with a slow sound device, or none, which matters because
  its menus wait for their sounds to end. On WSL2 the sound server can be slow for a while
  after it has been idle for some minutes; the sound then stutters at first.

With `DX_AUDIO_WAV` both write what was mixed to a WAV file. At exit both report the
length and the peak level of the mixed sound.

### Input

- `dx_host` takes scripted input only (`DX_SCRIPT`), which is passed straight to the SDL
  subset.
- `dx_host_sdl` puts the window's input through the firmware's own input layer
  (`src/pico/dx_input.cpp`). `dx_sdl.cpp` takes the place of the board's USB host
  (`src/pico/dx_usb.cpp`) and records the same raw events: keys as HID usages (SDL2's
  scancodes are HID usages), mouse buttons and wheel notches, and the gamepads merged
  into one with their buttons named by position. The firmware's code turns them into SDL
  events: keys with the characters of the US layout, gamepad buttons into joystick 0 as
  the engine is configured for it (`cmake/dx_engine.cmake`). The mouse position is
  absolute. Scripted input also works.

### Memory

The engine's `malloc` family is redirected to the PSRAM model by the linker
(`--wrap=malloc` and the others), and `src/port/dx_heap.cpp` replaces the global
`operator new` and `operator delete`. The pool is 8 MiB minus the engine's static data,
which `tools/static_bytes.py` measures from the engine's object files at build time
(`build/gen/dx_static_bytes.h`). Allocations carry a category (dungeon, monster, sound,
and so on); `dx_heap_mark()` reports the heap per category (lines starting with `MARK`),
for example after each level load and at exit.

When the pool runs out, the program writes a heap report (`MARK out of memory`) and the
flash cache's usage and map, and aborts; the shell then reports the run as aborted.

`DX_FLASH_SIM=1` moves every cacheable allocation into a separate simulated pool, which
shows what would stay in PSRAM if every asset were cached. `DX_PSRAM_BYTES` sets another
pool size.

### Flash asset cache

With `DX_FCACHE=1` the flash asset cache (`src/port/dx_fcache.c`) runs on an emulated
NOR flash region of 13312 KB (`DX_FCACHE_KB`). The region is mapped read-only except
while it is erased or programmed, so a write into cached data faults; programming can
only clear bits, as on flash; an erase or program outside the region aborts. At exit the
program reports the cache's contents and hit counts, the amount erased and programmed,
and an estimate of the time this takes on the board (typical NOR timings).
`DX_FCACHE_FILE` keeps the region in a file between runs, so a run can start with the
cache of an earlier one, as the board does after a restart. The number of erases of each
4 KB sector is kept next to it in `<file>.wear`.

`dx_host_sdl` turns the cache on by default, as on the board: the full game does not fit
in PSRAM without it. `DX_FCACHE=0` turns it off.

### Files and the SD card mode

By default the engine's files are the host's files, in the directories given with
`--data-dir`, `--save-dir` and `--config-dir`.

With `DX_SDCARD=<image>` the engine's files go through the board's file layer instead:
`src/pico/dx_fatfs_io.c` on FatFs with the board's `ffconf.h`, on a card image built by
`dx_sdimg`. The engine's file calls (`fopen`, `stat`, `access`, `mkdir`, `remove`,
`rename`, `truncate`, `readlink`) are redirected by the linker to `sdcard/sd_host.c`,
which passes them to the board's code. The game then starts as on the board:
`dx_fatfs_init()`, then the board's arguments (`--data-dir`, `--save-dir` and
`--config-dir` set to `/roms/devilutionx/`, and `-n`), followed by any given on the
command line. The programs' own files (frame dumps, scripts, the flash region, the WAV
file) stay on the host. Above the file layer, glibc's stdio is used instead of newlib's,
with newlib's 1 KB buffer.

`DX_SDCARD_FAIL` makes chosen card reads fail, as a card that does not answer, and
`DX_SDCARD_TRACE` lists every card read and write, with the files opened between them.

### SDL2 in dx_host_sdl

The port's SDL subset defines C functions with the names SDL2 uses (`SDL_Init`,
`SDL_PollEvent`, `SDL_GetError`, `SDL_CreateRGBSurface` and others). If SDL2 were linked
into the program, its calls would reach the subset instead. `dx_sdl.cpp` therefore loads
`libSDL2-2.0.so.0` at run time with `dlopen` and calls it through a table of function
pointers, typed from SDL2's own headers. It is compiled with SDL2's headers only, never
with the subset's.

`dx_sdl.cpp` uses SDL's software renderer and the X11 window's shared-memory framebuffer,
without OpenGL. The program's `operator new` replaces the global one for every library
in the process, and the GL drivers (Mesa, with LLVM) are C++ libraries: with OpenGL they
would allocate from the PSRAM model. As a consequence, `dx_host_sdl` needs SDL's `x11`
video driver, which is SDL2's default; it also works under Wayland through XWayland.
`dx_sdl.cpp` itself does not allocate from the heap.

## Files

| File | Contents |
|---|---|
| `build.sh`, `CMakeLists.txt` | The build of all programs. |
| `dx_host.cpp` | The platform of both programs: clock, screen surface, frame dumps, sound, scripted input, emulated flash, the timedemo test and the start-up. |
| `dx_sdl.cpp`, `dx_sdl.h` | The window, sound and raw input of `dx_host_sdl`. |
| `sdcard/sd_host.c` | The SD card mode: the engine's file calls to the board's file layer. |
| `sdcard/sd_disk.c`, `sdcard/sd_disk.h` | FatFs's disk on a card image, with failing reads and the trace. |
| `sdcard/sd_mkimg.c` | `dx_sdimg`. |
| `sdcard/dx_fatfs_io_host.h` | Renames the POSIX functions of `src/pico/dx_fatfs_io.c`, which on the board replace newlib's, so that they do not replace glibc's here. |
| `sdcard/shim/`, `sdcard/mkimg_conf/` | Stand-ins for Pico SDK headers, and the FatFs configuration of `dx_sdimg`. |
| `fixtures/WarriorLevel1to2/` | DevilutionX's reference demo and hero for the timedemo test. |

[TECHNICAL.md](../TECHNICAL.md) describes the port itself and contains the measured
figures.
