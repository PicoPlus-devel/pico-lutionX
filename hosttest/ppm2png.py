#!/usr/bin/env python3
"""Convert binary PPM (P6) frames from q2_host to PNG. Standard library only.

    python3 hosttest/ppm2png.py frame_00600.ppm [more.ppm ...]   -> frame_00600.png ...
    python3 hosttest/ppm2png.py --hdmi frame_00600.ppm           -> 640x480, as on the board
"""
import struct
import sys
import zlib


def read_ppm(path):
    with open(path, "rb") as f:
        data = f.read()
    fields, pos = [], 0
    while len(fields) < 4:
        while data[pos:pos + 1].isspace():
            pos += 1
        if data[pos:pos + 1] == b"#":
            pos = data.index(b"\n", pos) + 1
            continue
        end = pos
        while not data[end:end + 1].isspace():
            end += 1
        fields.append(data[pos:end])
        pos = end
    if fields[0] != b"P6" or int(fields[3]) != 255:
        raise ValueError(f"{path}: not an 8-bit P6 PPM")
    w, h = int(fields[1]), int(fields[2])
    return w, h, data[pos + 1:pos + 1 + w * h * 3]


def write_png(path, w, h, rgb):
    def chunk(tag, body):
        return struct.pack(">I", len(body)) + tag + body + struct.pack(">I", zlib.crc32(tag + body))

    rows = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(rows, 9))
    png += chunk(b"IEND", b"")
    with open(path, "wb") as f:
        f.write(png)


def hdmi(w, h, rgb):
    """What the board puts on HDMI: RGB555 (src/pico/q2_display.c, rgb555x2), each
    pixel doubled across and each line sent twice."""
    rgb = bytes(c & 0xF8 for c in rgb)
    rows = []
    for y in range(h):
        line = rgb[y * w * 3:(y + 1) * w * 3]
        wide = b"".join(line[x * 3:x * 3 + 3] * 2 for x in range(w))
        rows += [wide, wide]
    return w * 2, h * 2, b"".join(rows)


args = sys.argv[1:]
as_hdmi = "--hdmi" in args
for src in (a for a in args if a != "--hdmi"):
    w, h, rgb = read_ppm(src)
    if as_hdmi:
        w, h, rgb = hdmi(w, h, rgb)
    dst = src[:-4] + ".png" if src.endswith(".ppm") else src + ".png"
    write_png(dst, w, h, rgb)
    print(dst)
