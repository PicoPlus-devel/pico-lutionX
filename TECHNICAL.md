# pico-lutionX: technical notes

How the port is put together, how its memory is used and how to test it. The feasibility
study that preceded it is in [docs/FEASIBILITY.md](docs/FEASIBILITY.md).

## Layout

| Path | Contents |
|---|---|
| `3rdparty/devilutionX/` | DevilutionX 1.5.5 (`Source/`, `assets/`), patched in place; every change is marked `pico-lutionX` ([UPSTREAM.md](3rdparty/devilutionX/UPSTREAM.md)) |
| `3rdparty/dx-deps/` | the engine's libraries: libmpq, zlib (inflate), PKWare, {fmt}, SimpleIni, tl, hoehrmann_utf8, libsmackerdec, dr_mp3 |
| `3rdparty/pico_shared_drivers/`, `3rdparty/tusb_xinput/` | HDMI, PSRAM, FatFs, I2S/TLV320, USB HID and NES pad drivers, copied from pico-quake2 |
| `3rdparty/tlsf/` | the heap allocator |
| `src/sdl/` | an SDL 1.2 subset (video, events, keyboard, mouse, joystick, RWops, timer) over a small platform interface, `sdl_backend.h` |
| `src/aulib/` | an SDL_audiolib subset: streams, a mixer, PCM WAV and MP3 (dr_mp3) decoders |
| `src/port/` | the engine heap (`dx_heap`) and the flash asset cache (`dx_fcache`), shared by the board and the test program |
| `src/pico/` | the RP2350 platform layer: boot, display, audio, SD card, flash, USB, NES/SNES and Wii pads |
| `hosttest/` | the test programs for a PC: the same engine and SDL code on Linux (`-m32`), headless and in a window ([hosttest/README.md](hosttest/README.md)) |
| `tools/` | measurement and release tools |

The engine is built with `USE_SDL1`, `NONET`, an 8-bit screen surface with
`SDL_DOUBLEBUF` (the engine then draws straight into it) and without the 128 KB black
transparency table. `cmake/devilutionx.cmake` holds the sources and settings, shared by the
firmware (`cmake/dx_engine.cmake`, `libdx_engine.a`) and the test program.

## Memory

| Memory | Contents |
|---|---|
| SRAM (520 KB) | the 640×480 front buffer (300 KB), the platform layer, drivers, the audio ring, dr_mp3's scratch space (16 KB), the engine's light tables (4.75 KB) and pixel loops, core0's stack (16 KB); about 91 KB are left for the drivers' `malloc`, of which the HDMI audio and I2S rings take 52 KB at boot |
| PSRAM (8 MB) | all static data of `libdx_engine.a` (about 0.66 MB, `.psram_bss`/`.psram_data`), then the engine heap (TLSF, about 7.3 MB) |
| Flash (16 MB) | pico-bootLoader (512 KB, bootloader builds), the firmware (about 2 MB in a 2.5 MB slot), the flash asset cache (13 MB at `0x10300000`) |

- `cmake/psram_linker.cmake` places the engine's static data in PSRAM. It is zeroed
  and copied, and the heap is set up, in a runtime-init hook (`dx_early_init` in
  `src/pico/dx_boot.c`). The hook runs before the C++ static constructors, which already
  use engine globals and the heap.
- After the engine library is built, its calls to `malloc`, `free` and the rest of that
  family are renamed to `dx_malloc` etc. (`cmake/dx_engine.cmake`). The engine therefore
  allocates from the PSRAM heap, while the SDK and the drivers keep `malloc` in SRAM.
  `operator new`/`delete` go to the PSRAM heap for all code.

### Hot code and data in SRAM

Every access to flash or PSRAM goes through the 16 KB XIP cache, which flash code, the
flash asset cache and PSRAM data share. `src/port/dx_sram.h` places chosen data
(`DX_SRAM_BSS`) and functions (`DX_SRAM_CODE`) in SRAM instead:

- **Pixel loops:** the engine's per-pixel colour map loops (`engine/render/blit_impl.hpp`):
  lighting, blended transparency and colour translations. They took about 40 % of
  core0's time on a dungeon level. `src/port/dx_blit.c` runs them from SRAM, four pixels
  per step, in runs of up to 16 words: the back buffer is read and written a run at a time,
  so each run is one QSPI burst.
- **Light tables:** the 256-byte light tables that every lit pixel looks up
  (`lighting.cpp`).
- **MP3 decoding:** dr_mp3's scratch space.

### Stacks

- **Core0:** 16 KB at the top of SRAM (`cmake/psram_linker.cmake`). `main` paints it
  and sets the stack limit register (MSPLIM), so an overflow ends in a HardFault with
  "core0 stack overflow" on the UART instead of overwriting the SRAM heap below.
- **Core1:** 4 KB in the SCRATCH_Y bank (`dx_display.c`), for the scanline callback and
  the audio pump. It is painted at launch.
- **MP3 decoding:** dr_mp3 keeps 17 KB of scratch space on the stack per decoded frame.
  It is static here, in SRAM (`src/aulib/aulib_mp3.cpp`).
- **Largest remaining frames:** found by compiling the whole firmware with
  `-fstack-usage`:
  - `LoadMultipleCl2Sheet`, 4.6 KB, while a level loads;
  - `MpqWriter::WriteFileContents`, 4.3 KB, while saving;
  - `recv_plrinfo`, 16.5 KB, multiplayer only and not reachable in this build.

### The flash asset cache

The engine's sprite data does not fit in PSRAM: a dungeon level of the shareware data
needs 8.2–8.5 MB of heap, about 7.5 MB of it sprites. The flash asset cache
(`src/port/dx_fcache.c`, after pico-quake2's) keeps read-only data in flash, where it is
used in place through the XIP window:

- **Sprites:** CEL, CL2 and CLX sprites after their conversion (`engine/load_*.cpp`).
- **Dungeon tiles:** these files are copied from the MPQ straight into flash, without a
  copy in PSRAM.
- **Sound effects:** stored as IMA ADPCM WAV files (4 bits per sample), converted once on
  the way in:
  - MP3 (`spawn.mpq`): 2.6 MB for all effects, against 10 MB as 16-bit PCM; decoding
    several MP3 streams while they play cost up to 65 ms per frame in fights.
  - 8-bit PCM WAV (`DIABDAT.MPQ`): 2.3 MB instead of 4.6 MB for a level's effects, which
    the deep levels need (see "The full game").
- **Monster sprites:** one buffer per animation (`LoadMonsterAnims` in `monster.cpp`),
  owned through a reference block (`dx_heap_refblock`) that frees or releases them with the
  monster type. An animation stays in PSRAM, which draws faster and writes no flash,
  unless that would leave less than 2 MB contiguous; then it goes into the cache, where
  later visits find it.

Details of the cache:

- **Entries:** each entry is a contiguous run of 256-byte pages with a CRC. The directory
  is written to one of two alternating 128 KB slots after a level load that stored or
  removed entries. A level load that only uses entries changes just their age stamps;
  those are written with the next change, or after eight level loads in one session.
- **Reference counts:** freeing a pointer into the cache releases the entry (`dx_heap.cpp`
  passes it on). Entries with references are never evicted.
- **Writes in place:** the engine changes some sprites after loading (colour
  translations). Those are stored after the change (monsters), stored under a separate key
  (player class colours), or cloned first (the health bar).
- **Placement and eviction:** entries up to 128 KB (sound effects, item and object
  sprites) fill the region from the top, larger ones (tiles, player and monster
  animations) from the bottom. Each takes free room or room held by data unused for more
  than eight level loads, whichever lies nearer its end. Only when there is neither is
  younger data evicted, the oldest first, in tiers: this level load, the two before it,
  the six before those. An eviction removes the entries in one window of the needed size,
  so it leaves no scattered holes. The earlier rule told entries apart only as "used in
  this level" or "older": on every visit to town the loading-screen art evicted the town's
  tiles (2.1 MB), which the town load then wrote again (see "Flash wear").
- **Sound effects during a level load:** about 2 MB of the game's sound effects (those of
  the hero's class and the general ones) are in use for the whole game. `LoadGameLevel`
  frees those that are not playing at its start (`effects_release_game_sfx`) and loads
  them again at its end, so that the level's graphics may evict them when nothing else is
  left. On level 16 this decides whether Diablo's animations find room in the cache.
- **Not cached:** the main menu's art (`ui_art\`, `DxCacheActive` in `dx_fcache.hpp`):
  the menus run while the game's memory is free, and a long game would evict the art and
  the next start write it again. The widescreen loading-screen art is not loaded at all,
  because the 640×480 picture covers it on this screen.
- **Persistence:** the cache survives restarts. A different game data file (`spawn.mpq`
  or `DIABDAT.MPQ`, identified by its size) discards it, and so does firmware with a
  different directory version (version 2 introduced the placement above).
- **Diagnostics:** `dx_fcache_print_usage` lists the contents by kind (in use and total)
  and `dx_fcache_print_map` draws the region, 64 KB per character. Both are printed when
  the PSRAM heap runs out. `dx_fcache_get_stats` counts, among others, the evictions of
  entries used in the last three level loads (`recent_evictions`), which measure thrash.

Flash writes run from SRAM with every interrupt masked except the PIO-USB frame
interrupt (`src/pico/dx_flash.c`, `dx_usb_frame.c`); with the native USB controller,
which sends its frames by itself, every interrupt is masked. Core1 keeps scanning out from SRAM, and its
audio pump is held off meanwhile. Every erase and write is checked against the cache
region first: below it lies the firmware, and an address past the chip's end wraps round
onto the firmware. A write outside the region stops the game with an error message. The
test program applies the same check to its emulated flash.

### Flash wear

Measured in the test program (`hosttest/levels.sh warm`, see "The test program") on one
emulated flash per scenario. A trip is one run: start, a new hero in town, `+goto` to a
level, so two level loads. The figures are per trip once every listed level has been
visited; flash time uses typical NOR timings (64 KB block erase 150 ms, page 0.4 ms).

| Scenario | Flash erased | Flash time | Most-erased 4 KB sector |
|---|---|---|---|
| Shareware, 100 trips to levels 1–4 | 2.68 → 0.04–0.07 MB | 11.2 → 0.2–0.3 s | 1.6 → 0.03 erases |
| Full game, in order (1, 1, 2, 2, 1, 2 … 16), 63 trips | 4.25 → 2.0–3.2 MB | 19.8 → 9–14 s | 1.0 → 0.9–1.5 erases |
| Full game, 200 trips to random levels 1–16 | 5.22 → 3.9–4.4 MB | 24.4 → 18–21 s | 1.5 → 1.7–1.9 erases |
| Full game, 150 trips to levels 13–16 | 5.29 → 5.1 MB | 24.8 → 23.5 s | 1.0 → 1.6–1.7 erases |

The arrow separates the earlier cache (version 1) from the current one; ranges are two runs
of the same firmware, which differ because the test program runs on a real-time clock.
The earlier cache's random and level 13–16 figures are lowered by its failed trips (see
"The full game"), which write nothing that is kept.
With the shareware data the cache holds everything after the first visits, and the earlier
version rewrote the town's tiles and the loading-screen art on every trip. The full game's
graphics do not fit in 13 MB, so trips between parts of the dungeon replace copies either
way. The flash is rated for at least 100,000 erase cycles per sector; at two erases per
trip the most-erased sector reaches that after about 50,000 trips. Directory commits fell
from two per trip to 0.2 (shareware) and 1.3–2.0 (full game).

## Video

The engine draws into its 640×480 8-bit back buffer in PSRAM, which it reaches through
the uncached XIP window (`sdlb_screen_alloc` in `src/pico/dx_backend.cpp`). A cached write
that misses first reads the whole 8-byte line from the PSRAM and writes it back later. An
uncached write goes straight to the PSRAM, and writes that follow on in address are
chained into one QSPI transfer. The 300 KB drawn and copied each frame also stay out of the
16 KB cache.

On every frame, `dx_display_present` copies the back buffer into the front buffer in SRAM.
The copy starts once the beam has read row 300 of the picture on screen, in the last 6 ms
before the vertical blank (the wait mixes audio). It runs top to bottom faster than the
beam, so it only overwrites rows that this refresh has already shown, and after the
vertical blank it stays ahead of the beam. Each row also waits for the beam in case the
copy catches up with it. The copy takes about 6.2 ms, limited by the PSRAM reads (a copy
loop that loads 8 words at a time measured the same). The status line reports its duration
and the frames the beam reached first anyway.

The copy used to start at the vertical blank. That left only the 1.1 ms up to the first
visible line, and a mixing call (MP3 decoding, an SD card read, about 2 ms on average) that
was still running at the vertical blank made the copy start later: the top of the picture
showed the previous frame for one refresh. On a large screen this showed as tearing, most
with Zoom off, where each wait has more audio to mix. A mixing call that runs past the
window now delays the frame by one refresh instead.

The copy can also run on core1 (CMake option `DX_COPY_ON_CORE1`, for example
`CMAKE_ARGS=-DDX_COPY_ON_CORE1=ON ./adafruitdvisd-build.sh`), while core0 goes on with input,
game logic and audio. On HW_CONFIG 2 that gained nothing: both cores share the one QSPI bus,
so the copy took 9–17 ms instead of 6 and slowed core0 by as much as it saved. Above about
15 ms it also falls behind the beam. The rules it needs stay in place:

- Before the engine draws into the back buffer it waits for the copy to finish. The waits
  are in `GlobalBackBuffer()` and `DiabloUiSurface()` (engine patches), and in the SDL
  subset's fills, blits and `SDL_LockSurface` on the screen (`sdlb_screen_wait`). Flash
  writes wait too, because they take PSRAM off the bus.
- The test program enforces the rule: after each present the screen's pages are read-only
  until the next wait, so a drawing path without a wait faults with its stack.

Core1's scanline callback converts each row through a 256-entry RGB555 table for HSTX.
Palette changes take effect at the vertical blank.

## Sound

The engine's sound code is unchanged; it uses the SDL_audiolib subset in `src/aulib`:

- **Mixing:** streams are mixed on core0 at 22050 Hz. This runs whenever the engine polls
  for events, presents a frame or waits. Decoders read from the SD card, so mixing never
  runs on core1 or in an interrupt.
- **Output:** core1 plays the ring (`src/pico/dx_audio.c`, after pico-quake2's), doubles
  the rate to 44.1 kHz and feeds HDMI. On the Fruit Jam the jack takes over when
  headphones are plugged in; on HW_CONFIG 2 HDMI and the I2S DAC both play.
- **Formats:** the `spawn.mpq` that DevilutionX distributes stores sounds and music as MP3.
  - **Music:** decoded while it plays (dr_mp3, `src/aulib/aulib_mp3.cpp`).
  - **Sound effects:** decoded once into IMA ADPCM in the flash asset cache
    (`DecodeMp3ToAdpcmWav`, `src/aulib/ima_adpcm.h`) and played from there by the WAV
    decoder, which keeps no buffer per sound: all loaded effects have a decoder at once.
  - **The full game:** its effects are 8-bit PCM WAV (210 of 212, 22050 Hz mono), converted
    to IMA ADPCM in the cache the same way (`EncodeWavToAdpcmWav`). Measured over all 212
    against the originals: signal to noise median 24 dB, worst 10 dB; bright, metallic
    sounds (coins, picking up items) fare worst (about 15 dB), low and smooth ones best
    (over 40 dB). Its music is PCM WAV and played as it is.

## Input

- **USB host:** Pico-PIO-USB on the Fruit Jam (`HAS_USBPIO`). On the Pico Plus 2 board the
  native controller is the host, on the module's USB-C port as in pico_shared, pico-doom
  and pico-duke3D. At 384 MHz, a multiple of 48 MHz, both boards divide the USB and ADC
  clocks from PLL_SYS and retask PLL_USB as the 126 MHz HSTX clock (`dx_boot.c`), so the
  native controller keeps its 48 MHz.
- **PIO-USB and erratum RP2350-E9:** the Fruit Jam's RP2350 is an A2. On that revision, a
  pad with its input enabled holds a line that has been high at about 2.2 V, and the
  pull-down cannot bring it back. Pico-PIO-USB 0.7.2 switches the inputs off for 8 NOPs
  before it reads the line state; at 384 MHz that is 21 ns, too short to drain the line.
  A device was then often not found: D- stayed high, so D+ and D- read as SE1.
  `dx_usb_frame.c` handles this on chips up to A2:
  - while nothing is attached, it leaves the inputs off between frames and reads the
    line state itself, after switching them back on;
  - while a device is attached, it drains the lines for 5 µs every 16 frames, so that an
    unplug reads as SE0.
- **SDK and TinyUSB version:** the port is built with pico-sdk 2.2.0 and the TinyUSB it
  carries (0.18.0, commit 86ad6e56). A hub with a keyboard and mouse on the native
  controller was reliable with TinyUSB master of 2025-08-07 (5fb3c0996), whose driver for
  the native controller is that of 0.18.0 (it also resets EPX when a device is unplugged
  during a transfer); its host stack and hub driver are newer. With master of May 2026,
  whose driver for the native controller was rewritten, devices behind a hub were often
  lost or not found: status stages timed out, and the driver panicked
  (`buf_ctrl … already available` on the retry of a control transfer, `Data Seq Error`).
  `dx_usb.cpp` warns at build time when TinyUSB is newer than 0.18.0 on a board with the
  native controller. The vendored XInput driver builds with either version (`open()`
  returns `bool` before TinyUSB commit ef018e364, the descriptor length after it).
  pico-sdk 2.3.1 does not configure: it splits `memmap_default.ld` into included files,
  which `cmake/psram_linker.cmake` does not handle yet.
- **USB log:** `-DDX_USB_LOG=ON` (for example
  `BUILD=build_usblog ./adafruitdvisd-build.sh -DDX_USB_LOG=ON`) builds TinyUSB with its
  host log (`CFG_TUSB_DEBUG` 2) on the UART: enumeration, hub ports and every transfer.
  For diagnosing devices only, since printing waits for the UART.
- **Sampling:** the USB host is run from a 4 ms timer interrupt. Changes of
  keys, mouse buttons and pad buttons are recorded in a ring (`src/pico/dx_usb.cpp`) and
  turned into SDL events in thread context (`dx_input.cpp`).
- **USB and the SD card:** while a device enumerates, TinyUSB (without an OS) busy-waits
  in that interrupt: 450 ms after an attach, 50 ms per port reset. The SD driver gives up
  on a block after 200 ms by the clock, even if the card answered in the meantime, so
  FatFs's calls into the driver hold the timer off for each transfer (`--wrap` of
  `disk_read`, `disk_write` and `disk_ioctl`, `dx_fatfs_io.c`). Without this, devices that
  attached while DIABDAT.MPQ was being opened made SD reads fail at start-up.
- **Gamepads:** USB pads and the NES/SNES ports are merged into SDL joystick 0. The engine
  is told the button numbers with the `JOY_*` definitions in `cmake/dx_engine.cmake`, and
  its default padmapper layout (`InitPadmapActions`) is used unchanged. The stick clicks
  of XInput, DualShock 4 and DualSense pads are reported as `Button::LSTICK`/`RSTICK`
  (local change in `usb_hid`), their PS button as Select+Start, like XInput's Guide.
- **NES pads:** two buttons cannot reach the padmapper's actions, so `dx_nespad.cpp`
  translates a NES pad: B is the top face button, and Select is a shift key that turns
  A, B, Up and Down into the left and bottom face buttons and L and R; with Start held, A
  and B are the bottom and left face buttons of the engine's Start menu. The main menu
  screens select with the top face button as well (engine patch in
  `controls/menu_controls.cpp`).
- **SNES Mouse:** with `NESPAD_SNES_MOUSE` (board header) the nespad driver clocks 32 bits
  per read and reports a mouse in either port through `nespad_mouse[]`. `dx_usb.cpp`
  merges its buttons with a USB mouse's. Its motion is accelerated in `dx_nespad.cpp`,
  from 4 cursor pixels per count when it moves slowly to 16 when it moves fast (the speed
  averaged over 32 ms), and added in `dx_input_mouse`. The mouse's own higher
  sensitivities are not used: they only boost 4 counts or more between two reads, and
  at a read every 4 ms that takes a 15 inch/s swipe.
- **Controller-port reads:** `dx_nespad.cpp` collects a read and starts the next at most
  once per millisecond. A start within about 5 us of the previous read's end is lost and
  the state machine then waits forever, and after the interrupts were off (a flash erase,
  a USB device enumerating) the 4 ms timer runs the missed ticks back to back. Before
  this, the ports could be disabled after a level load ("a read never completed").
- **Wii pad (Fruit Jam):** read in thread context every 10 ms (`dx_wiipad.cpp`, after
  pico-duke3D's), because it shares the I2C bus with the TLV320 codec. It is brought up
  before the codec.

## The status line

With the CMake option `DX_PERF_LOG` (for example
`CMAKE_ARGS=-DDX_PERF_LOG=ON ./adafruitdvisd-build.sh`), the firmware writes a report to the
UART every 5 seconds. It is off by default: `printf` waits for the UART, and at 115200 baud
the report of about 370 bytes holds core0 for some 32 ms. With the hot-code list as well
(CMake option `DX_HOT_CODE`, see below) it is about 900 bytes and 80 ms, longer than the
audio mix runs ahead, so the sound stutters while it is written. The report:

```
status: 12.0 fps, heap 844/7522 KB (peak 1009), SRAM 40 KB free, stack 6120/16384 B core0 900/4096 B core1, frame copy 5.8 ms (max 7.1, 0 torn), cache 294 assets 7571 KB, 290 hits 4 stored, 40 KB written
time per frame (ms): other 2.1 logic 5.0 view 40.2 draw 6.0 copy 6.2 vsync 8.0 audio 9.1 sd 1.0 input 0.3 flash 0.0, XIP cache 92% hits of 410 M accesses
```

(The values above are an illustration of the format, not measurements.)

- **First line:**
  - the frame rate;
  - the PSRAM heap (`dx_heap`) now and at its peak;
  - the SRAM left for `malloc`;
  - the deepest use of both stacks;
  - how long the frame copy took (with the copy on core1, from the vertical blank; it
    must stay below about 15 ms to keep ahead of the beam), and how many frames the beam
    reached before the copy did, which tore the picture (the copy on core0 only);
  - the flash asset cache counters.
- **Second line:**
  - the time of an average frame per zone (`src/port/dx_prof.h`). The zones nest, so each
    millisecond is counted once:
    - `logic`: the engine's game tick (`game_loop`);
    - `view`: the dungeon or town (`DrawView`);
    - `draw`: the rest of `DrawAndBlit`;
    - `vsync` and `copy`: waiting for the copy window before the vertical blank and
      copying the frame (with the copy on core1, `vsync` is the wait for it);
    - `audio`, `sd`, `input`, `flash`: the platform layer;
    - `other`: everything else (menus, level loading);
  - the hit rate of the 16 KB XIP cache that flash code, flash data and PSRAM share.
- **Hot code:** only with the CMake option `DX_HOT_CODE` as well (for example
  `CMAKE_ARGS="-DDX_PERF_LOG=ON -DDX_HOT_CODE=ON" ./adafruitdvisd-build.sh`). Core0's
  program counter, sampled 2000 times a second (TIMER0 alarm 0), as the 40 busiest 16-byte
  blocks of code with their sample counts. The addresses only apply to the ELF file of the
  same build: `arm-none-eabi-addr2line -f -C -e pico_lutionx.elf 0x1004a3c0`.

## The test program

`hosttest/build.sh` builds `hosttest/dx_host` (with AddressSanitizer and UBSan) and
`NOSAN=1` builds `hosttest/dx_host_nosan`. With the 32-bit SDL2 installed
(`libsdl2-dev:i386`) it also builds `hosttest/dx_host_sdl`: the same program in a window,
with sound, keyboard, mouse and gamepads through the firmware's input layer, on real time.
[hosttest/README.md](hosttest/README.md) describes both programs in detail. The test
program runs the engine with:

- a heap the size of the board's free PSRAM;
- the flash asset cache on emulated NOR flash (`DX_FCACHE=1`), mapped read-only outside
  writes so that any write into cached data faults;
- PPM frame dumps (`DX_DUMP_EVERY`, `DX_DUMP_DIR`);
- audio into a WAV file (`DX_AUDIO_WAV`);
- scripted input (`DX_SCRIPT`);
- the board's file layer on a card image (`DX_SDCARD`, see below).

```sh
# data directory: spawn.mpq and the assets
mkdir -p /tmp/dx/data
ln -s /path/to/spawn.mpq /tmp/dx/data/
ln -s "$PWD/3rdparty/devilutionX/assets" /tmp/dx/data/assets

# upstream's reference demo, headless (prints TIMEDEMO: Same)
cp -r hosttest/fixtures/WarriorLevel1to2 /tmp/dx/fixture
hosttest/dx_host --timedemo-test /tmp/dx/fixture /tmp/dx/data

# the same demo rendered, with the flash cache and sound
cp -r hosttest/fixtures/WarriorLevel1to2 /tmp/dx/save
DX_FCACHE=1 hosttest/dx_host --data-dir /tmp/dx/data --save-dir /tmp/dx/save \
    --config-dir /tmp/dx/save --demo 0 --timedemo -n --spawn

# memory per level: total, with all sprites cached, and with the real cache
hosttest/sweep.sh /tmp/dx/data

# the full game: each level on an empty cache, then all of them in order on one cache
NOSAN=1 hosttest/build.sh
hosttest/levels.sh /path/to/diabdat-data cold
hosttest/levels.sh /path/to/diabdat-data warm

# flash wear: any sequence of levels on one cache (each run: start, town, the level)
hosttest/levels.sh /path/to/diabdat-data warm 3 1 16 9 16 2
```

Writes into the screen surface without a wait for the frame copy fault (see "Video"),
and flash writes outside the cache region abort. With `DX_FCACHE_FILE` the emulated flash
also counts the erases of every 4 KB sector, kept in `<file>.wear`. `levels.sh` in `warm`
mode ends with a summary once every listed level has been visited: flash erased, flash
time on the board (typical NOR timings), directory commits, the most and the average
erased sector, and evictions of recently used entries per run. `DX_HOST=<binary>` runs
another build of the test program, for example to compare two cache versions.

In the SD card mode the engine's files go through the board's own file layer,
`src/pico/dx_fatfs_io.c` on FatFs with the board's `ffconf.h`, on an image of a card
instead of the host's file system, and the game starts as on the board (`dx_fatfs_init`,
then the board's arguments). `hosttest/dx_sdimg` builds the image: FAT32 or exFAT, any
cluster size, and optionally with every large file split into fragments. Above the file
layer the test program uses glibc's stdio instead of newlib's, with newlib's 1 KB buffer.

```sh
# a card with the game data where the board expects it, then a start as on the board
hosttest/dx_sdimg -t fat32 -c 32 /tmp/dx/card.img /path/to/diabdat-data=/roms/devilutionx
DX_SDCARD=/tmp/dx/card.img DX_MAX_FRAMES=300 hosttest/dx_host --verbose

# exFAT, 128 KB clusters, large files in 40 fragments
hosttest/dx_sdimg -t exfat -c 128 -f 40 /tmp/dx/card.img /path/to/diabdat-data=/roms/devilutionx

# a card that fails to answer: every SD read numbered, then read 100 fails
DX_SDCARD_TRACE=1 DX_SDCARD=/tmp/dx/card.img DX_MAX_FRAMES=300 hosttest/dx_host 2> trace.log
DX_SDCARD_FAIL=100 DX_SDCARD=/tmp/dx/card.img DX_MAX_FRAMES=300 hosttest/dx_host
```

With `DX_SDCARD_TRACE` the opened files are listed with the SD reads before them.
`DX_SDCARD_FAIL` takes a comma-separated list of read numbers.

Other tools:

- `tools/arm_size.sh` links the engine for the RP2350 against a stub platform (code size,
  static data, stack frames).
- `tools/mpqsizes.py` lists what an MPQ contains.
- `tools/peakheap.c` measures the heap of a stock DevilutionX build.
- `tools/make_testsaves.sh` makes the saved games in `testsaves/` with the test program:
  one hero per dungeon level, taken there with debug commands, saved, and loaded again to
  check it (see [testsaves/README.md](testsaves/README.md)).

## Reset and BOOTSEL on the Pico Plus 2

Pressing the reset button sometimes leaves the Pico Plus 2 in BOOTSEL mode. This happens
mostly on a dungeon level and sometimes in the menu. Further resets do not help, but a
power cycle does.

- **Cause:** the PSRAM's chip select (GPIO47) has only a 100 kΩ pull-up on the board (R12).
  A reset restores the RP2350's internal pull-down on every GPIO, 36–113 kΩ (datasheet
  sections 9.7 and 14.9). Together they put chip select at about 0.9–1.75 V, which the
  APS6404L reads as selected (it needs about 2.3 V, 0.7 × VDD, to be deselected).
- **What happens:** if the reset falls in the middle of a PSRAM read, the PSRAM never
  sees chip select go high. It goes on driving the shared QSPI data lines whenever the
  bootrom clocks the bus to read the flash, so no image is found and the bootrom enters
  BOOTSEL. Every further reset finds the PSRAM in the same state; only removing power
  ends it. At power-on the PSRAM is not yet initialised and stays off the bus.
- **Other firmware:** it is not specific to this port. Any firmware that reads the PSRAM
  heavily on this board can meet it; this port reads it on every frame.
- **Remedies:**
  - *Hardware:* a stronger pull-up on the PSRAM chip select (10 kΩ instead of R12's
    100 kΩ, about 2.6–2.9 V).
  - *OTP (permanent, applies to every firmware on that board):* program FLASH_DEVINFO
    with CS1_GPIO = 47 and CS1_SIZE = 8 MB (CS0_SIZE = 16 MB) and set
    BOOT_FLAGS0.FLASH_DEVINFO_ENABLE. The bootrom then drives GPIO47 as the second chip
    select and sends its XIP exit sequence to the PSRAM before it looks for an image
    (datasheet section 5.4 and OTP_DATA FLASH_DEVINFO).
  - *Use:* restart with Quit Game (game menu) or Exit Diablo (main menu). Both end in
    `_exit(0)` (`src/pico/dx_boot.c`), which resets through the watchdog while the PSRAM
    is idle.

## The full game

`DIABDAT.MPQ` was tested in the test program, with the board's PSRAM and flash cache, by
loading every level (`hosttest/levels.sh`). It has not run on a board yet. Three changes
were needed beyond the shareware data:

- **Monster sets:** upstream builds a set in one buffer, which takes about twice the set
  in PSRAM at its peak. The full game's sets reach 1.8 MB (Diablo 3.4 MB), so levels 10, 12
  and 16 ran out of memory. Sets are now loaded one animation at a time (see "The flash
  asset cache"), so the peak is about twice the largest animation (841 KB).
- **Sound effects:** stored as IMA ADPCM, half their size as 8-bit PCM (see "Sound").
  Before that, a level's effects took 4.6 MB of the cache and were in use all the time.
- **Cache placement:** small entries fill the cache from the top and large ones from the
  bottom, and the game's sound effects are released while a level loads, so monster
  animations of up to 1 MB find room on a cache that is mostly in use (see "The flash
  asset cache").

Results, with a new hero taken to each level by `+goto`:

| | Levels 1–15 | Level 16 |
|---|---|---|
| PSRAM, largest free block after loading | 2.1–3.9 MB | 2.8 MB |
| Flash cache in use after loading | 11.1–12.7 of 12.75 MB | 12.6–12.7 MB |
| Flash written on a first visit (warm cache) | 0.2–3.9 MB | 7.8 MB |

Level 16 holds 8.5 MB of monster graphics, about half in PSRAM and half in flash. Its first
visit writes the most, about 45 s of flash work on the board: Diablo's animations, and
sound effects that made room for them and are written again at the end of the load.

On a cache with a long history of all parts of the dungeon, level 16 can still run out of
memory: about 8 MB of the cache is then in use at once by graphics stored at different
times (player and monster animations, tiles), which cannot be moved while in use, and no
1 MB window is left for the next animation, which then stays in PSRAM until PSRAM runs
out. In the test program:

| Scenario | Earlier cache | Current cache |
|---|---|---|
| In order (63 trips) | no failure | no failure |
| Levels 13–16 (150 trips) | first failure at trip 57 and 67 | no failure (5 runs) |
| Random levels 1–16 (200 trips) | first failure at trip 49 | no failure (2 runs) |
| Random levels 1–16, one trip in five to level 16 (200 trips) | first failure at trip 28 and 58 | first failure at trip 45–102 (4 runs) |

A failed load leaves the cache as it was, so later visits to level 16 fail again until
trips to other levels have changed the cache. Loading a game saved on level 16 does work on
such a cache. Tested on two cache states, each saved just before a failing trip:

| After the failure | State 1 | State 2 |
|---|---|---|
| New game, town, then level 16 (the failing route) | failed 5 of 5 | failed 3 of 3 |
| Load a game saved on level 16 | loaded 5 of 5 | loaded 5 of 5 |
| One trip to level 5, 9, 12 or 14, then load that save | loaded 4 of 4 | — |

Loading the save loads level 16 directly after start-up, with no other level load before
it in the same session. Why the town load before it leaves too little room was not
determined. On the board a failed load also leaves its flash writes behind; entries they
overwrote fail their CRC on the next start and are loaded again.

## Open items

- **Board testing:** everything in the README's list of what has not been verified.
- **Performance:**
  - **Measured (HW_CONFIG 2):**
    - main menu and town: 12 to 18 frames per second;
    - first cathedral level: 5 to 9 frames per second. A frame took 61–69 ms to draw
      the dungeon, 8–19 ms of game logic, 10–65 ms of audio (MP3 effects, since stored as
      ADPCM), 7–15 ms of waiting for the vertical blank and 6.2 ms of copying;
    - XIP cache hit rate 97–98 % of about 90 million accesses per second.
  - **Why it matters:** in single player `game_loop` runs at most one 20 Hz game tick
    per drawn frame, so below 20 frames per second the game itself slows down.
  - **After the ADPCM sound effects:** 9 to 11.6 frames per second on the first cathedral
    level. Drawing the dungeon took 50–70 ms, audio 10–15 ms (mostly music), logic
    5–15 ms. The hot-code samples put the pixel loops at about 40 % of core0's time; they
    now run from SRAM (see above), and the wait for the vertical blank mixes audio ahead.
  - **After that:** 11.6 to 14.1 frames per second on the first level, the dungeon in
    45–57 ms. `dx_blit_map` took about 38 % of core0's time, all of it waiting on memory:
    per 8 pixels a line fill from flash (the tile) and, for the back buffer, a line fill
    from PSRAM plus a write-back. The back buffer is now written through the uncached
    window instead.
  - **Uncached back buffer:** 15.4 to 19.6 frames per second on the first level, the
    dungeon in 28.5–40 ms; the frame copy (6.1 ms) and the wait for the vertical blank
    (3–6 ms) then took a fifth of a frame. Moving the copy to core1 gained nothing (see
    "Video"). The pixel loops now read and write the back buffer in runs, which halved the
    share of the semi-transparent walls (about 11 % to 6 %).
  - **Where the time goes now (14–19 fps):** per frame the one QSPI bus carries about
    300 KB of tile data from flash, about 340 KB of pixels written to PSRAM and 300 KB read
    back for the copy: some 25 ms of transfers before any computing. Further gains need
    fewer bytes on the bus rather than faster code.
  - **Zoom:** the engine's Zoom option draws the view at a quarter of the pixels and then
    doubles it in place, byte by byte from the end of the screen backwards. With the
    uncached back buffer every one of those bytes was a QSPI transfer of its own (QMI only
    chains ascending accesses), and Zoom took about 25 % of core0's time, so it gained
    nothing (about 15 fps). It now doubles row by row through SRAM in ascending order:
    with Zoom on, the first level runs at 27–32 fps and the view takes 16–22 ms.
  - **Further candidates:**
    - music decoding from SRAM;
    - the engine's Zoom option (half-resolution dungeon view);
    - running more than one game tick per frame;
    - 504 MHz.
- **The full game:** all 16 levels load in the test program with the board's memory
  (`hosttest/levels.sh`, cold and warm), but it has not run on a board. Level 16 is the
  tightest: 8.5 MB of monster graphics (Diablo alone 3.4 MB), half in PSRAM, half in
  flash, and an almost full cache; its first visit writes 7.8 MB to flash. After a long
  history of all parts of the dungeon it can still run out of memory (see "The full
  game"); a lasting fix needs less memory on level 16, and a way for the player to clear
  the cache would allow recovery. Whether the ADPCM sound effects are acceptable needs
  listening on the board.
- **Licences:** the drivers come from pico_shared (GPL-3.0) and libmpq is
  GPL-2.0-or-later. Neither may be distributed in one firmware with DevilutionX's licence
  (docs/FEASIBILITY.md, section 7). This must be settled before a release.
- **pico-bootLoader registration:**
  - an entry in `emu/emulators.txt` (`pico-lutionX;devilutionx;DevilutionX`) and in
    `emu/ports.txt`;
  - artwork in `emu/assets/themes/*/devilutionx.png` (plus `.444`/`.555`);
  - entries in the `SCRIPTED_*` tables of `build_emulators.sh` (script
    `%s-build-forbootloader.sh`, build `build_bl_%s`, output `src/pico`, UF2 name
    `pico_lutionx`, hwconfigs `2 8`, PIO-USB);
  - an updateAll entry that unpacks `pico-lutionX_sdcard.zip` and downloads `spawn.mpq`
    to `/roms/devilutionx`.
