#!/usr/bin/env python3
"""Asset sizes in a Diablo MPQ (spawn.mpq or DIABDAT.MPQ), read from the MPQ tables.

    python3 -I tools/mpqsizes.py <archive.mpq> [Source-dir]

Reports, without decompressing anything:
  - archive facts: file count, compressed/uncompressed totals, table sizes (RAM needed
    by the MPQ reader), compression methods used (PKWARE implode, zlib, ...);
  - dungeon tilesets (CEL/TIL/MIN per level type);
  - monster animation sets (all CL2 files of a monster) with the original image budget
    value (`image`, summed against monstimgtot < 4000 when a level is populated);
  - player animation sets per class, armour and weapon.
Names of monsters come from the engine's monstdat.cpp (default:
3rdparty/devilutionX/Source next to this tool).
"""
import collections
import os
import re
import struct
import sys

# ---- MPQ tables --------------------------------------------------------------------------

CRYPT = [0] * 0x500
_seed = 0x00100001
for _i in range(256):
    _idx = _i
    for _j in range(5):
        _seed = (_seed * 125 + 3) % 0x2AAAAB
        _t1 = (_seed & 0xFFFF) << 16
        _seed = (_seed * 125 + 3) % 0x2AAAAB
        _t2 = _seed & 0xFFFF
        CRYPT[_idx] = _t1 | _t2
        _idx += 256


def hash_string(s, kind):
    s1, s2 = 0x7FED7FED, 0xEEEEEEEE
    for c in s.upper().encode():
        s1 = (CRYPT[kind * 256 + c] ^ (s1 + s2)) & 0xFFFFFFFF
        s2 = (c + s1 + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return s1


def decrypt(data, key):
    out, s2 = [], 0xEEEEEEEE
    for i in range(0, len(data) // 4 * 4, 4):
        s2 = (s2 + CRYPT[0x400 + (key & 0xFF)]) & 0xFFFFFFFF
        c = struct.unpack_from('<I', data, i)[0] ^ ((key + s2) & 0xFFFFFFFF)
        out.append(c)
        key = ((((~key) << 0x15) + 0x11111111) | (key >> 0x0B)) & 0xFFFFFFFF
        s2 = (c + s2 + (s2 << 5) + 3) & 0xFFFFFFFF
    return out


class Mpq:
    def __init__(self, path):
        with open(path, 'rb') as f:
            self.data = f.read()
        d = self.data
        self.base = d.find(b'MPQ\x1a')
        if self.base < 0:
            raise SystemExit(f'{path}: not an MPQ archive')
        (_hsize, _asize, self.version, shift, hto, bto, self.hash_entries,
         self.block_entries) = struct.unpack_from('<IIHHIIII', d, self.base + 4)
        self.sector = 512 << shift
        ht = decrypt(d[self.base + hto:self.base + hto + self.hash_entries * 16], hash_string('(hash table)', 3))
        bt = decrypt(d[self.base + bto:self.base + bto + self.block_entries * 16], hash_string('(block table)', 3))
        self.hashes = [tuple(ht[i * 4:i * 4 + 4]) for i in range(self.hash_entries)]
        self.blocks = [tuple(bt[i * 4:i * 4 + 4]) for i in range(self.block_entries)]

    def find(self, name):
        a, b, c = hash_string(name, 0), hash_string(name, 1), hash_string(name, 2)
        start = a % self.hash_entries
        for k in range(self.hash_entries):
            e = self.hashes[(start + k) % self.hash_entries]
            if e[3] == 0xFFFFFFFF:
                return None
            if e[0] == b and e[1] == c and e[3] < self.block_entries:
                return self.blocks[e[3]]
        return None

    def size(self, name):
        block = self.find(name)
        return block[2] if block else None

    def compression_methods(self):
        """Histogram of how files are compressed (first sector of each file)."""
        names = {0x02: 'zlib', 0x08: 'PKWARE implode', 0x10: 'bzip2', 0x01: 'huffman',
                 0x40: 'ADPCM mono', 0x80: 'ADPCM stereo'}
        hist = collections.Counter()
        for off, csize, usize, flags in self.blocks:
            if not flags & 0x80000000:
                continue
            if flags & 0x100:
                hist['PKWARE implode (whole file flag)'] += 1
            elif flags & 0x200:
                if flags & 0x10000:
                    hist['compressed, encrypted (not inspected)'] += 1
                    continue
                n = (usize + self.sector - 1) // self.sector
                table = struct.unpack_from('<%dI' % (n + 1), self.data, self.base + off)
                first = table[1] - table[0]
                if first >= min(self.sector, usize):
                    hist['stored'] += 1
                else:
                    mask = self.data[self.base + off + table[0]]
                    hist[' + '.join(v for k, v in names.items() if mask & k) or hex(mask)] += 1
            else:
                hist['stored'] += 1
        return hist


# ---- Engine asset names -----------------------------------------------------------------

def monsters(source):
    """(id, name, assets suffix, image budget) from monstdat.cpp."""
    text = open(os.path.join(source, 'monstdat.cpp'), encoding='utf-8', errors='replace').read()
    out = []
    for m in re.finditer(r'/\* (MT_\w+) \*/\s*\{ P_\("monster", "([^"]+)"\),\s*"([^"]+)",\s*(?:nullptr|"[^"]*"),\s*'
                         r'(?:nullptr|"[^"]*"),\s*MonsterAvailability::\w+,\s*(\d+),\s*(\d+)', text):
        out.append((m.group(1), m.group(2), m.group(3).replace('\\\\', '\\'), int(m.group(5))))
    return out


PLAYER_CLASSES = [('warrior', 'w'), ('rogue', 'r'), ('sorceror', 's')]
ARMOUR = 'lmh'
WEAPONS = 'nusdbamht'
PLAYER_ANIMS = ['as', 'aw', 'st', 'wl', 'at', 'ht', 'lm', 'fm', 'qm', 'dt', 'bl']
TILESETS = [('town', 'levels\\towndata\\town'), ('cathedral (l1)', 'levels\\l1data\\l1'),
            ('catacombs (l2)', 'levels\\l2data\\l2'), ('caves (l3)', 'levels\\l3data\\l3'),
            ('hell (l4)', 'levels\\l4data\\l4')]


def kib(n):
    return f'{n / 1024:8.0f} KiB'


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    mpq = Mpq(sys.argv[1])
    source = sys.argv[2] if len(sys.argv) > 2 else os.path.join(
        os.path.dirname(os.path.abspath(__file__)), '..', '3rdparty', 'devilutionX', 'Source')

    used = [b for b in mpq.blocks if b[3] & 0x80000000]
    print(f'archive: {sys.argv[1]}')
    print(f'  files {len(used)}, compressed {kib(sum(b[1] for b in used))}, '
          f'uncompressed {kib(sum(b[2] for b in used))}, sector {mpq.sector} bytes')
    print(f'  hash table {mpq.hash_entries} entries, block table {mpq.block_entries} entries '
          f'(= {kib((mpq.hash_entries + mpq.block_entries) * 16)} in RAM)')
    print('  compression:', ', '.join(f'{k} {v}' for k, v in mpq.compression_methods().most_common()))

    print('\ndungeon tilesets (CEL + TIL + MIN):')
    for label, base in TILESETS:
        sizes = [mpq.size(base + ext) for ext in ('.cel', '.til', '.min')]
        if sizes[0] is None:
            print(f'  {label:16s} not in this archive')
            continue
        print(f'  {label:16s} {kib(sum(s or 0 for s in sizes))}  (cel {kib(sizes[0]).strip()})')

    print('\nmonster animation sets (largest first; image = original budget units):')
    rows, seen = [], set()
    for mid, name, suffix, image in monsters(source):
        if suffix in seen:
            continue
        seen.add(suffix)
        total = sum(mpq.size(f'monsters\\{suffix}{c}.cl2') or 0 for c in 'nwahds')
        if total:
            rows.append((total, name, suffix, image))
    rows.sort(reverse=True)
    for total, name, suffix, image in rows[:25]:
        print(f'  {kib(total)}  image {image:5d}  {name} ({suffix})')
    print(f'  {len(rows)} monster sets present, together {kib(sum(r[0] for r in rows))}')

    print('\nplayer animation sets (all animations of one class/armour/weapon, largest first):')
    sets = []
    for cls, ch in PLAYER_CLASSES:
        for a in ARMOUR:
            for w in WEAPONS:
                prefix = ch + a + w
                total = sum(mpq.size(f'plrgfx\\{cls}\\{prefix}\\{prefix}{anim}.cl2') or 0 for anim in PLAYER_ANIMS)
                if total:
                    sets.append((total, cls, prefix))
    sets.sort(reverse=True)
    for total, cls, prefix in sets[:10]:
        print(f'  {kib(total)}  {cls} {prefix}')
    print(f'  {len(sets)} player sets present')


if __name__ == '__main__':
    main()
