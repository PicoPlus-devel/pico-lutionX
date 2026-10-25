#!/usr/bin/env python3
"""Small helpers for patching the vendored DevilutionX sources, which use CRLF line endings.

Library use only (imported by one-off patch scripts):
    from dxpatch import Patcher
    p = Patcher('3rdparty/devilutionX/Source/diablo.cpp')
    p.add_include('"dx_heap.h"')
    p.after_function_brace('void LoadLvlGFX()', 'DxHeapScope dxHeapScope(DXH_DUNGEON);')
    p.replace(old, new)            # exact text, must occur once (LF in arguments is fine)
    p.save()
Every inserted line is tagged with "pico-lutionX:" so the patches can be found again.
"""

TAG = '// pico-lutionX'


class Patcher:
    def __init__(self, path):
        self.path = path
        with open(path, newline='') as f:
            self.text = f.read()
        self.nl = '\r\n' if '\r\n' in self.text else '\n'

    def _n(self, s):
        return s.replace('\r\n', '\n').replace('\n', self.nl)

    def replace(self, old, new, count=1):
        old, new = self._n(old), self._n(new)
        found = self.text.count(old)
        if found != count:
            raise SystemExit(f'{self.path}: expected {count} occurrence(s), found {found}:\n{old}')
        self.text = self.text.replace(old, new)

    def add_include(self, header):
        line = f'#include {header} {TAG}'
        if line in self.text:
            return
        idx = self.text.index('#include')
        end = self.text.index(self.nl, idx) + len(self.nl)
        self.text = self.text[:end] + line + self.nl + self.text[end:]

    def after_function_brace(self, signature, statement, indent='\t'):
        sig = self._n(signature)
        idx = self.text.find(sig + self.nl + '{' + self.nl)
        if idx < 0:
            raise SystemExit(f'{self.path}: function not found: {signature}')
        pos = idx + len(sig + self.nl + '{' + self.nl)
        line = f'{indent}{statement} {TAG}: heap accounting{self.nl}'
        if self.text[pos:pos + len(line)] == line:
            return
        self.text = self.text[:pos] + line + self.text[pos:]

    def save(self):
        with open(self.path, 'w', newline='') as f:
            f.write(self.text)
