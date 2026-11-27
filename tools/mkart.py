#!/usr/bin/env python3
"""Draws the new AG Bear artwork (park, shop, props, icons, dress-up items) as PNGs in art/.

The style follows the existing living-room and house pictures (bg.png, outside-house.png):
flat pastel colours with a dark purple outline.  Everything here is generated, so the PNGs
can be rebuilt at any time with:

    python3 tools/mkart.py            # writes art/*.png
    python3 tools/mkart.py --preview  # also writes art/preview.png (contact sheet)

Transparent pixels are written with alpha 0; tools/mkassets.py turns them into colour 0.
"""
import argparse
import math
import random
from pathlib import Path

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parent.parent
ART = ROOT / "art"

# --- palette taken from the original pictures -------------------------------------------
OL = (0x4E, 0x43, 0x59)       # outline purple
OL2 = (0x3F, 0x38, 0x4D)
SKY = (0xA0, 0xD6, 0xE9)
SKY2 = (0xB8, 0xE2, 0xF0)
CLOUD = (0xFB, 0xF4, 0xDF)
GRASS = (0xB5, 0xD6, 0x93)
GRASS2 = (0x9C, 0xC8, 0x7C)
GRASS3 = (0x7A, 0xBE, 0x88)
LEAF = (0x70, 0xAF, 0x83)
LEAF2 = (0x8F, 0xCB, 0x96)
TEAL = (0x6D, 0xC7, 0xBD)
WALL = (0xF5, 0xDA, 0x88)
LILAC = (0xB2, 0x91, 0xC6)
LILAC2 = (0x8B, 0x68, 0xA5)
WOOD = (0xBD, 0x96, 0x6D)
WOOD2 = (0x96, 0x6E, 0x4C)
CREAM = (0xFC, 0xF4, 0xE0)
PINK = (0xF4, 0xA6, 0xB8)
PINK2 = (0xE0, 0x7A, 0x96)
RED = (0xE0, 0x4A, 0x4A)
RED2 = (0xA8, 0x2C, 0x34)
ORANGE = (0xF6, 0xA8, 0x4C)
YELLOW = (0xF8, 0xD8, 0x48)
GOLD = (0xE8, 0xB0, 0x30)
GOLD2 = (0xB8, 0x7C, 0x1C)
BLUE = (0x5C, 0x8C, 0xD8)
BLUE2 = (0x3C, 0x5C, 0xA8)
WATER = (0x7C, 0xC4, 0xE4)
WATER2 = (0x5C, 0xA8, 0xD4)
WHITE = (0xFF, 0xFF, 0xFF)
GREY = (0xB4, 0xB0, 0xBC)
DARK = (0x28, 0x24, 0x30)
BLACK = (0x14, 0x12, 0x18)
GREEN = (0x58, 0xB0, 0x58)
PATH = (0xE8, 0xD4, 0xA8)
PATH2 = (0xD4, 0xBC, 0x8C)
TILE1 = (0xF0, 0xE8, 0xDC)
TILE2 = (0xD8, 0xCC, 0xC0)
PEACH = (0xF8, 0xD0, 0xB0)
PEACH2 = (0xF0, 0xB8, 0x98)
CLEAR = (0, 0, 0, 0)


def canvas(w, h, fill=CLEAR):
    im = Image.new("RGBA", (w, h), fill if len(fill) == 4 else fill + (255,))
    return im, ImageDraw.Draw(im)


def ell(d, box, fill, outline=OL):
    d.ellipse(box, fill=fill, outline=outline)


def rect(d, box, fill, outline=OL):
    d.rectangle(box, fill=fill, outline=outline)


def poly(d, pts, fill, outline=OL):
    d.polygon(pts, fill=fill, outline=outline)


def line(d, pts, fill=OL, width=1):
    d.line(pts, fill=fill, width=width)


def cloud(d, x, y, s=1.0):
    for dx, dy, r in ((0, 6, 8), (10, 0, 10), (22, 4, 9), (30, 9, 7)):
        r = int(r * s)
        ell(d, (x + int(dx * s) - r, y + int(dy * s) - r, x + int(dx * s) + r, y + int(dy * s) + r), CLOUD, CLOUD)
    d.rectangle((x - int(8 * s), y + int(6 * s), x + int(37 * s), y + int(16 * s)), fill=CLOUD)


def flower(d, x, y, petal):
    for dx, dy in ((-2, 0), (2, 0), (0, -2), (0, 2)):
        d.point((x + dx, y + dy), fill=petal)
        d.point((x + dx // 2, y + dy // 2), fill=petal)
    d.point((x, y), fill=YELLOW)


def tree(d, x, ground, h=60, r=26, trunk=WOOD2):
    rect(d, (x - 5, ground - h, x + 5, ground), trunk)
    line(d, [(x - 1, ground - h + 8), (x - 1, ground - 4)], WOOD)
    cy = ground - h - r + 10
    for dx, dy, rr in ((-r + 6, 4, r - 6), (r - 6, 6, r - 8), (0, -6, r), (0, 8, r - 4)):
        ell(d, (x + dx - rr, cy + dy - rr, x + dx + rr, cy + dy + rr), LEAF)
    for dx, dy, rr in ((-r + 6, 4, r - 7), (r - 6, 6, r - 9), (0, -6, r - 1), (0, 8, r - 5)):
        d.ellipse((x + dx - rr, cy + dy - rr, x + dx + rr, cy + dy + rr), fill=LEAF)
    for dx, dy in ((-8, -14), (6, -10), (-14, 2), (12, 8), (0, 4)):
        ell(d, (x + dx - 3, cy + dy - 2, x + dx + 3, cy + dy + 2), LEAF2, LEAF2)


def dither_band(d, y0, y1, w, c1, c2):
    for y in range(y0, y1):
        for x in range((y & 1), w, 2):
            d.point((x, y), fill=c2)
        d.line([(0, y), (0, y)], fill=c1)


# --- scenes (320x200) --------------------------------------------------------------------
def park():
    im, d = canvas(320, 200, SKY)
    d.rectangle((0, 0, 319, 40), fill=SKY2)
    dither_band(d, 40, 48, 320, SKY2, SKY)
    cloud(d, 24, 18)
    cloud(d, 196, 10, 1.2)
    cloud(d, 270, 40, 0.8)
    # distant hills
    for cx, r, col in ((60, 90, GRASS3), (230, 110, LEAF2), (330, 70, GRASS3)):
        d.ellipse((cx - r, 108 - r // 3, cx + r, 108 + r), fill=col, outline=OL)
    d.rectangle((0, 112, 319, 199), fill=GRASS)
    line(d, [(0, 112), (319, 112)])
    dither_band(d, 112, 118, 320, GRASS, GRASS2)
    # path
    poly(d, [(150, 112), (176, 112), (230, 199), (120, 199)], PATH)
    for y in range(120, 199, 9):
        d.point((150 + (y - 112) // 3, y), fill=PATH2)
        d.point((186 - (y - 112) // 6, y + 4), fill=PATH2)
    # pond
    ell(d, (14, 150, 104, 186), WATER)
    ell(d, (26, 158, 74, 176), WATER2, WATER2)
    line(d, [(36, 162), (48, 162)], CREAM)
    line(d, [(58, 170), (66, 170)], CREAM)
    # reeds
    for x in (100, 104, 108):
        line(d, [(x, 176), (x + 1, 160)], GREEN)
        rect(d, (x, 158, x + 2, 163), WOOD2, WOOD2)
    # swing frame (left) - the seat and ropes are drawn by the game
    for x0, x1 in ((16, 30), (90, 76)):
        line(d, [(x0, 142), (x1, 66)], OL, 3)
        line(d, [(x0, 141), (x1, 67)], LILAC, 1)
    rect(d, (26, 62, 80, 68), LILAC)
    line(d, [(28, 64), (78, 64)], LILAC2)
    # slide
    poly(d, [(112, 130), (118, 130), (118, 80), (112, 80)], LILAC)
    poly(d, [(118, 82), (126, 82), (148, 140), (140, 142)], YELLOW)
    line(d, [(121, 82), (144, 141)], GOLD)
    for y in range(86, 130, 8):
        line(d, [(112, y), (118, y)])
    # big honey tree (right) - honey pots fall from it in the catch game
    tree(d, 272, 132, h=46, r=32)
    for (hx, hy) in ((252, 52), (290, 64)):
        rect(d, (hx - 4, hy - 4, hx + 4, hy + 4), GOLD)
        line(d, [(hx - 4, hy - 1), (hx + 4, hy - 1)], ORANGE)
    # bench
    rect(d, (196, 128, 246, 132), WOOD)
    rect(d, (196, 120, 246, 124), WOOD)
    rect(d, (200, 132, 203, 142), WOOD2)
    rect(d, (239, 132, 242, 142), WOOD2)
    # flowers
    rnd = random.Random(7)
    for _ in range(36):
        x, y = rnd.randrange(4, 316), rnd.randrange(122, 198)
        if 112 < x < 238 and y > 112 + (x - 150) * 0:  # keep the path clear
            if 120 + (y - 112) // 3 < x < 180 + (y - 112) * 2 // 3:
                continue
        if 14 < x < 110 and 148 < y < 188:
            continue
        flower(d, x, y, rnd.choice((PINK, CREAM, RED, LILAC)))
    return im


def shop():
    im, d = canvas(320, 200, PEACH)
    # striped wallpaper
    for x in range(0, 320, 16):
        d.rectangle((x, 0, x + 7, 150), fill=PEACH2)
    # awning sign
    rect(d, (0, 0, 319, 22), CREAM)
    for x in range(0, 320, 20):
        poly(d, [(x, 22), (x + 20, 22), (x + 10, 30)], RED if (x // 20) % 2 == 0 else CREAM)
    # shelves: three long shelves on the back wall
    for sy in (58, 96, 134):
        rect(d, (8, sy, 220, sy + 5), WOOD)
        rect(d, (12, sy + 6, 16, sy + 12), WOOD2)
        rect(d, (212, sy + 6, 216, sy + 12), WOOD2)
    # goods on the shelves (drawn small; the real price list is the shop buttons)
    def jar(x, y):
        rect(d, (x, y - 11, x + 9, y), GOLD)
        rect(d, (x - 1, y - 14, x + 10, y - 11), RED)
        line(d, [(x + 2, y - 6), (x + 7, y - 6)], ORANGE)

    def cake(x, y):
        rect(d, (x, y - 8, x + 13, y), PINK)
        rect(d, (x, y - 11, x + 13, y - 8), CREAM)
        d.point((x + 6, y - 13), fill=RED)
        d.point((x + 6, y - 12), fill=RED)

    def apple(x, y):
        ell(d, (x, y - 8, x + 8, y), RED)
        d.point((x + 4, y - 9), fill=GREEN)

    def bottle(x, y):
        rect(d, (x, y - 12, x + 7, y), CREAM)
        rect(d, (x + 2, y - 15, x + 5, y - 12), BLUE)
        line(d, [(x + 2, y - 6), (x + 5, y - 6)], RED)
        line(d, [(x + 3, y - 8), (x + 3, y - 4)], RED)

    def ball(x, y):
        ell(d, (x, y - 10, x + 10, y), BLUE)
        line(d, [(x + 1, y - 5), (x + 9, y - 5)], CREAM)

    def gift(x, y):
        rect(d, (x, y - 10, x + 11, y), LILAC)
        line(d, [(x + 5, y - 10), (x + 5, y)], YELLOW)
        line(d, [(x, y - 6), (x + 11, y - 6)], YELLOW)

    x = 20
    for i in range(7):
        (jar if i % 2 == 0 else apple)(x, 57)
        x += 28
    x = 20
    for i in range(7):
        (cake if i % 2 == 0 else bottle)(x, 95)
        x += 28
    x = 20
    for i in range(7):
        (ball if i % 2 == 0 else gift)(x, 133)
        x += 28
    # sunglasses and hat stand on the right
    rect(d, (236, 50, 300, 150), CREAM)
    line(d, [(268, 50), (268, 150)], PEACH2)
    rect(d, (244, 64, 260, 70), DARK)
    rect(d, (276, 64, 292, 70), DARK)
    poly(d, [(250, 110), (262, 110), (256, 88)], PINK)
    poly(d, [(274, 108), (294, 108), (290, 96), (278, 96)], GOLD)
    for px in (276, 282, 288):
        poly(d, [(px, 96), (px + 4, 96), (px + 2, 90)], GOLD)
    # floor tiles
    d.rectangle((0, 150, 319, 199), fill=TILE1)
    line(d, [(0, 150), (319, 150)])
    for ty in range(150, 200, 10):
        for tx in range(0, 320, 20):
            if ((tx // 20) + (ty // 10)) % 2:
                d.rectangle((tx, ty + 1, tx + 19, ty + 10), fill=TILE2)
    # counter (front right) - the shopkeeper stands behind it
    rect(d, (226, 150, 319, 199), WOOD)
    rect(d, (222, 144, 319, 152), WOOD2)
    for x in range(236, 319, 22):
        rect(d, (x, 160, x + 14, 190), WOOD2)
    # till
    rect(d, (290, 128, 314, 144), LILAC)
    rect(d, (294, 124, 310, 130), TEAL)
    return im


# --- props (transparent) ----------------------------------------------------------------
def honeypot(s=16):
    im, d = canvas(s, s)
    k = s / 16
    ell(d, (1 * k, 4 * k, 14 * k, 15 * k), GOLD)
    rect(d, (3 * k, 1 * k, 12 * k, 5 * k), GOLD2)
    d.ellipse((4 * k, 7 * k, 7 * k, 10 * k), fill=YELLOW)
    d.text((5 * k, 7 * k), "", fill=OL)
    return im


def apple_img():
    im, d = canvas(12, 13)
    ell(d, (0, 2, 11, 12), RED)
    d.point((3, 5), fill=PINK)
    d.point((3, 6), fill=PINK)
    line(d, [(6, 0), (6, 3)], WOOD2)
    d.point((7, 1), fill=GREEN)
    d.point((8, 1), fill=GREEN)
    return im


def cake_img():
    im, d = canvas(16, 16)
    rect(d, (1, 7, 14, 15), PINK)
    rect(d, (1, 4, 14, 8), CREAM)
    line(d, [(2, 11), (13, 11)], PINK2)
    rect(d, (7, 0, 8, 3), RED, RED)
    return im


def bee_img():
    im, d = canvas(14, 11)
    ell(d, (4, 0, 9, 4), WHITE)
    ell(d, (1, 3, 12, 10), YELLOW)
    line(d, [(5, 3), (5, 10)], DARK)
    line(d, [(8, 3), (8, 10)], DARK)
    d.point((11, 6), fill=DARK)
    return im


def ball_img(s=12):
    im, d = canvas(s, s)
    ell(d, (0, 0, s - 1, s - 1), BLUE)
    line(d, [(1, s // 2), (s - 2, s // 2)], CREAM)
    d.point((3, 3), fill=WHITE)
    return im


def coin_img():
    im, d = canvas(9, 9)
    ell(d, (0, 0, 8, 8), YELLOW, GOLD2)
    line(d, [(4, 2), (4, 6)], GOLD2)
    return im


def heart_img(col=RED):
    im, d = canvas(9, 8)
    rows = ["01100110", "11111111", "11111111", "11111111", "01111110", "00111100", "00011000"]
    for y, r in enumerate(rows):
        for x, c in enumerate(r):
            if c == "1":
                d.point((x, y), fill=col)
    d.point((1, 1), fill=PINK)
    return im


def gift_img():
    im, d = canvas(14, 14)
    rect(d, (0, 4, 13, 13), LILAC)
    rect(d, (0, 4, 13, 7), LILAC2)
    line(d, [(6, 4), (6, 13)], YELLOW)
    line(d, [(7, 4), (7, 13)], YELLOW)
    ell(d, (2, 0, 6, 4), YELLOW, GOLD2)
    ell(d, (7, 0, 11, 4), YELLOW, GOLD2)
    return im


def bubble_img():
    im, d = canvas(10, 10)
    ell(d, (0, 0, 9, 9), (0xD8, 0xF0, 0xFC), WATER2)
    d.point((3, 3), fill=WHITE)
    return im


def note_img():
    im, d = canvas(10, 12)
    ell(d, (0, 7, 5, 11), DARK, DARK)
    line(d, [(5, 1), (5, 9)], DARK)
    line(d, [(5, 1), (9, 3)], DARK, 2)
    return im


def zzz_img():
    im, d = canvas(8, 8)
    line(d, [(0, 0), (7, 0)], BLUE2)
    line(d, [(7, 0), (0, 7)], BLUE2)
    line(d, [(0, 7), (7, 7)], BLUE2)
    return im


def balloon_img(col, col2):
    im, d = canvas(36, 46)
    ell(d, (1, 1, 34, 38), col)
    d.ellipse((7, 6, 13, 14), fill=WHITE)
    poly(d, [(15, 38), (20, 38), (17, 42)], col2)
    line(d, [(17, 42), (19, 45)], OL)
    return im


# --- dress-up items for the 64x64 pet bear ----------------------------------------------
def pet_shades():
    im, d = canvas(28, 8)
    rect(d, (1, 1, 11, 7), DARK, BLACK)
    rect(d, (16, 1, 26, 7), DARK, BLACK)
    line(d, [(0, 2), (27, 2)], BLACK)
    d.point((3, 3), fill=GREY)
    d.point((18, 3), fill=GREY)
    return im


def pet_shades_side():
    im, d = canvas(10, 7)
    rect(d, (2, 1, 9, 6), DARK, BLACK)
    line(d, [(0, 2), (3, 2)], BLACK)
    d.point((4, 3), fill=GREY)
    return im


def pet_partyhat():
    im, d = canvas(20, 24)
    poly(d, [(1, 22), (18, 22), (10, 2)], PINK)
    line(d, [(5, 14), (14, 18)], YELLOW, 2)
    line(d, [(8, 8), (13, 10)], BLUE, 2)
    ell(d, (7, 0, 12, 5), YELLOW, GOLD2)
    return im


def pet_crown():
    im, d = canvas(24, 16)
    poly(d, [(1, 15), (22, 15), (22, 4), (17, 9), (12, 1), (7, 9), (1, 4)], YELLOW, GOLD2)
    d.point((12, 10), fill=RED)
    d.point((6, 12), fill=BLUE)
    d.point((18, 12), fill=GREEN)
    return im


# --- dress-up items for the talker photo (bear head ~216 px wide) -----------------------
def big_shades(style):
    im, d = canvas(120, 34)
    if style == "cool":
        poly(d, [(4, 6), (54, 6), (52, 26), (34, 32), (12, 28)], DARK, BLACK)
        poly(d, [(66, 6), (116, 6), (108, 28), (86, 32), (68, 26)], DARK, BLACK)
        rect(d, (52, 8, 68, 12), BLACK, BLACK)
        line(d, [(0, 7), (4, 7)], BLACK, 3)
        line(d, [(116, 7), (119, 7)], BLACK, 3)
        line(d, [(12, 11), (24, 11)], (0x70, 0x70, 0x88), 2)
        line(d, [(74, 11), (86, 11)], (0x70, 0x70, 0x88), 2)
    elif style == "hearts":
        for cx in (30, 90):
            d.ellipse((cx - 26, 2, cx - 1, 24), fill=PINK2, outline=OL)
            d.ellipse((cx + 1, 2, cx + 26, 24), fill=PINK2, outline=OL)
            poly(d, [(cx - 26, 15), (cx + 26, 15), (cx, 33)], PINK2)
            d.polygon([(cx - 24, 13), (cx + 24, 13), (cx, 30)], fill=PINK2)
            d.ellipse((cx - 18, 7, cx - 11, 12), fill=PINK)
        rect(d, (54, 10, 66, 13), RED2, RED2)
    elif style == "star":
        for cx in (30, 90):
            pts = []
            for i in range(10):
                a = -math.pi / 2 + i * math.pi / 5
                r = 17 if i % 2 == 0 else 8
                pts.append((cx + r * math.cos(a) * 1.4, 17 + r * math.sin(a)))
            poly(d, pts, YELLOW, GOLD2)
        rect(d, (52, 14, 68, 17), GOLD2, GOLD2)
    elif style == "specs":
        for cx in (32, 88):
            d.ellipse((cx - 22, 4, cx + 22, 30), outline=DARK, width=3)
            d.ellipse((cx - 14, 9, cx - 8, 13), fill=WHITE)
        line(d, [(54, 14), (66, 14)], DARK, 3)
    return im


def big_hat(kind):
    if kind == "party":
        im, d = canvas(70, 92)
        poly(d, [(4, 88), (66, 88), (36, 12)], PINK)
        for y, c in ((70, YELLOW), (50, BLUE), (32, GREEN)):
            w = (y - 12) * 30 // 76
            line(d, [(36 - w + 2, y), (36 + w - 2, y + 6)], c, 4)
        for i in range(7):
            ell(d, (32 + (i % 3) * 3 - 6, 2 + i, 40 + (i % 3) * 3 - 2, 12 + i), YELLOW, GOLD2)
        rect(d, (2, 86, 68, 91), YELLOW, GOLD2)
    elif kind == "top":
        im, d = canvas(110, 80)
        rect(d, (22, 2, 88, 66), DARK, BLACK)
        rect(d, (22, 50, 88, 62), RED, RED2)
        ell(d, (0, 62, 109, 79), DARK, BLACK)
        line(d, [(30, 8), (30, 46)], (0x50, 0x50, 0x60), 2)
    elif kind == "crown":
        im, d = canvas(110, 64)
        pts = [(4, 62), (106, 62), (106, 14), (84, 36), (72, 2), (55, 30), (38, 2), (26, 36), (4, 14)]
        poly(d, pts, YELLOW, GOLD2)
        rect(d, (4, 50, 106, 62), GOLD, GOLD2)
        for x, c in ((20, RED), (40, BLUE), (55, GREEN), (70, BLUE), (90, RED)):
            ell(d, (x - 4, 52, x + 4, 60), c, OL)
        for x, y in ((4, 14), (38, 2), (72, 2), (106, 14)):
            ell(d, (x - 4, y - 4, x + 4, y + 4), CREAM, GOLD2)
    elif kind == "cap":
        im, d = canvas(130, 60)
        d.pieslice((14, 4, 110, 100), 180, 360, fill=BLUE, outline=OL)
        line(d, [(62, 4), (62, 52)], BLUE2, 2)
        ell(d, (56, 0, 68, 8), RED, OL)
        poly(d, [(96, 46), (128, 50), (126, 58), (90, 56)], BLUE2)
        rect(d, (14, 48, 110, 56), BLUE2)
        ell(d, (40, 18, 56, 34), CREAM, OL)
        line(d, [(44, 26), (52, 26)], RED, 2)
    elif kind == "wizard":
        im, d = canvas(120, 110)
        poly(d, [(14, 98), (106, 98), (70, 4), (54, 30)], LILAC2)
        ell(d, (0, 92, 119, 109), LILAC2, OL)
        for x, y in ((50, 60), (70, 40), (78, 78), (40, 84)):
            for dx, dy in ((0, -3), (0, 3), (-3, 0), (3, 0), (0, 0)):
                d.point((x + dx, y + dy), fill=YELLOW)
        ell(d, (66, 0, 74, 8), YELLOW, GOLD2)
    elif kind == "flower":
        im, d = canvas(60, 56)
        for a in range(6):
            ang = a * math.pi / 3
            cx, cy = 30 + 15 * math.cos(ang), 28 + 15 * math.sin(ang)
            ell(d, (cx - 11, cy - 11, cx + 11, cy + 11), PINK, PINK2)
        ell(d, (19, 17, 41, 39), YELLOW, GOLD2)
    return im


def bowtie(kind):
    im, d = canvas(64, 32)
    col, col2 = {"red": (RED, RED2), "blue": (BLUE, BLUE2), "spots": (YELLOW, GOLD2)}[kind]
    poly(d, [(2, 2), (28, 12), (28, 20), (2, 30)], col)
    poly(d, [(62, 2), (36, 12), (36, 20), (62, 30)], col)
    rect(d, (27, 10, 37, 22), col2)
    if kind == "spots":
        for x, y in ((10, 10), (14, 20), (50, 10), (54, 20)):
            ell(d, (x - 2, y - 2, x + 2, y + 2), RED, RED)
    return im


def moustache():
    im, d = canvas(70, 22)
    for s in (-1, 1):
        pts = [(35, 6), (35 + s * 14, 2), (35 + s * 26, 6), (35 + s * 33, 1), (35 + s * 30, 12),
               (35 + s * 18, 16), (35, 12)]
        poly(d, pts, (0x4A, 0x30, 0x24), BLACK)
    return im


# --- Panel icons (24x24) ----------------------------------------------------------------
def icon(fn):
    im, d = canvas(24, 24)
    fn(im, d)
    return im


def paste(dst, src, x, y):
    dst.alpha_composite(src, (x, y))


def ic_feed(im, d):
    paste(im, honeypot(20), 2, 3)


def ic_wash(im, d):
    rect(d, (3, 12, 20, 20), PINK)
    rect(d, (5, 9, 18, 12), CREAM)
    for x, y, r in ((6, 5, 3), (13, 3, 2), (18, 6, 3)):
        ell(d, (x - r, y - r, x + r, y + r), (0xD8, 0xF0, 0xFC), WATER2)


def ic_nap(im, d):
    rect(d, (2, 14, 21, 19), BLUE)
    rect(d, (2, 10, 7, 14), CREAM)
    line(d, [(2, 8), (2, 21)], WOOD2, 2)
    line(d, [(21, 13), (21, 21)], WOOD2, 2)
    for x, y in ((11, 3), (16, 7)):
        line(d, [(x, y), (x + 4, y)], BLUE2)
        line(d, [(x + 4, y), (x, y + 4)], BLUE2)
        line(d, [(x, y + 4), (x + 4, y + 4)], BLUE2)


def ic_medi(im, d):
    rect(d, (6, 6, 17, 21), CREAM)
    rect(d, (8, 2, 15, 6), BLUE)
    rect(d, (10, 10, 13, 18), RED, RED)
    rect(d, (8, 12, 15, 15), RED, RED)


def ic_game(im, d):
    for (x, y, c) in ((2, 2, RED), (13, 2, GREEN), (2, 13, BLUE), (13, 13, YELLOW)):
        rect(d, (x, y, x + 8, y + 8), c)


def ic_dress(im, d):
    paste(im, pet_shades(), -2, 9)


def ic_go(im, d):
    rect(d, (10, 4, 13, 22), WOOD2)
    poly(d, [(3, 4), (19, 4), (22, 7), (19, 10), (3, 10)], CREAM)
    poly(d, [(20, 12), (4, 12), (1, 15), (4, 18), (20, 18)], TEAL)


def ic_menu(im, d):
    for i, (h, c) in enumerate(((10, RED), (16, YELLOW), (20, GREEN))):
        rect(d, (3 + i * 7, 22 - h, 8 + i * 7, 22), c)


def ic_home(im, d):
    poly(d, [(2, 11), (12, 2), (22, 11)], LILAC2)
    rect(d, (5, 11, 19, 22), LILAC)
    rect(d, (10, 15, 14, 22), OL, OL)


def ic_garden(im, d):
    line(d, [(12, 12), (12, 22)], GREEN, 2)
    poly(d, [(12, 18), (6, 14), (12, 16)], GREEN)
    for a in range(5):
        ang = a * 2 * math.pi / 5
        cx, cy = 12 + 5 * math.cos(ang), 8 + 5 * math.sin(ang)
        ell(d, (cx - 3, cy - 3, cx + 3, cy + 3), PINK)
    ell(d, (9, 5, 15, 11), YELLOW, GOLD2)


def ic_park(im, d):
    rect(d, (10, 13, 13, 22), WOOD2)
    ell(d, (2, 1, 21, 17), LEAF)
    ell(d, (6, 5, 10, 9), LEAF2, LEAF2)


def ic_shop(im, d):
    rect(d, (3, 8, 20, 22), PINK)
    d.arc((7, 2, 16, 14), 180, 360, fill=OL, width=2)
    line(d, [(6, 12), (17, 12)], PINK2)


def ic_hi(im, d):
    ell(d, (1, 2, 22, 16), WHITE)
    poly(d, [(5, 14), (10, 15), (4, 21)], WHITE)
    d.line([(5, 15), (9, 15)], fill=WHITE)
    for x in (7, 11, 15):
        d.point((x, 9), fill=OL)


def ic_together(im, d):
    paste(im, heart_img(), 2, 6)
    paste(im, heart_img(PINK2), 12, 10)


def ic_swing(im, d):
    line(d, [(2, 2), (21, 2)], LILAC2, 2)
    line(d, [(7, 2), (5, 18)])
    line(d, [(17, 2), (15, 18)])
    rect(d, (3, 18, 17, 21), WOOD)


def ic_catch(im, d):
    paste(im, honeypot(14), 5, 0)
    poly(d, [(9, 15), (15, 15), (15, 18), (18, 18), (12, 23), (6, 18), (9, 18)], GREEN)


def ic_fetch(im, d):
    paste(im, ball_img(14), 5, 5)


def ic_dance(im, d):
    paste(im, note_img(), 2, 2)
    paste(im, note_img(), 12, 10)


def ic_back(im, d):
    poly(d, [(2, 12), (12, 3), (12, 8), (21, 8), (21, 16), (12, 16), (12, 21)], CREAM)


def ic_picnic(im, d):
    rect(d, (2, 12, 21, 21), RED)
    for x in range(2, 21, 4):
        line(d, [(x, 12), (x, 21)], CREAM)
    paste(im, apple_img(), 6, 0)


def ic_save(im, d):
    rect(d, (3, 3, 20, 20), BLUE)
    rect(d, (7, 3, 16, 9), CREAM)
    rect(d, (6, 13, 17, 20), WHITE)


def ic_quit(im, d):
    rect(d, (5, 3, 18, 21), WOOD)
    rect(d, (8, 6, 15, 21), OL, OL)
    d.point((14, 13), fill=YELLOW)


def ic_tick(im, d):
    line(d, [(3, 12), (9, 19), (20, 4)], GREEN, 4)


def ic_voice(im, d):
    poly(d, [(3, 9), (8, 9), (13, 4), (13, 20), (8, 15), (3, 15)], CREAM)
    d.arc((10, 6, 20, 18), 300, 60, fill=OL, width=2)


def ic_tickle(im, d):
    line(d, [(4, 20), (18, 4)], WOOD2, 2)
    poly(d, [(16, 2), (22, 2), (22, 8)], PINK)


def item_icon(img):
    im, d = canvas(24, 24)
    paste(im, img, (24 - img.width) // 2, (24 - img.height) // 2)
    return im


ICONS = {
    "feed": ic_feed, "wash": ic_wash, "nap": ic_nap, "medi": ic_medi, "game": ic_game,
    "dress": ic_dress, "go": ic_go, "menu": ic_menu, "home": ic_home, "garden": ic_garden,
    "park": ic_park, "shop": ic_shop, "hi": ic_hi, "together": ic_together, "swing": ic_swing,
    "catch": ic_catch, "fetch": ic_fetch, "dance": ic_dance, "back": ic_back, "picnic": ic_picnic,
    "save": ic_save, "quit": ic_quit, "tick": ic_tick, "voice": ic_voice, "tickle": ic_tickle,
}


def space_back():
    im, d = canvas(228, 216, (0x10, 0x10, 0x2C))
    rnd = random.Random(3)
    for y in range(0, 216, 2):
        if y > 150:
            d.line([(0, y), (227, y)], fill=(0x18, 0x14, 0x3C))
    for _ in range(90):
        x, y = rnd.randrange(228), rnd.randrange(216)
        d.point((x, y), fill=rnd.choice((WHITE, YELLOW, SKY2, CREAM)))
    for x, y in ((30, 40), (190, 30), (200, 150), (20, 170)):
        for dx, dy in ((0, -2), (0, 2), (-2, 0), (2, 0), (0, -1), (0, 1), (-1, 0), (1, 0), (0, 0)):
            d.point((x + dx, y + dy), fill=WHITE)
    ell(d, (170, 160, 220, 210), ORANGE, OL)
    d.arc((158, 178, 232, 194), 160, 380, fill=YELLOW, width=2)
    ell(d, (180, 170, 190, 178), YELLOW, ORANGE)
    ell(d, (12, 12, 40, 40), GREY, OL)
    ell(d, (20, 20, 26, 26), (0x90, 0x90, 0xA0), (0x90, 0x90, 0xA0))
    return im


FONT = "/usr/share/fonts/truetype/ubuntu/Ubuntu-B.ttf"


def outlined_text(im, xy, text, size, fill, outline=OL, width=3, anchor="mt"):
    from PIL import ImageFont
    try:
        font = ImageFont.truetype(FONT, size)
    except OSError:
        font = ImageFont.load_default(size)
    d = ImageDraw.Draw(im)
    d.text(xy, text, font=font, fill=fill, anchor=anchor, stroke_width=width, stroke_fill=outline)


def title():
    """The Pet's title screen (320x256): the owner's photo of four AG Bears under a big title."""
    im, d = canvas(320, 256, SKY)
    for y in range(0, 150):                       # sky getting lighter towards the horizon
        t = y / 150
        c = tuple(round(SKY2[i] * (1 - t) + CREAM[i] * t * 0.35 + SKY2[i] * t * 0.65) for i in range(3))
        d.line([(0, y), (319, y)], fill=c)
    for a in range(0, 360, 20):                   # sun rays behind the title
        ang = math.radians(a)
        d.polygon([(160, 40), (160 + 260 * math.cos(ang), 40 + 260 * math.sin(ang)),
                   (160 + 260 * math.cos(ang + 0.17), 40 + 260 * math.sin(ang + 0.17))], fill=(0xC8, 0xEC, 0xF6))
    cloud(d, 14, 70, 0.9)
    cloud(d, 262, 82, 0.8)
    d.ellipse((-60, 150, 380, 330), fill=GRASS, outline=OL)
    rnd = random.Random(11)
    for _ in range(40):
        flower(d, rnd.randrange(4, 316), rnd.randrange(200, 254), rnd.choice((PINK, CREAM, RED, LILAC)))
    group = Image.open(ART / "photos" / "bear-group.png").convert("RGBA")
    w = 292
    group = group.resize((w, round(group.height * w / group.width)), Image.LANCZOS)
    im.alpha_composite(group, ((320 - w) // 2, 256 - group.height - 14))
    outlined_text(im, (160, 4), "AG BEAR", 46, YELLOW, OL, 4)
    outlined_text(im, (160, 50), "& FRIENDS", 20, PINK, OL, 3)
    outlined_text(im, (160, 241), "Click or press fire to play!", 11, WHITE, OL, 2)
    return im


def seal_frames():
    """Sid the Seal for the park pond, from the owner's photo: closed and open mouth (64x64)."""
    src = Image.open(ART / "photos" / "seal.png").convert("RGBA")
    w = 64
    s = src.resize((w, round(src.height * w / src.width)), Image.LANCZOS)
    out = []
    for opened in (False, True):
        f = Image.new("RGBA", (64, 64), CLEAR)
        img = s.copy()
        if opened:
            x0, y0, x1, y1 = 12, 30, 28, 38
            jaw = img.crop((x0, y0, x1, y1))
            ImageDraw.Draw(img).ellipse((x0 + 2, y0 - 1, x1 - 2, y0 + 4), fill=(64, 24, 24, 255))
            img.alpha_composite(jaw, (x0, y0 + 2))
        f.alpha_composite(img, (0, 64 - img.height))
        out.append(f)
    return out


PARK_BEARS = ["starfish", "fudgie", "grandmaw", "hulihoop"]


def park_bear_frames(name):
    """A Talker character from the owner's photos as a park friend: closed and open mouth (64x64),
    the jaw taken from tools/characters.py (given there for a 180 pixel wide picture)."""
    import sys
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from characters import CHARACTERS
    c = next(c for c in CHARACTERS if c["name"] == name)
    src = Image.open(ART / "photos" / c["file"]).convert("RGBA").crop(c["crop"])
    k = 64 / max(src.width, src.height)
    s = src.resize((max(1, round(src.width * k)), max(1, round(src.height * k))), Image.LANCZOS)
    j = (64 / max(src.width, src.height)) / (180 / src.width)   # 180-wide coordinates -> 64 box
    x0, y0, x1, y1 = (round(v * j) for v in c["jaw"])
    d = max(2, round(c["drop"] * j))
    out = []
    for opened in (False, True):
        f = Image.new("RGBA", (64, 64), CLEAR)
        img = s.copy()
        if opened:
            jaw = img.crop((x0, y0, x1, y1))
            ImageDraw.Draw(img).ellipse((x0 + 1, y0 - 1, x1 - 1, y0 + d * 2), fill=(64, 24, 24, 255))
            img.alpha_composite(jaw, (x0, y0 + d))
        f.alpha_composite(img, ((64 - img.width) // 2, 64 - img.height))
        out.append(f)
    return out


def build():
    ART.mkdir(exist_ok=True)
    out = {}
    out["scene-park"] = park()
    out["scene-shop"] = shop()
    out["prop-honey"] = honeypot(16)
    out["prop-apple"] = apple_img()
    out["prop-cake"] = cake_img()
    out["prop-bee"] = bee_img()
    out["prop-ball"] = ball_img()
    out["prop-coin"] = coin_img()
    out["prop-heart"] = heart_img()
    out["prop-gift"] = gift_img()
    out["prop-bubble"] = bubble_img()
    out["prop-note"] = note_img()
    out["prop-zzz"] = zzz_img()
    for name, c1, c2 in (("red", RED, RED2), ("green", GREEN, LEAF), ("blue", BLUE, BLUE2),
                         ("yellow", YELLOW, GOLD2)):
        out[f"balloon-{name}"] = balloon_img(c1, c2)
        out[f"balloon-{name}-lit"] = balloon_img(tuple(min(255, v + 70) for v in c1), c2)
    out["pet-shades"] = pet_shades()
    out["pet-shades-side"] = pet_shades_side()
    out["pet-shades-sidel"] = pet_shades_side().transpose(Image.FLIP_LEFT_RIGHT)
    out["pet-partyhat"] = pet_partyhat()
    out["pet-crown"] = pet_crown()
    for s in ("cool", "hearts", "star", "specs"):
        out[f"talk-glasses-{s}"] = big_shades(s)
    for h in ("party", "top", "crown", "cap", "wizard", "flower"):
        out[f"talk-hat-{h}"] = big_hat(h)
    for b in ("red", "blue", "spots"):
        out[f"talk-bow-{b}"] = bowtie(b)
    out["talk-moustache"] = moustache()
    out["talk-back-space"] = space_back()
    out["title"] = title()
    out["seal-0"], out["seal-1"] = seal_frames()
    for n in PARK_BEARS:
        out[f"{n}-0"], out[f"{n}-1"] = park_bear_frames(n)
    for name, fn in ICONS.items():
        out[f"icon-{name}"] = icon(fn)
    for name in ("honey", "apple", "cake", "ball", "gift"):
        out[f"icon-item-{name}"] = item_icon(out[f"prop-{name}"])
    out["icon-item-shades"] = item_icon(pet_shades())
    out["icon-item-partyhat"] = item_icon(pet_partyhat().resize((15, 18), Image.NEAREST))
    out["icon-item-crown"] = item_icon(pet_crown())
    out["icon-item-medi"] = icon(ic_medi)
    for name, im in out.items():
        im.save(ART / f"{name}.png")
    return out


def preview(out):
    names = sorted(out)
    cols, cell = 8, 0
    big = [n for n in names if n.startswith("scene-")]
    small = [n for n in names if not n.startswith("scene-")]
    W = 640
    y = 0
    sheet = Image.new("RGBA", (W, 2000), (0x99, 0x99, 0x99, 255))
    x = 0
    for n in big:
        sheet.alpha_composite(out[n], (x, y))
        x += 320
    y += 204
    x = 0
    rowh = 0
    for n in small:
        im = out[n].resize((out[n].width * 2, out[n].height * 2), Image.NEAREST)
        if x + im.width > W:
            x = 0
            y += rowh + 4
            rowh = 0
        sheet.alpha_composite(im, (x, y))
        x += im.width + 4
        rowh = max(rowh, im.height)
    sheet = sheet.crop((0, 0, W, y + rowh + 4))
    sheet.save(ART / "preview.png")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--preview", action="store_true")
    a = ap.parse_args()
    o = build()
    if a.preview:
        preview(o)
    print(f"wrote {len(o)} images to {ART}")
