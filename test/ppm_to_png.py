#!/usr/bin/env python3
"""Convert the screen previews written by the host tests (PPM) to PNG.

    TREADMILL_PREVIEW_DIR=docs/screens test/run_native_tests.sh
    python3 test/ppm_to_png.py docs/screens

Each screen is drawn 1:1 (the 1.14" ones 2x, so both packs show at a
similar size), with a grey border. Standard library only.
"""
import os
import struct
import sys
import zlib

BORDER = 2


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + bytes(r) for r in rows)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))  # 8-bit RGB
        f.write(chunk(b"IDAT", zlib.compress(raw, 9)))
        f.write(chunk(b"IEND", b""))


def convert(ppm_path):
    with open(ppm_path, "rb") as f:
        data = f.read()
    # header: P6 <width> <height> 255, then raw RGB
    parts = data.split(maxsplit=4)
    width, height = int(parts[1]), int(parts[2])
    pixels = parts[4]
    scale = 2 if width < 300 else 1
    w, h = width * scale + 2 * BORDER, height * scale + 2 * BORDER
    rows = []
    for y in range(h):
        row = bytearray()
        for x in range(w):
            if y < BORDER or y >= h - BORDER or x < BORDER or x >= w - BORDER:
                row += b"\x5a\x5a\x5a"
            else:
                i = (((y - BORDER) // scale) * width + (x - BORDER) // scale) * 3
                row += pixels[i:i + 3]
        rows.append(row)
    write_png(ppm_path[:-4] + ".png", w, h, rows)
    os.remove(ppm_path)


if __name__ == "__main__":
    folder = sys.argv[1]
    for name in sorted(os.listdir(folder)):
        if name.endswith(".ppm"):
            convert(os.path.join(folder, name))
