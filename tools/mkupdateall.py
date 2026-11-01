#!/usr/bin/env python3
"""Writes pico-lutionX's game data into pico-bootLoader's updateAll.json.

    tools/mkupdateall.py ../pico-bootLoader/updateAll/updateAll.json
    tools/mkupdateall.py --print

Two entries in the "data" list, both installed in /roms/devilutionx:

    diablo        DevilutionX's assets: every file of pico-lutionX_sdcard.zip, as
                  attached to this repository's release <tag>
    diablo_spawn  spawn.mpq, the shareware data of Diablo, from DevilutionX's own
                  download (the full game, DIABDAT.MPQ, is not installed)

The files of the first entry, their sizes and CRC32s are those of the archive that
tools/make_sdcard_zip.py builds now. It must therefore be run from the commit that is
released as <tag>, and again whenever the assets change before the release.

--tag defaults to the newest version heading in CHANGELOG.md. An entry with the same
id is replaced in place; otherwise the entry is added at the end of the list. The rest
of the file is left as it is, byte for byte. With --print the entries are printed
instead of written.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import tempfile
import zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.join(HERE, '..')
REPO_URL = 'https://github.com/PicoPlus-devel/pico-lutionX'
ZIP_NAME = 'pico-lutionX_sdcard.zip'
ZIP_PREFIX = 'roms/devilutionx/'
DESTINATION = '/roms/devilutionx'

# DevilutionX's download of the shareware data, as its Android and web builds use it.
SPAWN = {
    'id': 'diablo_spawn',
    'name': 'Diablo',
    'set': 'shareware data (spawn.mpq)',
    'url': 'https://github.com/diasurgical/devilutionx-assets/releases/download/v2/spawn.mpq',
    'destination': DESTINATION,
    'files': [{'name': 'spawn.mpq', 'size': 25448219, 'crc': '36187f94'}],
}


def changelog_tag():
    text = open(os.path.join(ROOT, 'CHANGELOG.md'), encoding='utf-8').read()
    m = re.search(r'^## (v\S+)', text, re.M)
    if not m:
        raise SystemExit('CHANGELOG.md has no "## v..." heading; give --tag')
    return m.group(1)


def assets_entry(tag):
    with tempfile.TemporaryDirectory() as d:
        path = os.path.join(d, ZIP_NAME)
        subprocess.run([sys.executable, '-I', os.path.join(HERE, 'make_sdcard_zip.py'), path],
                       check=True, stdout=subprocess.DEVNULL)
        with zipfile.ZipFile(path) as zf:
            infos = [i for i in zf.infolist() if not i.is_dir()]
    files = []
    for i in infos:
        if not i.filename.startswith(ZIP_PREFIX):
            raise SystemExit('%s: %s is outside %s' % (ZIP_NAME, i.filename, ZIP_PREFIX))
        files.append({'name': i.filename[len(ZIP_PREFIX):], 'size': i.file_size, 'crc': '%08x' % i.CRC})
    return {
        'id': 'diablo',
        'name': 'Diablo',
        'set': 'DevilutionX assets, pico-lutionX %s' % tag,
        'url': '%s/releases/download/%s/%s' % (REPO_URL, tag, ZIP_NAME),
        'destination': DESTINATION,
        'files': files,
    }


def render_entry(e, indent='    '):
    """One entry as updateAll.json writes it: a file per line, columns aligned."""
    i2 = indent + '  '
    lines = [indent + '{']
    for key in ('id', 'name', 'set', 'url', 'destination'):
        lines.append('%s"%s": %s,' % (i2, key, json.dumps(e[key], ensure_ascii=False)))
    lines.append('%s"files": [' % i2)
    names = ['"name": %s,' % json.dumps(f['name']) for f in e['files']]
    sizes = ['"size": %d,' % f['size'] for f in e['files']]
    wn = max(len(n) for n in names)
    ws = max(len(s) for s in sizes)
    for k, f in enumerate(e['files']):
        comma = ',' if k < len(e['files']) - 1 else ''
        lines.append('%s  { %s %s "crc": "%s" }%s' % (i2, names[k].ljust(wn), sizes[k].ljust(ws), f['crc'], comma))
    lines.append(i2 + ']')
    lines.append(indent + '}')
    return '\n'.join(lines)


def put_entry(text, entry):
    """text with entry in the "data" list, replacing the entry with the same id."""
    body = render_entry(entry)
    if '\n  "data": [\n' not in text:
        raise SystemExit('no "data" list in the layout this script writes; not touching it')
    start = text.index('\n  "data": [\n')
    old = re.compile(r'\n    \{\n      "id": %s,\n.*?\n    \}' % re.escape(json.dumps(entry['id'])), re.S)
    m = old.search(text, start)
    if m:
        return text[:m.start()] + '\n' + body + text[m.end():]
    end = text.index('\n  ]', start)
    return text[:end] + ',\n' + body + text[end:]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('json', nargs='?', help="pico-bootLoader's updateAll/updateAll.json")
    ap.add_argument('--tag', help='release whose %s is downloaded (default: from CHANGELOG.md)' % ZIP_NAME)
    ap.add_argument('--print', action='store_true', help='print the entries instead of writing them')
    args = ap.parse_args()

    entries = [assets_entry(args.tag or changelog_tag()), SPAWN]
    if args.print or not args.json:
        print(',\n'.join(render_entry(e) for e in entries))
        return 0

    text = open(args.json, encoding='utf-8').read()
    doc = json.loads(text)
    for e in entries:
        text = put_entry(text, e)

    # what was written must be the old document with exactly these entries put in
    data = doc.setdefault('data', [])
    for e in entries:
        ids = [d['id'] for d in data]
        if e['id'] in ids:
            data[ids.index(e['id'])] = e
        else:
            data.append(e)
    if json.loads(text) != doc:
        raise SystemExit('%s: the entries could not be put in place; not touching it' % args.json)
    open(args.json, 'w', encoding='utf-8').write(text)
    print('%s: %s, %d files, and %s' % (args.json, entries[0]['id'], len(entries[0]['files']), entries[1]['id']))
    return 0


if __name__ == '__main__':
    sys.exit(main())
