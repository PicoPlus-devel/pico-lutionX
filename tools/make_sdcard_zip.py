#!/usr/bin/env python3
"""Builds the SD-card archive of pico-lutionX: the DevilutionX assets the board needs.

    python3 -I tools/make_sdcard_zip.py <output.zip>

The archive unpacks to roms/devilutionx/ on the card. It holds DevilutionX's own assets
(the loose form of devilutionx.mpq; fonts, UI and level data) for a 640x480 Diablo game
without network play, with their licences. It contains no Blizzard game data: spawn.mpq
or DIABDAT.MPQ is added to the same folder by the user (or by updateAll).

Left out: widescreen art (*w.clx), the multiplayer arenas, the Hellfire levels and the
PNG images of the SDL2 touch controls.
"""
import os
import sys
import zipfile

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..')
DX = os.path.join(ROOT, '3rdparty', 'devilutionX')
ASSETS = os.path.join(DX, 'assets')
DEST = 'roms/devilutionx/'
STAMP = (2025, 10, 30, 0, 0, 0)  # the DevilutionX release date, for reproducible archives

README = """pico-lutionX game data folder
==============================

Copy one of these files into this folder (roms/devilutionx on the SD card):

  spawn.mpq     the shareware data of Diablo
  DIABDAT.MPQ   the data of the full game (from the CD or a digital copy)

The assets folder holds DevilutionX's own assets (fonts, interface and level data).
Their licences are in the licenses folder and in LICENSE-DevilutionX.md.

Saved games and diablo.ini are written to this folder.
"""


def wanted(rel):
    parts = rel.split('/')
    top = parts[0]
    name = parts[-1]
    if top in ('arena', 'nlevels'):
        return False
    if name.endswith('.png'):
        return False
    if name.endswith('w.clx') and top in ('gendata', 'ui_art'):
        return False
    return True


def add(zf, arcname, data):
    info = zipfile.ZipInfo(arcname, STAMP)
    info.compress_type = zipfile.ZIP_DEFLATED
    info.external_attr = 0o644 << 16
    zf.writestr(info, data)


def main():
    if len(sys.argv) != 2:
        raise SystemExit(__doc__)
    files = []
    for dirpath, _dirs, names in os.walk(ASSETS):
        for name in names:
            rel = os.path.relpath(os.path.join(dirpath, name), ASSETS).replace(os.sep, '/')
            if wanted(rel):
                files.append(rel)
    total = 0
    with zipfile.ZipFile(sys.argv[1], 'w') as zf:
        for rel in sorted(files):
            with open(os.path.join(ASSETS, rel), 'rb') as f:
                data = f.read()
            total += len(data)
            add(zf, DEST + 'assets/' + rel, data)
        with open(os.path.join(DX, 'LICENSE.md'), 'rb') as f:
            add(zf, DEST + 'LICENSE-DevilutionX.md', f.read())
        for name in sorted(os.listdir(os.path.join(DX, 'licenses'))):
            with open(os.path.join(DX, 'licenses', name), 'rb') as f:
                add(zf, DEST + 'licenses/' + name, f.read())
        add(zf, DEST + 'README.txt', README.encode())
    print(f'{sys.argv[1]}: {len(files)} asset files, {total // 1024} KB')


if __name__ == '__main__':
    main()
