# Libraries bundled with DevilutionX 1.5.5

Copied from the DevilutionX 1.5.5 fully vendored source tarball (see
`../devilutionX/UPSTREAM.md`); zlib from the archive that DevilutionX's build downloads.
Only the files the port compiles are kept.

| Directory | Library | Version / commit | Licence | Use |
|---|---|---|---|---|
| `fmt/` | {fmt} | 10.0.0 | MIT | string formatting (`format.cc`) |
| `libmpq/` | libmpq (diasurgical fork) | b78d66c | **GPL-2.0-or-later** | MPQ reading |
| `libsmackerdec/` | libsmackerdec (diasurgical fork) | 91e732b | LGPL-2.1-or-later | Smacker videos |
| `PKWare/` | PKWARE DCL implode/explode from StormLib (Ladislav Zezula) | as in DevilutionX 3rdParty | MIT (StormLib) | MPQ sectors, saves |
| `simpleini/` | SimpleIni (header only, `SI_NO_CONVERSION`) | 56499b5 | MIT | diablo.ini |
| `tl/` | tl::expected, tl::function_ref | as in DevilutionX 3rdParty | CC0 | |
| `hoehrmann_utf8/` | UTF-8 decoder (Taylor R Campbell / Bjoern Hoehrmann) | as in DevilutionX 3rdParty | BSD-2-Clause / MIT | |
| `zlib/` | zlib, inflate only | 1.2.13 | zlib | DevilutionX's spawn.mpq is zlib-compressed |
| `dr_libs/` | dr_mp3 (David Reid, based on minimp3) | 0.6.34 | public domain / MIT-0 | MP3 sounds and music of spawn.mpq (`src/aulib/aulib_mp3.cpp`); not from the DevilutionX tarball |

Local additions: `libmpq/config.h` from DevilutionX's `3rdParty/libmpq`. bzip2 is not
included; `src/port/stubs/bzlib.h` makes libmpq report bzip2 blocks as unreadable (game
MPQs do not use bzip2).

Local change: `dr_mp3.h` (`drmp3dec_decode_frame`) takes the storage class of its 17 KB
scratch space from `DRMP3_SCRATCH_STORAGE` when that is defined, marked `pico-lutionX`.
`src/aulib/aulib_mp3.cpp` makes it static (in SRAM on the board), because core0's stack
is 16 KB.

libmpq's GPL is not compatible with the Sustainable Use License of DevilutionX for
distribution; see `docs/FEASIBILITY.md` (licences) for the replacement plan.
