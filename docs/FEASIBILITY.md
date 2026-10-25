# pico-lutionX: feasibility of DevilutionX on the RP2350

Status of this document: result of the feasibility gate, 2026-10-24. The port has been
implemented since; [TECHNICAL.md](../TECHNICAL.md) describes its current state. Changes
from this report: the application slot is 2.5 MB and the flash cache 13 MB (the firmware
with sound is about 2 MB); sound effects and dungeon tiles are streamed into the flash
cache; the `spawn.mpq` that DevilutionX distributes stores its sounds as MP3.

## 1. Summary

The question was whether DevilutionX, the open-source Diablo engine, can be ported to the
RP2350 boards of the pico native-port family. The target boards have 8 MB PSRAM and 16 MB
flash: HW_CONFIG 2 (Pimoroni Pico Plus 2 on the Adafruit DVI and microSD breakouts or the
PicoNES PCB) and HW_CONFIG 8 (Adafruit Fruit Jam). The port would need keyboard, mouse and
gamepad input, would run the shareware and the retail game data, and would be released
both standalone and as a pico-bootLoader application.

**Recommendation: conditional go.**

- **What fits.** The engine compiles and links for the RP2350 at 1.25 MB of code (`-Os`).
  Its static data is 0.51 MiB. With the unmodified shareware data, it runs headless and
  rendered through the new SDL 1.2 subset, in a heap of exactly the PSRAM that the board has left.
  It replays the reference timedemo with an identical outcome, clean under AddressSanitizer
  and UndefinedBehaviorSanitizer.
- **What does not fit.** Without further measures a dungeon level needs 8.2–8.5 MiB of
  heap. Only 7.5 MiB of PSRAM is left after the static data. About 7.3–7.8 MiB of that
  heap is read-only sprite data.
- **The condition for memory.** The port needs a flash asset cache, as pico-quake2 already
  has. Sprite data is written once to flash and used in place. PSRAM then holds only
  0.6–0.9 MiB after loading, with a peak of 1.9 MiB while loading.

The conditions for continuing:

1. **Flash asset cache.** A flash asset cache must be built: pico-quake2's design, adapted
   to the engine's sprite buffers. Without it the port does not fit.
2. **Licences.** Two GPL components that the port would link must be replaced or
   relicensed (section 7):
   - `pico_shared` (GPL-3.0), from which the board drivers come;
   - libmpq (GPL-2.0-or-later).
   DevilutionX's own licence does not permit them in the same binary.
3. **Rendering speed.** Rendering speed on the board is unmeasured. It is the main
   technical risk and the first thing to measure after a go (section 8).

## 2. What was done

| Step | Result |
|---|---|
| Stock DevilutionX 1.5.5 built for Linux (SDL 1.2, `NONET`, `NOSOUND`), heap measured with an LD_PRELOAD tracker (`tools/peakheap.c`) | First numbers (64-bit): 8.6 MiB live at dungeon level 2, 8 MiB of it sprite data |
| Engine vendored into `3rdparty/devilutionX` with its libraries (`3rdparty/dx-deps`) | 23 files patched, all marked `pico-lutionX` (`3rdparty/devilutionX/UPSTREAM.md`) |
| SDL 1.2 subset written from scratch (`src/sdl/`), covering the 55 SDL functions the engine references | The engine's SDL1 code paths compile unchanged |
| Engine heap (`src/port/dx_heap.cpp`): TLSF pool the size of the free PSRAM, per-category accounting, simulated flash cache | Out-of-memory on the host means out-of-memory on the board |
| Host harness (`hosttest/`): 32-bit build like the Cortex-M33, sanitizers, PPM frame dumps, scripted input, headless timedemo test | Timedemo `Same`, sanitizers clean |
| Per-level sweep (`hosttest/sweep.sh`) over all levels in spawn.mpq | Section 3 |
| Full RP2350 link with pico-sdk 2.2.0 (`tools/arm_size.sh`, `tools/sizeprobe/`) | Section 4 |
| MPQ analysis (`tools/mpqsizes.py`) | Section 5 |

The commands to repeat every measurement are in section 10.

## 3. Memory

All figures are from the host harness: 32-bit, TLSF heap, 8-byte block header included,
measured after the level has loaded. The demo playback buffer is left out because normal
play does not have it. Data: spawn.mpq (town and cathedral levels 1–4).

### 3.1 Heap without a flash cache

| Level | Heap after loading | Board budget (8 MiB − 0.51 MiB static) |
|---|---|---|
| 0, town | 6.30 MiB | 7.49 MiB |
| 1 | 8.16 MiB (8.37 MiB peak while loading) | 7.49 MiB |
| 2 | 8.27 MiB | 7.49 MiB |
| 3 | 8.16 MiB | 7.49 MiB |
| 4 | 8.46 MiB | 7.49 MiB |

With the board's budget enforced, the harness runs out of memory while loading the
monster sprites of dungeon level 1.

### 3.2 Where the heap goes (dungeon level 4; town in brackets)

| Category | Size | Read-only after loading |
|---|---|---|
| Monster sprites | 3.63 MiB (–) | yes, after the colour translation is applied |
| Player sprites (one class, armour and weapon) | 1.50 MiB (1.43) | yes |
| Missile sprites (all player spells) | 0.96 MiB (0.96) | yes |
| UI: panels, cursors, fonts, spell icons | 0.82 MiB (0.82) | 0.63 MiB of it |
| Dungeon tiles (CEL, TIL) | 0.73 MiB (2.12) | yes |
| Objects and item drop animations | 0.34 MiB (0.15) | yes |
| Town NPCs | – (0.28) | yes |
| Screen back buffer (640×480, 8 bpp) | 0.29 MiB | no |
| Everything else (MPQ tables, game state on the heap, buffers) | 0.18 MiB | no |

### 3.3 Heap with the flash asset cache (simulated)

In this run all final sprite buffers come from a separate pool that stands in for flash,
and the PSRAM pool is capped at the board's budget. The run completes, and the timedemo
outcome is still `Same`.

| Level | PSRAM after loading | PSRAM peak while loading | In the flash cache |
|---|---|---|---|
| 0, town | 0.64 MiB | 1.79 MiB | 5.66 MiB |
| 1 | 0.85 MiB | 1.86 MiB | 7.31 MiB |
| 2–4 | 0.64 MiB | 1.86 MiB | 7.52–7.82 MiB |

Including the 0.51 MiB of static data, PSRAM use stays under 2.4 MiB of 8 MiB. That leaves
room to choose per category what goes to flash:

- **Option A, all sprite data in flash.** Least PSRAM. Each new set of monsters is written
  to flash on its first visit (section 6).
- **Option B, monster sprites in PSRAM, everything else in flash.**
  - *PSRAM:* about 6.0 MiB peak for spawn, including static data and the 3.6 MiB of
    monster sprites.
  - *Flash writes:* only for tilesets, player sets and the UI, which change rarely.
  - *Retail levels:* their monster budget allows up to about 4–5 MiB of monster sprites,
    so option B needs a fallback to flash when they do not fit.

Option B is recommended, with the fallback.

### 3.4 Retail data (estimated, not measured)

The engine limits the random monsters of a level with the original budget
`monstimgtot < 4000`. `tools/mpqsizes.py` shows that one budget unit corresponds to roughly
0.8–1.1 KiB of sprite data. That puts random monsters at about 3.5–4.4 MiB per level, plus
quest and unique monsters.

The dungeon tilesets of levels 5–16 are not in spawn.mpq. In the original game they are of
the same order as the cathedral's, which is 0.7 MiB.

With the flash cache, the resident PSRAM use does not depend on these sizes. Only the
amount written to flash per level does. To measure them, run `hosttest/sweep.sh` with a
DIABDAT.MPQ for levels 0–16.

## 4. Code, static data and stack (RP2350, pico-sdk 2.2.0, arm-none-eabi-gcc 13.2.1)

The full link combines the engine, its libraries, the SDL subset, the heap and pico_stdlib,
against a stub platform.

| Build | Flash (code and read-only data) | Static data (`.data` + `.bss`) |
|---|---|---|
| `-Os` | 1,252,784 bytes | 530,148 bytes |
| `-O2` | 1,521,160 bytes | 530,264 bytes |
| `-O2` with LTO | does not link (pico-sdk's `--wrap` scheme and LTO conflict); not needed | |

Further findings from the link:

- **Clean link.** It confirms that newlib and libstdc++ work with the port's settings: no
  exceptions, no RTTI, no `std::filesystem`, no iostream. The only missing functions are
  `mkdir`, `readlink` and `truncate`, which the FatFs layer will provide.
- **Largest static symbols.** The transparency table (64 KiB), the tile micro table
  (43 KiB), the item array (35 KiB) and the dungeon maps (12–25 KiB each).
- **Stack frames.** Three functions had frames of 12.6–16.5 KiB and were patched:
  `UnPackPlayer`, `CreatePlayer` and `FindMeleeTarget`. The only frame above 8 KiB left is
  `recv_plrinfo`, which only multiplayer reaches. A 16 KiB main stack is expected to be
  enough; it must be confirmed on the board.

## 5. Game data

- **spawn.mpq** (the copy used for the measurements, 25,448,219 bytes, the same size as
  the one diasurgical distributes):
  - 1030 files, 24.2 MiB compressed, 39.1 MiB uncompressed.
  - Its sectors are **zlib**-compressed, unlike the original Diablo archives, which use
    PKWARE implode. The port therefore needs zlib inflate (vendored) and PKWARE explode
    (for DIABDAT.MPQ).
  - The MPQ tables take 48 KiB of RAM.
- **DevilutionX assets.**
  - The prebuilt `devilutionx.mpq` is bzip2-compressed. bzip2 decompression needs several
    MB of work memory, so the port reads the loose `assets/` folder from the SD card
    instead.
  - The full folder is 6.0 MB: fonts 2.3 MB, Hellfire-only `nlevels` 0.5 MB, widescreen
    cutscene art 1.9 MB.
  - A 640×480, Diablo-only subset is estimated at about 2–2.5 MB (not yet assembled).
- **SD layout (proposed).** The SD folder name is still your decision.
  - `/roms/devilutionx/`: `spawn.mpq` or `DIABDAT.MPQ`, plus `assets/`.
  - Saves and `diablo.ini` in the same folder.
  - The saves are standard `.sv` files, interchangeable with DevilutionX 1.5.5 on a PC.

## 6. Flash

| Region | Size | Note |
|---|---|---|
| pico-bootLoader (bootloader builds only) | 512 KiB | `FRENS_APP_BASE = 0x10080000` |
| Application | 2 MiB slot | measured 1.25–1.52 MB |
| Settings | 4 KiB | |
| Flash asset cache | about 13.5 MiB | same absolute address in standalone and bootloader builds |

- **Working set.** For the town plus one dungeon type, option A needs about 10–11 MiB (town
  5.7 MiB plus about 5 MiB of level-specific data; the missile and UI entries are shared).
  Option B needs about 7 MiB. Either way the cache stays warm across the usual
  town-to-dungeon trips.
- **Write cost.** Taking pico-quake2's figures (64 KiB erase about 150 ms, page program
  about 0.4 ms), writing costs about 3.4 s per MiB:
  - option A: about 12 s on the first visit of a level with 3.6 MiB of new monster sprites;
  - option B: about 2–5 s, only for new tilesets and player sets.
- **Persistence.** The cache survives power cycles. Its directory format and CRC checking
  can follow pico-quake2 (`qcommon/fcache.c`).

## 7. Licences

This is an assessment, not legal advice.

| Component | Licence | Compatible with DevilutionX's licence in one firmware? |
|---|---|---|
| DevilutionX 1.5.5 | Sustainable Use License 1.0: free distribution only, non-commercial, licence must accompany copies, modifications must be marked | – |
| `pico_shared` (source of the vendored drivers: usb_hid, nespad, wiipad, HDMI, PSRAM, audio) | **GPL-3.0** (repository licence) | **No** |
| libmpq | **GPL-2.0-or-later** | **No** (upstream DevilutionX has since replaced it with the MIT-licensed mpqfs) |
| libsmackerdec | LGPL-2.1-or-later | Yes, if the source of the whole firmware is published; avoidable by leaving out cutscenes |
| zlib, {fmt}, SimpleIni, PKWARE (StormLib), tl, hoehrmann_utf8, TLSF, pico-sdk, TinyUSB, FatFs, pico_hdmi | zlib, MIT, MIT, MIT, CC0, BSD/MIT, BSD-3, BSD-3, MIT, BSD-1, Unlicense | Yes |
| SDL 1.2 subset (`src/sdl/`) | written for this project | Yes |

The GPL does not allow further restrictions, and the Sustainable Use License restricts
commercial use. A binary that contains both therefore cannot be distributed under either
licence.

The options for the drivers:

1. As the author of `pico_shared`, license the needed parts under a permissive licence
   (MIT/BSD) for this project. This is possible only for code you wrote yourself; parts
   derived from others keep their original licences.
2. Take the drivers from their permissive origins instead: pico_hdmi (Unlicense), TinyUSB
   (MIT), the CircuitPython PSRAM setup (MIT), FatFs (BSD), and write the HID and pad
   handling anew.

For libmpq, use mpqfs (MIT) or a small MPQ reader of our own. The read-only Diablo format
needs hash and block tables, decryption, PKWARE explode and zlib inflate.

The game data is not part of the port. spawn.mpq would be downloaded on the user's
computer by updateAll, as the Quake II demo is. DIABDAT.MPQ is supplied by the user.

## 8. Risks

| Risk | Detection | Mitigation |
|---|---|---|
| **Rendering speed.** The back buffer is in PSRAM and the sprites in flash, and both share the 16 KB XIP cache with the code. | First board task: copy speed PSRAM→SRAM, PSRAM read/write speed, then the timedemo on the board with fps over the serial port | Hot render loops in SRAM; the engine's Zoom option (half-resolution dungeon view); render fewer frames while the logic stays at 20 Hz; 504 MHz option |
| SRAM: the 300 KB front buffer leaves about 200 KB | Link map of the platform layer with the front buffer, before the engine is added | Smaller HDMI audio queue and audio ring; limit the hot code |
| Flash write time on first visits | Load timings per level, cold and warm cache | Option B (section 3.3); cache persistence |
| Retail levels (unmeasured) | `hosttest/sweep.sh` with DIABDAT.MPQ, levels 0–16 | Flash cache; monster sprites fall back to flash |
| Licences | – | Section 7, decision required |
| Gamepad layout: DevilutionX expects about 12 buttons and two sticks, the SNES pad has 8 buttons and the NES pad 4 | Design the mapping before implementing it | Select as a modifier, the automap and menus by key combination; USB pads with full layouts |
| Saves are written through FatFs to the MPQ format | Save and load round trip on the board and with DevilutionX on a PC | `UNPACKED_SAVES` as a fallback |
| Sound: the engine's audio library (SDL_audiolib, LGPL) is replaced | Host harness first, then the board | Small mixer on core 0, output on core 1, as in pico-quake2 |
| Cutscenes: decoding Smacker video at 640×480 | Board measurement | Leave cutscenes out at first |

## 9. Target architecture and phases after a go

**Architecture**

1. **Engine.** DevilutionX 1.5.5 as vendored, built with `USE_SDL1`, `NONET`, an 8 bpp
   screen with `SDL_DOUBLEBUF` (the engine then renders directly into the screen surface),
   and the black-16 transparency table off.
2. **SDL layer.** The SDL 1.2 subset over a small backend interface (`src/sdl/sdl_backend.h`):
   one backend for the host harness, one for the board.
3. **Video.**
   - Front buffer: 640×480×8 bpp in SRAM, scanned out over HSTX by a custom scanline
     callback on core 1 with an RGB555 lookup table.
   - Back buffer: in PSRAM. `SDL_Flip` copies it at vblank, staying ahead of the beam.
   - Palette changes take effect at vsync.
4. **Memory.**
   - Engine static data and the TLSF heap in PSRAM, set up before the C++ static
     constructors run.
   - Sprite data in the flash asset cache according to option B.
5. **Data.** MPQ read from the SD card with fast seek, plus the assets subset; standard
   saves.
6. **Input.**
   - USB keyboard: reports compared frame to frame into key events.
   - USB mouse: movement integrated into an absolute cursor.
   - USB, NES/SNES and Wii pads: merged into one joystick.
   - HW_CONFIG 2 uses PIO-USB on GPIO 20/21, as pico-quake2 does.
7. **Audio.** Mixer on core 0, resampling and output on core 1: HDMI, TLV320 or PCM5100A.

**Phases**

| Phase | Content | Exit criterion |
|---|---|---|
| 2 | Platform on the board: boot, clocks, PSRAM, FatFs, USB, display (640×480 8 bpp), taken from pico-quake2 where possible; licence decision implemented | PSRAM→SRAM copy of a frame ≤ 10 ms, ≥ 40 KB SRAM free |
| 3 | Engine on the board | Title → town → level 1 → save → load; timedemo `Same` on the board, fps measured |
| 4 | Flash asset cache (reference-counted release of cached buffers; on the host the cache is read-only, so writes into cached data fault) | Sweep passes; load times measured |
| 5 | Sound | Music and effects without dropouts |
| 6 | Input: keyboard, mouse, pads | A quest completed with a pad only, and with keyboard and mouse only |
| 7 | Saves and retail data | `.sv` round trip with the PC; DIABDAT.MPQ sweep |
| 8 | Performance | Timedemo ≥ 20 fps |
| 9 | Release | `buildAll.sh` and CI for both boards, standalone and bootloader UF2; README, CHANGELOG; pico-bootLoader entries (`emulators.txt`, `ports.txt`, artwork, `build_emulators.sh`, `updateAll.json`) |

## 10. Reproducing the measurements

```sh
# host harness (32-bit; with sanitizers, and without for measurements)
hosttest/build.sh
NOSAN=1 hosttest/build.sh

# data directory: spawn.mpq plus the DevilutionX assets
mkdir -p /tmp/dx/data
ln -s /path/to/spawn.mpq /tmp/dx/data/
ln -s "$PWD/3rdparty/devilutionX/assets" /tmp/dx/data/assets

# headless timedemo, as upstream's test/timedemo_test.cpp (prints TIMEDEMO: Same)
cp -r hosttest/fixtures/WarriorLevel1to2 /tmp/dx/fixture
hosttest/dx_host --timedemo-test /tmp/dx/fixture /tmp/dx/data

# rendered timedemo on the board's PSRAM budget with the flash cache simulated
cp -r hosttest/fixtures/WarriorLevel1to2 /tmp/dx/save
DX_FLASH_SIM=1 DX_DUMP_EVERY=800 DX_DUMP_DIR=/tmp/dx hosttest/dx_host_nosan \
    --data-dir /tmp/dx/data --save-dir /tmp/dx/save --config-dir /tmp/dx/save \
    --demo 0 --timedemo -n --spawn

# heap per level (sections 3.1-3.3)
hosttest/sweep.sh /tmp/dx/data

# RP2350 code size, static data and stack frames (section 4)
tools/arm_size.sh

# MPQ facts and asset sizes (section 5)
python3 -I tools/mpqsizes.py /path/to/spawn.mpq
```

Screenshots from the host harness (rendered through the SDL subset on the board's memory
configuration) are in `docs/screenshots/`.
