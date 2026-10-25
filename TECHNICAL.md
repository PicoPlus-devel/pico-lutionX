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
| `hosttest/` | the test program for a PC: the same engine and SDL code on Linux (`-m32`) |
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
- **Sound effects:** these are copied the same way. MP3 effects (`spawn.mpq`) are
  decoded once on the way in and stored as IMA ADPCM WAV files (4 bits per sample, about
  2.6 MB for all of them, against 10 MB as 16-bit PCM). Decoding several MP3 streams while
  they play cost up to 65 ms per frame in fights.
- **Monster sprites:** cached only when PSRAM runs short, judged by the largest free
  block, because they change with every level.

Details of the cache:

- **Entries:** each entry is a contiguous run of 256-byte pages with a CRC. The directory
  is written to one of two alternating 128 KB slots after every level load.
- **Reference counts:** freeing a pointer into the cache releases the entry (`dx_heap.cpp`
  passes it on). Entries with references are never evicted; the others go least recently
  used first.
- **Writes in place:** the engine changes some sprites after loading (colour
  translations). Those are stored after the change (monsters), stored under a separate key
  (player class colours), or cloned first (the health bar).
- **Persistence:** the cache survives restarts. A different game data file (`spawn.mpq`
  or `DIABDAT.MPQ`, identified by its size) discards it.

Flash writes run from SRAM with every interrupt masked except the USB frame interrupt
(`src/pico/dx_flash.c`, `dx_usb_frame.c`). Core1 keeps scanning out from SRAM, and its
audio pump is held off meanwhile. Every erase and write is checked against the cache
region first: below it lies the firmware, and an address past the chip's end wraps round
onto the firmware. A write outside the region stops the game with an error message. The
test program applies the same check to its emulated flash.

## Video

The engine draws into its 640×480 8-bit back buffer in PSRAM, which it reaches through
the uncached XIP window (`sdlb_screen_alloc` in `src/pico/dx_backend.cpp`). A cached write
that misses first reads the whole 8-byte line from the PSRAM and writes it back later. An
uncached write goes straight to the PSRAM, and writes that follow on in address are
chained into one QSPI transfer. The 300 KB drawn and copied each frame also stay out of the
16 KB cache.

On every frame, `dx_display_present` waits for the vertical blank (mixing audio meanwhile)
and copies the back buffer into the front buffer in SRAM. The copy runs top to bottom
faster than the beam, so it does not tear. It takes about 6.2 ms, limited by the PSRAM
reads (a copy loop that loads 8 words at a time measured the same). The status line reports
its duration.

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
  - **The full game:** uses PCM WAV, played as it is.

## Input

- **Sampling:** the USB host runs on Pico-PIO-USB from a 4 ms timer interrupt. Changes of
  keys, mouse buttons and pad buttons are recorded in a ring (`src/pico/dx_usb.cpp`) and
  turned into SDL events in thread context (`dx_input.cpp`).
- **Gamepads:** USB pads and the NES/SNES ports are merged into SDL joystick 0. The engine
  is told the button numbers with the `JOY_*` definitions in `cmake/dx_engine.cmake`.
- **Wii pad (Fruit Jam):** read in thread context every 10 ms (`dx_wiipad.cpp`, after
  pico-duke3D's), because it shares the I2C bus with the TLV320 codec. It is brought up
  before the codec.

## The status line

Every 5 seconds the firmware writes two lines to the UART:

```
status: 12.0 fps, heap 844/7522 KB (peak 1009), SRAM 40 KB free, stack 6120/16384 B core0 900/4096 B core1, frame copy 5.8 ms (max 7.1), cache 294 assets 7571 KB, 290 hits 4 stored, 40 KB written
time per frame (ms): other 2.1 logic 5.0 view 40.2 draw 6.0 copy 6.2 vsync 8.0 audio 9.1 sd 1.0 input 0.3 flash 0.0, XIP cache 92% hits of 410 M accesses
```

(The values above are an illustration of the format, not measurements.)

- **First line:**
  - the frame rate;
  - the PSRAM heap (`dx_heap`) now and at its peak;
  - the SRAM left for `malloc`;
  - the deepest use of both stacks;
  - how long the frame copy took (with the copy on core1, from the vertical blank; it
    must stay below about 15 ms to keep ahead of the beam);
  - the flash asset cache counters.
- **Second line:**
  - the time of an average frame per zone (`src/port/dx_prof.h`). The zones nest, so each
    millisecond is counted once:
    - `logic`: the engine's game tick (`game_loop`);
    - `view`: the dungeon or town (`DrawView`);
    - `draw`: the rest of `DrawAndBlit`;
    - `vsync` and `copy`: waiting for the vertical blank and copying the frame (with
      the copy on core1, `vsync` is the wait for it);
    - `audio`, `sd`, `input`, `flash`: the platform layer;
    - `other`: everything else (menus, level loading);
  - the hit rate of the 16 KB XIP cache that flash code, flash data and PSRAM share.
- **Hot code:** core0's program counter, sampled 2000 times a second (TIMER0 alarm 0), as
  the 40 busiest 16-byte blocks of code with their sample counts. The addresses only apply
  to the ELF file of the same build:
  `arm-none-eabi-addr2line -f -C -e pico_lutionx.elf 0x1004a3c0`.

## The test program

`hosttest/build.sh` builds `hosttest/dx_host` (with AddressSanitizer and UBSan) and
`NOSAN=1` builds `hosttest/dx_host_nosan`. It runs the engine with:

- a heap the size of the board's free PSRAM;
- the flash asset cache on emulated NOR flash (`DX_FCACHE=1`), mapped read-only outside
  writes so that any write into cached data faults;
- PPM frame dumps (`DX_DUMP_EVERY`, `DX_DUMP_DIR`);
- audio into a WAV file (`DX_AUDIO_WAV`);
- scripted input (`DX_SCRIPT`).

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
```

Other tools:

- `tools/arm_size.sh` links the engine for the RP2350 against a stub platform (code size,
  static data, stack frames).
- `tools/mpqsizes.py` lists what an MPQ contains.
- `tools/peakheap.c` measures the heap of a stock DevilutionX build.

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
- **The full game:** `DIABDAT.MPQ` is untested. Its level types 5–16 and larger monster
  sets have only been estimated (docs/FEASIBILITY.md). Its sound effects are PCM WAV,
  which takes more flash than the shareware MP3s.
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
