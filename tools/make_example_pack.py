#!/usr/bin/env python3
"""Builds the example mod pack in examples/mods/DK94-Example from scratch.

usage: make_example_pack.py [OUT_DIR]

Everything in the pack is drawn or composed by this script - no game
graphics or audio are used - so the pack can be shared:

  tiles/   HD versions of the in-stage timer digits 0-9 (color = indexed,
           so the game's own palettes and Super Game Boy colours apply)
  border/  an HD Super Game Boy border: night sky, girders, ladders, barrels
  audio/   a new chiptune for the first stage (music 05), with an intro
           and a loop point in music_05.ini

The only things taken from the game are the tile names: each is the 16
bytes of the original tile, which is how the runtime finds the tile to
replace (see MODDING.md).  Python 3 standard library only; if ffmpeg with
libvorbis is installed the music is written as OGG, otherwise as WAV."""

import math
import os
import shutil
import struct
import subprocess
import sys
import zlib

S = 4   # pack scale: tiles are 32x32, the border 1024x896


def write_png(path, w, h, rgba):
    raw = b"".join(b"\0" + bytes(rgba[y * w * 4:(y + 1) * w * 4]) for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 6, 0, 0, 0)) +
                chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


class Rng:
    """Small deterministic generator, so the pack is the same on every run."""
    def __init__(self, seed):
        self.s = seed

    def next(self):
        self.s = (self.s * 1103515245 + 12345) & 0x7FFFFFFF
        return self.s / 0x7FFFFFFF


# ---- shape helpers (signed distances, in pixels) ---------------------------

def seg_dist(px, py, ax, ay, bx, by):
    dx, dy = bx - ax, by - ay
    t = ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy or 1)
    t = max(0.0, min(1.0, t))
    return math.hypot(px - ax - t * dx, py - ay - t * dy)


def arc(cx, cy, rx, ry, a0, a1, n=24):
    return [(cx + rx * math.cos(math.radians(a0 + (a1 - a0) * i / n)),
             cy + ry * math.sin(math.radians(a0 + (a1 - a0) * i / n))) for i in range(n + 1)]


def poly_dist(px, py, strokes):
    d = 1e9
    for pts in strokes:
        for (ax, ay), (bx, by) in zip(pts, pts[1:]):
            d = min(d, seg_dist(px, py, ax, ay, bx, by))
    return d


# ---- tiles: timer digits ----------------------------------------------------

# The original 8x8 tiles (2 bits per pixel, 16 bytes) of the digits 0-9 the
# game shows in the stage timer; the file names must be these.
DIGIT_KEYS = [
    "00003c026e116e116e116e113c43003e", "00001c023c021c221c021c023e01003f",
    "00003c026e116e111c6338067e01007f", "00003c024e311c630e114e313c43003e",
    "00001c023c024c325c227e011c63001e", "00007c02700e7c02067976093c43003e",
    "00003c02700e7c02760976093c43003e", "00007e016e110e711c031c021c02001e",
    "00003c026e113c43760976093c43003e", "00003c026e116e113e410e313c03003e",
]

# New glyphs as strokes in a 32x32 tile (rounded, bold, drawn for this pack).
DIGIT_STROKES = [
    [arc(15.5, 15, 6.5, 9.5, 0, 360, 40)],
    [[(15.5, 5.5), (15.5, 24.5)], [(15.5, 5.5), (11, 9.5)], [(11.5, 24.5), (19.5, 24.5)]],
    [arc(15.5, 10.5, 6, 5, 195, 380) + [(9.5, 24.5), (21.5, 24.5)]],
    [arc(15.5, 10, 5.5, 4.6, 200, 450), arc(15.5, 19.6, 6.2, 5.2, 270, 520)],
    [[(18.5, 24.5), (18.5, 5.5), (9.2, 18.5), (22.5, 18.5)]],
    [[(21, 5.5), (11, 5.5), (10.2, 14.2)] + arc(15.5, 18.6, 6.2, 6, 232, 500)],
    [arc(16.5, 15.5, 6.8, 10, 295, 180) + [(9.7, 19)], arc(15.6, 19, 6, 5.7, 0, 360, 40)],
    [[(9.5, 5.5), (21.5, 5.5), (13, 24.5)]],
    [arc(15.5, 10, 5.2, 4.6, 0, 360, 36), arc(15.5, 19.6, 6.2, 5.2, 0, 360, 40)],
    [arc(15.4, 11, 6, 5.7, 0, 360, 40), [(21.4, 11), (21.4, 14)] + arc(14.6, 14, 6.8, 10, 0, 115)],
]


def digit_tile(strokes):
    """Colour indices (as greys, see MODDING.md): body 1, shadow 2,
    bevel 3, background 0.  With the stage palette that is a black digit
    with a light grey shadow and a dark grey highlight."""
    grey = [255, 170, 85, 0]
    px = bytearray(32 * 32 * 4)
    for y in range(32):
        for x in range(32):
            cx, cy = x + 0.5, y + 0.5
            idx = 0
            if poly_dist(cx - 2.2, cy - 2.2, strokes) < 2.9:
                idx = 2                                        # drop shadow
            if poly_dist(cx, cy, strokes) < 2.9:
                idx = 1                                        # body
                if poly_dist(cx + 1.0, cy + 1.0, strokes) < 0.9:
                    idx = 3                                    # bevel highlight
            v = grey[idx]
            px[(y * 32 + x) * 4:(y * 32 + x) * 4 + 4] = bytes((v, v, v, 255))
    return px


# ---- border -----------------------------------------------------------------

W, H = 256 * S, 224 * S
GAME = (48, 40, 208, 184)       # the game screen inside the border, native pixels


class Canvas:
    def __init__(self, w, h):
        self.w, self.h = w, h
        self.px = [[0.0, 0.0, 0.0, 0.0] for _ in range(w * h)]

    def blend(self, x, y, col, a):
        if 0 <= x < self.w and 0 <= y < self.h and a > 0:
            p = self.px[y * self.w + x]
            a = min(1.0, a) * (col[3] / 255 if len(col) > 3 else 1.0)
            for i in range(3):
                p[i] = p[i] * (1 - a) + col[i] * a
            p[3] = p[3] * (1 - a) + a

    def shape(self, box, dist, col):
        """Fills where dist(x, y) < 0 (native units), anti-aliased."""
        x0, y0, x1, y1 = (int(v * S) for v in box)
        for y in range(max(0, y0 - 1), min(self.h, y1 + 2)):
            for x in range(max(0, x0 - 1), min(self.w, x1 + 2)):
                d = dist((x + 0.5) / S, (y + 0.5) / S) * S
                self.blend(x, y, col, 0.5 - d)

    def circle(self, cx, cy, r, col):
        self.shape((cx - r, cy - r, cx + r, cy + r), lambda x, y: math.hypot(x - cx, y - cy) - r, col)

    def rrect(self, x0, y0, x1, y1, r, col):
        cx, cy, hw, hh = (x0 + x1) / 2, (y0 + y1) / 2, (x1 - x0) / 2, (y1 - y0) / 2

        def d(x, y):
            qx, qy = abs(x - cx) - hw + r, abs(y - cy) - hh + r
            return math.hypot(max(qx, 0), max(qy, 0)) + min(max(qx, qy), 0) - r
        self.shape((x0, y0, x1, y1), d, col)

    def line(self, ax, ay, bx, by, r, col):
        self.shape((min(ax, bx) - r, min(ay, by) - r, max(ax, bx) + r, max(ay, by) + r),
                   lambda x, y: seg_dist(x, y, ax, ay, bx, by) - r, col)

    def rgba(self):
        out = bytearray(self.w * self.h * 4)
        for i, (r, g, b, a) in enumerate(self.px):
            out[i * 4:i * 4 + 4] = bytes((int(r + 0.5), int(g + 0.5), int(b + 0.5), int(a * 255 + 0.5)))
        return out


def girder(c, x0, x1, y):
    """A red steel girder, 8 high, with its top edge at y."""
    h = 8
    red, dark, light = (206, 46, 62), (120, 18, 40), (246, 120, 120)
    c.rrect(x0, y, x1, y + h, 0, red)
    c.rrect(x0, y, x1, y + 1.5, 0, light)                     # flanges
    c.rrect(x0, y + h - 1.5, x1, y + h, 0, dark)
    # lattice: holes between zig-zag struts show the sky through
    for k in range(int((x1 - x0) // 8)):
        bx = x0 + k * 8
        for tri in ((bx + 1.6, 2.6, bx + 6.4, 2.6, bx + 4, 5.4), (bx + 5.6, 5.4, bx + 10.4, 5.4, bx + 8, 2.6)):
            if tri[4] > x1 - 1:
                continue
            ax, ay, bx2, by, cx, cy = (v + (y if i % 2 else 0) for i, v in enumerate(tri))

            def d(px, py, ax=ax, ay=ay, bx2=bx2, by=by, cx=cx, cy=cy):
                e1 = (bx2 - ax) * (py - ay) - (by - ay) * (px - ax)
                e2 = (cx - bx2) * (py - by) - (cy - by) * (px - bx2)
                e3 = (ax - cx) * (py - cy) - (ay - cy) * (px - cx)
                inside = (e1 >= 0 and e2 >= 0 and e3 >= 0) or (e1 <= 0 and e2 <= 0 and e3 <= 0)
                m = min(seg_dist(px, py, ax, ay, bx2, by), seg_dist(px, py, bx2, by, cx, cy),
                        seg_dist(px, py, cx, cy, ax, ay))
                return -m if inside else m
            c.shape((min(ax, bx2, cx), y, max(ax, bx2, cx), y + h), d, (60, 10, 30, 200))
    for k in range(int((x1 - x0) // 16) + 1):                 # rivets
        rx = x0 + 4 + k * 16
        if rx < x1 - 2:
            c.circle(rx, y + 1.2, 0.7, (255, 210, 200))


def ladder(c, x, y0, y1):
    rail, rung = (120, 210, 240), (90, 170, 210)
    for ry in range(int(y0) + 6, int(y1) - 2, 7):
        c.line(x + 1, ry, x + 11, ry, 0.9, rung)
    c.line(x + 1, y0, x + 1, y1, 1.1, rail)
    c.line(x + 11, y0, x + 11, y1, 1.1, rail)


def barrel_side(c, cx, cy):
    """A standing barrel seen from the side, 14 x 16."""
    c.rrect(cx - 7, cy - 8, cx + 7, cy + 8, 3.5, (150, 82, 36))
    c.rrect(cx - 5.5, cy - 8, cx + 5.5, cy + 8, 3, (178, 104, 48))
    for k in (-2, 2):
        c.line(cx + k, cy - 6.5, cx + k, cy + 6.5, 0.35, (120, 62, 26))
    for hy in (-5, 5):
        c.line(cx - 6.8, cy + hy, cx + 6.8, cy + hy, 0.9, (58, 44, 52))
    c.line(cx - 4, cy - 7.2, cx + 3, cy - 7.2, 0.5, (226, 170, 110))


def barrel_end(c, cx, cy, r):
    """A barrel rolling towards the viewer: the round end with its rings."""
    c.circle(cx, cy, r, (58, 44, 52))
    c.circle(cx, cy, r - 1.0, (170, 96, 44))
    c.circle(cx, cy, r * 0.62, (58, 44, 52))
    c.circle(cx, cy, r * 0.62 - 0.9, (196, 118, 56))
    for a in (20, 140, 260):
        t = math.radians(a)
        c.line(cx + math.cos(t) * r * 0.2, cy + math.sin(t) * r * 0.2,
               cx + math.cos(t) * r * 0.5, cy + math.sin(t) * r * 0.5, 0.4, (120, 62, 26))
    c.circle(cx - r * 0.35, cy - r * 0.45, r * 0.16, (240, 190, 130))


# 5x7 letters for the caption (drawn for this pack)
FONT = {
    "D": "11110 10001 10001 10001 10001 10001 11110", "K": "10001 10010 10100 11000 10100 10010 10001",
    "9": "01110 10001 10001 01111 00001 00010 01100", "4": "00010 00110 01010 10010 11111 00010 00010",
    "R": "11110 10001 10001 11110 10100 10010 10001", "E": "11111 10000 10000 11110 10000 10000 11111",
    "C": "01110 10001 10000 10000 10000 10001 01110", "O": "01110 10001 10001 10001 10001 10001 01110",
    "M": "10001 11011 10101 10101 10001 10001 10001", "P": "11110 10001 10001 11110 10000 10000 10000",
    "X": "10001 10001 01010 00100 01010 10001 10001", "A": "01110 10001 10001 11111 10001 10001 10001",
    "L": "10000 10000 10000 10000 10000 10000 11111", " ": "00000 00000 00000 00000 00000 00000 00000",
}


def text(c, s, cx, y, col, shadow):
    w = len(s) * 6 - 1
    x0 = cx - w / 2
    for i, ch in enumerate(s):
        for ry, row in enumerate(FONT[ch].split()):
            for rx, bit in enumerate(row):
                if bit == "1":
                    px, py = x0 + i * 6 + rx, y + ry
                    c.rrect(px + 0.5, py + 0.5, px + 1.5, py + 1.5, 0.25, shadow)
                    c.rrect(px, py, px + 1, py + 1, 0.25, col)


def border():
    c = Canvas(W, H)
    rnd = Rng(1994)
    # night sky
    for y in range(H):
        t = y / H
        col = (14 + 40 * t, 16 + 10 * t, 46 + 34 * t)
        for x in range(W):
            c.px[y * W + x] = [col[0], col[1], col[2], 1.0]
    for _ in range(150):
        x, y, s = rnd.next() * 256, rnd.next() * 224, rnd.next()
        c.circle(x, y, 0.25 + 0.45 * s * s, (255, 250, 220, 140 + int(115 * s)))
    # skyline
    x = 0.0
    while x < 256:
        bw, bh = 8 + rnd.next() * 14, 14 + rnd.next() * 26
        c.rrect(x, 224 - bh, x + bw, 225, 0.5, (30, 22, 58))
        for wy in range(int(224 - bh + 3), 222, 4):
            for wx in range(int(x + 2), int(x + bw - 2), 3):
                if rnd.next() < 0.3:
                    c.rrect(wx, wy, wx + 1.2, wy + 1.6, 0.2, (255, 214, 120, 200))
        x += bw + 1 + rnd.next() * 4
    # moon
    for r, a in ((15, 30), (12, 45)):
        c.circle(22, 14, r, (255, 240, 200, a))
    c.circle(22, 14, 9, (250, 238, 196))
    for mx, my, mr in ((19, 11, 2.2), (25, 17, 1.6), (25, 10, 1.0)):
        c.circle(mx, my, mr, (226, 210, 168))
    # screen bezel
    gx0, gy0, gx1, gy1 = GAME
    c.rrect(gx0 - 6, gy0 - 6, gx1 + 6, gy1 + 6, 5, (10, 10, 18))
    c.rrect(gx0 - 4.5, gy0 - 4.5, gx1 + 4.5, gy1 + 4.5, 3.5, (70, 74, 96))
    c.rrect(gx0 - 3, gy0 - 3, gx1 + 3, gy1 + 3, 2.5, (26, 26, 38))
    for sx, sy in ((gx0 - 3.6, gy0 - 3.6), (gx1 + 3.6, gy0 - 3.6), (gx0 - 3.6, gy1 + 3.6), (gx1 + 3.6, gy1 + 3.6)):
        c.circle(sx, sy, 1.0, (190, 196, 214))
    # girders, ladders, barrels
    girder(c, 0, 256, 22)
    girder(c, 0, 256, 194)
    ladder(c, 16, 30, 194)
    ladder(c, 228, 30, 194)
    ladder(c, 196, 2, 22)
    barrel_end(c, 132, 15, 6.5)
    barrel_end(c, 160, 15, 6.5)
    for bx in (11, 26, 245):
        barrel_side(c, bx, 214)
    text(c, "DK94 RECOMP", 128, 204, (255, 222, 90), (90, 30, 30))
    text(c, "EXAMPLE PACK", 128, 214, (140, 220, 250), (20, 30, 70))
    # the game screen must be fully transparent
    for y in range(gy0 * S, gy1 * S):
        for x in range(gx0 * S, gx1 * S):
            c.px[y * W + x] = [0.0, 0.0, 0.0, 0.0]
    return c.rgba()


# ---- music ------------------------------------------------------------------

RATE = 32000
STEP = 60 / 150 / 4             # a 16th note at 150 bpm

NOTE = {"C": 0, "D": 2, "E": 4, "F": 5, "G": 7, "A": 9, "B": 11}


def midi(n):
    return 12 * (int(n[-1]) + 1) + NOTE[n[0]] + (1 if "#" in n else 0)


# lead: note/16ths, "-" = rest.  Bars 1-8: verse, 9-16: bridge.
LEAD = """
E5/2 G5/2 C6/3 G5/1 E5/2 G5/2 A5/4 | A5/2 G5/2 E5/2 C5/2 D5/2 E5/2 A4/4 |
F5/2 A5/2 C6/2 A5/2 G5/2 F5/2 E5/2 D5/2 | D5/3 E5/1 F5/2 G5/2 B5/4 -/2 G5/2 |
E5/2 G5/2 C6/3 G5/1 E6/4 D6/2 C6/2 | A5/2 C6/2 B5/2 A5/2 G5/2 E5/2 C5/4 |
F5/2 E5/2 F5/2 A5/2 G5/4 F5/2 D5/2 | G5/4 F5/2 E5/2 D5/4 B4/4 |
A5/4 C6/2 A5/2 F5/4 A5/4 | B5/4 D6/2 B5/2 G5/4 B5/4 |
E6/3 D6/1 B5/2 G5/2 E5/4 G5/4 | A5/2 B5/2 C6/2 D6/2 E6/8 |
F6/2 E6/2 C6/2 A5/2 D6/2 C6/2 A5/4 | G5/2 A5/2 B5/2 D6/2 C6/2 B5/2 G5/4 |
C6/4 G5/2 E5/2 G5/2 C6/2 E6/4 | D6/2 C6/2 B5/2 G5/2 C6/8
"""
CHORDS = "C Am F G C Am F G F G Em Am F G C C".split()
CHORD_NOTES = {"C": (0, 4, 7), "Am": (9, 12, 16), "F": (5, 9, 12), "G": (7, 11, 14), "Em": (4, 7, 11)}


def square(phase, duty):
    return 1.0 if (phase % 1.0) < duty else -1.0


def triangle(phase):
    p = phase % 1.0
    return 4 * p - 1 if p < 0.5 else 3 - 4 * p


def music():
    bars = len(CHORDS) + 1                  # + 1 bar intro
    n = int(bars * 16 * STEP * RATE)
    mix = [0.0] * n
    intro = int(16 * STEP * RATE)

    def tone(start, dur, freq, wave, vol, vib=0.0):
        s0, s1 = int(start * RATE), min(n, int((start + dur) * RATE))
        ph = 0.0
        for i in range(s0, s1):
            t = (i - s0) / RATE
            env = min(1.0, t / 0.004) * (0.75 + 0.25 * math.exp(-t * 12)) * min(1.0, (s1 - i) / (RATE * 0.012))
            f = freq * (1 + (vib * math.sin(2 * math.pi * 5.5 * t) if t > 0.15 else 0))
            ph += f / RATE
            mix[i] += wave(ph) * vol * env

    def hz(m):
        return 440 * 2 ** ((m - 69) / 12)

    t = 16 * STEP                            # lead starts after the intro
    for tok in LEAD.replace("|", " ").split():
        note, d = tok.split("/")
        d = int(d) * STEP
        if note != "-":
            tone(t, d * 0.92, hz(midi(note) - 12), lambda p: square(p, 0.25), 0.30, vib=0.006)
        t += d
    for b, ch in enumerate(CHORDS):
        t0 = (b + 1) * 16 * STEP
        root = 36 + CHORD_NOTES[ch][0] % 12
        for k, off in enumerate((0, 12, 7, 12, 0, 12, 7, 12)):          # bass in 8ths
            tone(t0 + k * 2 * STEP, 2 * STEP * 0.8, hz(root + off), triangle, 0.42)
        for k in range(16):                                              # arpeggio
            m = 60 + CHORD_NOTES[ch][k % 3]
            tone(t0 + k * STEP, STEP * 0.7, hz(m), lambda p: square(p, 0.125), 0.07)

    lfsr = 0x7FFF

    def noise():
        nonlocal lfsr
        bit = (lfsr ^ (lfsr >> 1)) & 1
        lfsr = (lfsr >> 1) | (bit << 14)
        return 1.0 if lfsr & 1 else -1.0

    def drum(start, kind):
        s0 = int(start * RATE)
        length = {"k": 0.14, "s": 0.12, "h": 0.03}[kind]
        ph = 0.0
        held = 0.0
        for i in range(s0, min(n, s0 + int(length * RATE))):
            t = (i - s0) / RATE
            if kind == "k":
                ph += (55 + 120 * math.exp(-t * 40)) / RATE
                v = math.sin(2 * math.pi * ph) * 0.55 * math.exp(-t * 22)
            else:
                if (i - s0) % (1 if kind == "h" else 2) == 0:
                    held = noise()
                v = held * (0.22 if kind == "s" else 0.10) * math.exp(-t * (30 if kind == "s" else 120))
            mix[i] += v

    for k in (0, 4, 8):                                                  # intro
        drum(k * STEP, "k")
    for k in (12, 13, 14, 15):
        drum(k * STEP, "s")
    for b in range(1, bars):
        t0 = b * 16 * STEP
        for k in range(16):
            if k in (0, 8, 10):
                drum(t0 + k * STEP, "k")
            if k in (4, 12):
                drum(t0 + k * STEP, "s")
            if k % 2 == 0:
                drum(t0 + k * STEP, "h")

    lp, out = 0.0, bytearray()
    peak = max(abs(v) for v in mix) or 1.0
    for v in mix:
        lp += (v - lp) * 0.45                                            # soften the square waves
        out += struct.pack("<h", int(lp / peak * 13000))
    return out, intro


def write_wav(path, pcm):
    with open(path, "wb") as f:
        f.write(b"RIFF" + struct.pack("<I", 36 + len(pcm)) + b"WAVEfmt " +
                struct.pack("<IHHIIHH", 16, 1, 1, RATE, RATE * 2, 2, 16) +
                b"data" + struct.pack("<I", len(pcm)) + pcm)


# ---- pack -------------------------------------------------------------------

PACK_INI = """\
; Example pack for DK94 Recomp - everything in it is original work, made by
; tools/make_example_pack.py (MIT licence, like the rest of the project).
name = DK94 Example Pack
author = DK94 Recomp project
version = 1.0
description = HD border, HD timer digits and a new first-stage tune
scale = {scale}
; indexed: the digits are drawn with the four greys, so the game's palettes
; and Super Game Boy colours apply to them (the border is always full colour)
color = indexed
"""


def main():
    root = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(root, "examples", "mods", "DK94-Example")
    for sub in ("tiles", "border", "audio"):
        os.makedirs(os.path.join(out, sub), exist_ok=True)
    with open(os.path.join(out, "pack.ini"), "w") as f:
        f.write(PACK_INI.format(scale=S))

    for key, strokes in zip(DIGIT_KEYS, DIGIT_STROKES):
        write_png(os.path.join(out, "tiles", key + ".png"), 32, 32, digit_tile(strokes))
    write_png(os.path.join(out, "border", "border.png"), W, H, border())

    pcm, intro = music()
    adir = os.path.join(out, "audio")
    for old in ("music_05.wav", "music_05.ogg"):
        if os.path.exists(os.path.join(adir, old)):
            os.remove(os.path.join(adir, old))
    wav = os.path.join(adir, "music_05.wav")
    write_wav(wav, pcm)
    ffmpeg = shutil.which("ffmpeg")
    if ffmpeg and subprocess.run([ffmpeg, "-loglevel", "error", "-y", "-i", wav, "-c:a", "libvorbis",
                                  "-q:a", "5", "-map_metadata", "-1", "-fflags", "+bitexact",
                                  os.path.join(adir, "music_05.ogg")]).returncode == 0:
        os.remove(wav)
    with open(os.path.join(adir, "music_05.ini"), "w") as f:
        f.write("; the first bar is an intro; the song then loops from bar 2 to the end\n"
                f"loop_start = {intro}\n")
    print(f"wrote {out}")


if __name__ == "__main__":
    main()
