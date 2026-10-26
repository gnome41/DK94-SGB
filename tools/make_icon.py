#!/usr/bin/env python3
"""Generates the DK94 Recomp icon (original pixel art: steel girders, a
ladder and a barrel) in every size the project needs.

usage: tools/make_icon.py        (writes assets/icon/* and runtime/icon_data.h)

Outputs:
  assets/icon/dk94-{16,32,48,64,128,256}.png
  assets/icon/dk94.ico          Windows icon (all sizes, PNG-compressed)
  runtime/icon_data.h           64x64 RGBA for the window icon
The 16 px size is drawn separately; larger sizes are the 32 px art scaled
by whole pixels."""

import os
import struct
import zlib

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

PAL = {
    ".": (0, 0, 0, 0),
    "K": (20, 16, 32, 255),     # outline
    "N": (40, 34, 74, 255),     # background
    "n": (52, 45, 95, 255),     # background, upper half
    "k": (30, 24, 46, 255),     # girder openings
    "R": (222, 58, 48, 255),    # girder
    "r": (150, 32, 26, 255),    # girder shadow
    "W": (255, 140, 110, 255),  # girder highlight
    "B": (176, 104, 46, 255),   # barrel
    "b": (104, 56, 22, 255),    # barrel shadow
    "h": (226, 160, 96, 255),   # barrel highlight
    "S": (160, 160, 176, 255),  # hoop
    "s": (70, 70, 82, 255),     # hoop shadow
    "C": (120, 214, 236, 255),  # ladder rail
    "c": (64, 140, 170, 255),   # ladder rung
}


class Canvas:
    def __init__(self, size):
        self.size = size
        self.px = [["."] * size for _ in range(size)]

    def put(self, x, y, c):
        if 0 <= x < self.size and 0 <= y < self.size:
            self.px[y][x] = c

    def rounded(self, x, y, inset, r):
        x0 = y0 = inset
        x1 = y1 = self.size - 1 - inset
        if not (x0 <= x <= x1 and y0 <= y <= y1):
            return False
        cx = x0 + r if x < x0 + r else (x1 - r if x > x1 - r else x)
        cy = y0 + r if y < y0 + r else (y1 - r if y > y1 - r else y)
        return (x - cx) ** 2 + (y - cy) ** 2 <= r * r + r * 0.8

    def background(self, r):
        for y in range(self.size):
            for x in range(self.size):
                if self.rounded(x, y, 0, r):
                    self.put(x, y, "K")
                if self.rounded(x, y, 1, r - 1):
                    self.put(x, y, "n" if y < self.size * 7 // 16 else "N")

    def sprite(self, x0, y0, rows):
        for j, row in enumerate(rows):
            for i, c in enumerate(row):
                if c != ".":
                    self.put(x0 + i, y0 + j, c)


def icon32():
    c = Canvas(32)
    c.background(6)

    def girder(y0, d):          # sloped steel beam with V bracing
        for x in range(2, 30):
            y = y0 + ((x - 2) // 7) * d
            if not c.rounded(x, y + 5, 1, 5):
                continue
            c.put(x, y, "W")
            c.put(x, y + 1, "R")
            c.put(x, y + 2, "R" if x % 6 in (0, 5) else "k")
            c.put(x, y + 3, "R" if x % 6 in (1, 4) else "k")
            c.put(x, y + 4, "R" if x % 6 in (2, 3) else "k")
            c.put(x, y + 5, "r")

    girder(4, 1)
    girder(23, -1)
    for y in range(12, 21):     # ladder between the girders
        c.put(21, y, "C")
        c.put(25, y, "C")
        if y % 3 == 1:
            for x in range(22, 25):
                c.put(x, y, "c")
    c.sprite(4, 13, [           # barrel on the lower girder
        "..bbbbbb..",
        ".bhBBBBBb.",
        "sSSSSSSSSs",
        "bhBBBBBBBb",
        "bhhBBbBBBb",
        "bhBBBBBBbb",
        "bhBBBBBBBb",
        "sSSSSSSSSs",
        ".bhBBBBBb.",
        "..bbbbbb..",
    ])
    return c


def icon16():
    c = Canvas(16)
    c.background(3)

    def girder(y0, d):
        for x in range(1, 15):
            y = y0 + ((x - 1) // 5) * d
            if not c.rounded(x, y + 2, 1, 2):
                continue
            c.put(x, y, "W")
            c.put(x, y + 1, "R" if x % 3 else "k")
            c.put(x, y + 2, "r")

    girder(2, 1)
    girder(12, -1)
    for y in range(6, 11):
        c.put(11, y, "C")
        c.put(13, y, "C")
        if y % 2 == 0:
            c.put(12, y, "c")
    c.sprite(2, 6, [
        ".bbbb.",
        "SSSSSs",
        "bhBBBb",
        "bhBBBb",
        "SSSSSs",
        ".bbbb.",
    ])
    return c


def rgba(canvas, scale):
    n = canvas.size * scale
    return [[PAL[canvas.px[y // scale][x // scale]] for x in range(n)] for y in range(n)]


def png_bytes(img):
    h, w = len(img), len(img[0])
    raw = b"".join(b"\0" + b"".join(bytes(p) for p in row) for row in img)

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def ico_bytes(pngs):
    """ICO with PNG-compressed images (supported since Windows Vista)."""
    head = struct.pack("<HHH", 0, 1, len(pngs))
    offset = 6 + 16 * len(pngs)
    dirs, data = b"", b""
    for size, blob in pngs:
        dim = 0 if size >= 256 else size
        dirs += struct.pack("<BBBBHHII", dim, dim, 0, 0, 1, 32, len(blob), offset + len(data))
        data += blob
    return head + dirs + data


def main():
    out = os.path.join(ROOT, "assets", "icon")
    os.makedirs(out, exist_ok=True)
    big, small = icon32(), icon16()
    images = {16: rgba(small, 1)}
    for size in (32, 48, 64, 128, 256):
        images[size] = rgba(big, size // 32) if size % 32 == 0 else None
    # 48 px: 16 px art x3 stays crisp where 32 px art would need 1.5x
    images[48] = rgba(small, 3)
    pngs = []
    for size, img in sorted(images.items()):
        blob = png_bytes(img)
        with open(os.path.join(out, f"dk94-{size}.png"), "wb") as f:
            f.write(blob)
        pngs.append((size, blob))
    with open(os.path.join(out, "dk94.ico"), "wb") as f:
        f.write(ico_bytes(pngs))

    img = images[64]
    flat = [v for row in img for p in row for v in p]
    lines = [", ".join(f"0x{v:02x}" for v in flat[i:i + 16]) for i in range(0, len(flat), 16)]
    with open(os.path.join(ROOT, "runtime", "icon_data.h"), "w") as f:
        f.write("/* generated by tools/make_icon.py - do not edit */\n"
                "#ifndef ICON_DATA_H\n#define ICON_DATA_H\n\n"
                "#define ICON_SIZE 64\n"
                "static const unsigned char icon_rgba[ICON_SIZE * ICON_SIZE * 4] = {\n    "
                + ",\n    ".join(lines) + "\n};\n\n#endif\n")
    print("wrote", ", ".join(f"dk94-{s}.png" for s in sorted(images)), "dk94.ico, runtime/icon_data.h")


if __name__ == "__main__":
    main()
