# DevilutionX 1.5.5 (vendored)

Source: <https://github.com/diasurgical/devilutionX>, release 1.5.5 (2025-10-30), from
`devilutionx-src-fully-vendored.tar.xz`
(sha256 `f42379cdf098a351a2020d9e6de1840c76e09113c792f73c6df34dec26818b04`).

Copied from the tarball:

| Here | Tarball |
|---|---|
| `Source/` | `Source/` (CRLF line endings kept) |
| `assets/` | `Packaging/resources/assets/` (the loose form of `devilutionx.mpq`) |
| `licenses/` | `Packaging/resources/LICENSE.*.txt` (licences of the assets) |
| `LICENSE.md`, `VERSION` | same |

The timedemo fixture `test/fixtures/timedemo/WarriorLevel1to2` is in `hosttest/fixtures/`.
The bundled libraries are in `../dx-deps` (see its UPSTREAM.md).

DevilutionX is licensed under the Sustainable Use License 1.0 (`LICENSE.md`): it may be
distributed free of charge for non-commercial purposes, the licence must accompany every
copy, and modified copies must say that they were modified.

## Local changes

Every change is marked with a `pico-lutionX` comment (`grep -rn pico-lutionX Source`).
`tools/dxpatch.py` applies changes without touching the CRLF line endings.

Port changes:

- `storm/storm_net.cpp`: `dwLastError` is a plain variable instead of `thread_local` when
  built with `NONET` (bare-metal targets have no thread-local storage).
- `pack.cpp` (`UnPackPlayer`), `player.cpp` (`CreatePlayer`): reset the player in place
  instead of `player = {}`, which put a 16 KiB temporary on the stack.
- `controls/plrctrls.cpp` (`FindMeleeTarget`): the 12.5 KiB `visited` map is static instead
  of a local array.
- `utils/cl2_to_clx.hpp`: reserve the conversion buffers up front, avoiding vector growth
  peaks during level loading.

- `engine/render/blit_impl.hpp`: the per-pixel colour map loops (lighting, blended
  transparency, colour translations) call the word-at-a-time versions in
  `src/port/dx_blit.c`, which run from SRAM on the board; `lighting.cpp`: the light tables
  are in SRAM (`DX_SRAM_BSS`, `src/port/dx_sram.h`).
- `options.cpp`: Zoom is on by default when `DEVILUTIONX_PICO_ZOOM_DEFAULT` is defined (the
  board's engine library, `cmake/dx_engine.cmake`).
- `engine/render/scrollrt.cpp` (`Zoom`): doubles the view row by row through SRAM, in
  ascending order, instead of byte by byte backwards; with the uncached back buffer the old
  order made every pixel a QSPI transfer of its own (even sizes; odd ones keep the old code).
- `engine/dx.cpp` (`GlobalBackBuffer`), `DiabloUI/diabloui.h` (`DiabloUiSurface`): wait
  with `SDLDX_WaitScreen()` (an SDL subset extension) until the board has copied the last
  frame, which it does in the background on core1.
- `utils/sdl2_to_1_2_backports.cpp`: `SDL_GetBasePath`/`SDL_GetPrefPath` return the SD
  card's game data folder when `DEVILUTIONX_PICO_DATA_DIR` is defined (the board).
- `utils/soundsample.cpp`: streamed sounds share the MPQ archive instead of opening a
  second copy (`DEVILUTIONX_PICO_SINGLE_THREADED`).

Flash asset cache (`src/port/dx_fcache.hpp`):

- `engine/load_cel.cpp`, `engine/load_cl2.cpp`, `engine/load_cl2.hpp`
  (`LoadMultipleCl2Sheet`), `engine/load_clx.cpp`, `engine/load_pcx.cpp`: look the
  converted sprites up in the cache first; store them after loading (CL2 straight from
  the conversion buffer).
- `monster.cpp` (`InitMonsterGFX`, `LoadMonsterAnims`): a monster set is loaded as one
  buffer per animation, each converted and colour translated by itself, in PSRAM or (when
  PSRAM runs short) in the cache as `<key>:a<i>`; the monster type owns them through a
  reference block (`dx_heap_refblock`). Upstream's one-buffer build peaks at about twice the
  set, which the full game's deep levels cannot provide. The upstream code is kept for other
  builds.
- `player.cpp` (`LoadPlrGFX`): sprites with a class colour translation are cached with the
  translation applied, under their own key.
- `diablo.cpp`: dungeon tiles (`LoadLvlGFX`) go through `DxLoadFileCached`, which copies
  the file from the MPQ straight into flash; `LoadGameLevel` starts a new cache sequence
  and commits the directory at its end.
- `engine/sound.cpp` (`LoadAudioFile`): sound effects are kept in the cache and played
  from there, converted into IMA ADPCM WAV files on the way in (MP3 in `spawn.mpq`, 8-bit
  PCM WAV in `DIABDAT.MPQ`).

Upstream bug:

- `engine/render/primitive_render.cpp`: without `DEVILUTIONX_PALETTE_TRANSPARENCY_BLACK_16_LUT`,
  `DrawHalfTransparentBlendedRectTo` did not compile (5 arguments mapped onto the
  6-argument function); it is now a small wrapper that passes colour 0.

Time per frame (`src/port/dx_prof.h`): `DxProfZone` in `diablo.cpp` (`game_loop`) and
`engine/render/scrollrt.cpp` (`DrawAndBlit`, and `DrawView` inside it).

Measurement hooks (heap accounting, see `src/port/dx_heap.h`):

- `DxHeapScope` category scopes in the loaders: `diablo.cpp` (`LoadLvlGFX`), `monster.cpp`
  (`InitMonsterGFX`), `player.cpp` (`LoadPlrGFX`, `InitPlayerGFX`), `misdat.cpp`
  (`MissileFileData::LoadGFX`), `objects.cpp` (`InitObjectGFX`), `items.cpp`
  (`InitItemGFX`), `towners.cpp` (`InitTowners`), `cursor.cpp`, `inv.cpp`, `control.cpp`,
  `panels/spell_icons.cpp`, `engine/render/text_render.cpp` (`LoadFont`),
  `mpq/mpq_reader.cpp` (`MpqArchive::Open`), `engine/demomode.cpp` (`InitPlayBack`),
  `engine/sound.cpp` (`LoadAudioFile`).
- `DxCacheableScope` around the final sprite buffers (the candidates for the flash asset
  cache): `utils/cel_to_clx.cpp`, `utils/cl2_to_clx.hpp`, `utils/pcx_to_clx.cpp`,
  `utils/surface_to_clx.cpp`, `engine/load_clx.cpp`, and all of `LoadLvlGFX`.
- `dx_heap_mark()` reports in `diablo.cpp`: before and after the main menu is freed, and at
  the end of `LoadGameLevel`.
