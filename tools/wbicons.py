#!/usr/bin/env python3
"""AG Bear's Workbench icons, drawn the same way as AmiKart's, AmiPool's,
The Machine's and GeoDash's (tools/wbicons.py there).

Each .info file carries a classic 4-colour planar image (grey, black, white,
blue) that every Workbench shows, and an OS 3.5 colour image (an IFF FORM
ICON appended to the file) that AmigaOS 3.5, 3.9, 3.2 and AROS show instead.
Both have a selected (clicked) state.

  AGBearPet.info      the pet: AG Bear's face with a heart (clicked: he winks
                      and the heart grows)
  AGBearTalker.info   the talker: AG Bear with a speech bubble (clicked: his
                      mouth opens and the bubble fills with words)
  AGBear-drawer.info  the drawer, with AG Bear's face on the front
  ReadMe.info         a page with a honey-pot emblem, opened with MultiView

  tools/wbicons.py --write DIR     the icons into DIR (wbicons/ in the repo)
  tools/wbicons.py --preview FILE  a PNG contact sheet"""
import argparse
import math
import struct
import zlib
from pathlib import Path

WB_GREY, WB_BLACK, WB_WHITE, WB_BLUE = 0, 1, 2, 3
CLASSIC_RGB = [(0xAA, 0xAA, 0xAA), (0x00, 0x00, 0x00),
               (0xFF, 0xFF, 0xFF), (0x66, 0x88, 0xBB)]

ROLES = {
    # role: (rgb, classic pen or (pen, pen) checker)
    ".":  ((0x99, 0x99, 0x99), WB_GREY),   # transparent
    "K":  ((0x1C, 0x1C, 0x26), WB_BLACK),  # outline
    "W":  ((0xFF, 0xFF, 0xFF), WB_WHITE),
    "WS": ((0xC8, 0xCC, 0xD4), WB_GREY),
    "bH": ((0xB0, 0x8C, 0x6C), WB_WHITE),  # AG Bear's fur
    "b":  ((0x7C, 0x5C, 0x44), WB_GREY),
    "bS": ((0x4C, 0x36, 0x28), (WB_GREY, WB_BLACK)),
    "mH": ((0xF0, 0xD8, 0xB0), WB_WHITE),  # muzzle
    "m":  ((0xD8, 0xB4, 0x84), WB_WHITE),
    "mS": ((0xA8, 0x84, 0x5C), WB_GREY),
    "rH": ((0xFF, 0x8C, 0x8C), WB_WHITE),  # red cap, heart
    "r":  ((0xD8, 0x30, 0x34), WB_BLUE),
    "rS": ((0x8C, 0x18, 0x1C), WB_BLACK),
    "yH": ((0xFF, 0xF0, 0xA0), WB_WHITE),  # honey
    "y":  ((0xF0, 0xB8, 0x30), WB_GREY),
    "yS": ((0xA8, 0x74, 0x10), WB_BLUE),
    "fH": ((0xFF, 0xEE, 0xA8), WB_WHITE),  # folder front
    "f":  ((0xF2, 0xC0, 0x44), WB_BLUE),
    "fS": ((0xBE, 0x84, 0x1E), (WB_BLUE, WB_BLACK)),
    "fB": ((0xC8, 0x8E, 0x24), (WB_BLUE, WB_BLACK)),  # folder back
    "fD": ((0x8C, 0x5C, 0x12), WB_BLACK),  # inside of the open folder
    "p":  ((0xFA, 0xFA, 0xF4), WB_WHITE),  # paper
    "pS": ((0xC6, 0xC6, 0xBC), WB_GREY),
    "pL": ((0x90, 0x96, 0xA4), WB_BLUE),   # ruled lines on paper
    "sH": ((0xFF, 0xFF, 0xFF), WB_WHITE),  # speech bubble
    "s":  ((0xEC, 0xF4, 0xFF), WB_WHITE),
    "sS": ((0xB4, 0xC4, 0xDC), WB_GREY),
    "t":  ((0x5C, 0x8C, 0xD8), WB_BLUE),   # words in the bubble
}
ROLE_ORDER = list(ROLES)
ROLE_INDEX = {role: i for i, role in enumerate(ROLE_ORDER)}


class Canvas:
    def __init__(self, width, height):
        self.width = width
        self.height = height
        self.px = [["."] * width for _ in range(height)]

    def set(self, x, y, role):
        if 0 <= x < self.width and 0 <= y < self.height:
            self.px[y][x] = role

    def cells(self, inside):
        return {(x, y) for y in range(self.height) for x in range(self.width)
                if inside(x + 0.5, y + 0.5)}

    def shape(self, inside, hi, body, sh, outline="K"):
        """Fill a shape with a 1-pixel outline and a top-left light bevel."""
        cells = self.cells(inside)
        edge = {(x, y) for (x, y) in cells
                if any(n not in cells for n in
                       ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1)))}
        inner = cells - edge if outline else cells
        for (x, y) in cells:
            if outline and (x, y) in edge:
                role = outline
            elif (x - 1, y) not in inner or (x, y - 1) not in inner:
                role = hi
            elif (x + 1, y) not in inner or (x, y + 1) not in inner:
                role = sh
            else:
                role = body
            self.set(x, y, role)

    def fill(self, inside, role):
        for (x, y) in self.cells(inside):
            self.set(x, y, role)

    def line(self, x0, y0, x1, y1, role):
        steps = max(abs(x1 - x0), abs(y1 - y0), 1)
        for i in range(steps + 1):
            self.set(round(x0 + (x1 - x0) * i / steps),
                     round(y0 + (y1 - y0) * i / steps), role)

    def ball(self, cx, cy, radius, colour="r"):
        """A shaded pool ball lit from the top left (r, y, K for the 8, W)."""
        light = (-0.55, -0.65)

        def inside(x, y):
            return (x - cx) ** 2 + (y - cy) ** 2 <= radius ** 2

        cells = self.cells(inside)
        for (x, y) in cells:
            if any(n not in cells for n in
                   ((x - 1, y), (x + 1, y), (x, y - 1), (x, y + 1))):
                self.set(x, y, "K")
                continue
            nx = (x + 0.5 - cx) / radius
            ny = (y + 0.5 - cy) / radius
            lit = nx * light[0] + ny * light[1]
            if colour == "K":
                role = "WS" if lit > 0.7 else "K"
            elif colour == "W":
                role = "W" if lit > -0.35 else "WS"
            else:
                role = (colour + "H" if lit > 0.45 else colour
                        if lit > -0.35 else colour + "S")
            self.set(x, y, role)
        if colour == "K":
            self.fill(disc(cx + 0.4, cy + 0.4, max(0.9, radius * 0.3)), "W")
        elif colour != "W":
            self.set(int(cx - radius * 0.45), int(cy - radius * 0.5), "W")


def disc(cx, cy, r):
    return lambda x, y: (x - cx) ** 2 + (y - cy) ** 2 <= r * r


def gear(cx, cy, root, tip, teeth, phase=0.0, hole=0.0):
    def inside(x, y):
        dx, dy = x - cx, y - cy
        r = math.hypot(dx, dy)
        if r < hole or r > tip:
            return False
        if r <= root:
            return True
        turn = (math.atan2(dy, dx) / (2 * math.pi) * teeth + phase) % 1.0
        # Teeth taper a little towards the tip.
        half = 0.26 - 0.06 * (r - root) / (tip - root)
        return abs(turn - 0.5) < half
    return inside


def plank(x0, y0, x1, y1, half):
    length = math.hypot(x1 - x0, y1 - y0)
    ux, uy = (x1 - x0) / length, (y1 - y0) / length

    def inside(x, y):
        along = (x - x0) * ux + (y - y0) * uy
        across = -(x - x0) * uy + (y - y0) * ux
        return 0 <= along <= length and abs(across) <= half
    return inside


def polygon(points):
    def inside(x, y):
        hit = False
        j = len(points) - 1
        for i in range(len(points)):
            xi, yi = points[i]
            xj, yj = points[j]
            if (yi > y) != (yj > y) and \
                    x < (xj - xi) * (y - yi) / (yj - yi) + xi:
                hit = not hit
            j = i
        return hit
    return inside


def union(*shapes):
    return lambda x, y: any(s(x, y) for s in shapes)



# ---- The icons -----------------------------------------------------------

def bear_head(c, cx, cy, s, wink=False, mouth=False, cap=True):
    """AG Bear's face: round ears, brown fur, a tan muzzle and his red cap."""
    for ex in (-1, 1):
        c.shape(disc(cx + ex * 9.5 * s, cy - 8 * s, 4.6 * s), "bH", "b", "bS")
        c.fill(disc(cx + ex * 9.5 * s, cy - 8 * s, 2.0 * s), "mS")
    c.shape(disc(cx, cy, 11.5 * s), "bH", "b", "bS")
    c.shape(polygon([(cx - 6.5 * s, cy + 2 * s), (cx - 7 * s, cy + 7 * s), (cx - 3 * s, cy + 10.5 * s),
                     (cx + 3 * s, cy + 10.5 * s), (cx + 7 * s, cy + 7 * s), (cx + 6.5 * s, cy + 2 * s)]),
            "mH", "m", "mS")
    c.fill(disc(cx, cy + 3.2 * s, 1.9 * s), "K")                        # nose
    c.set(round(cx - 0.6 * s), round(cy + 2.4 * s), "WS")
    if mouth:
        c.fill(polygon([(cx - 3 * s, cy + 6 * s), (cx + 3 * s, cy + 6 * s), (cx + 1.8 * s, cy + 9.2 * s),
                        (cx - 1.8 * s, cy + 9.2 * s)]), "rS")
    else:
        c.line(round(cx - 2.5 * s), round(cy + 6.6 * s), round(cx + 2.5 * s), round(cy + 6.6 * s), "K")
    # eyes
    for ex in (-1, 1):
        x, y = round(cx + ex * 4.6 * s), round(cy - 2.6 * s)
        if wink and ex == 1:
            c.line(x - 1, y, x + 1, y, "K")
        else:
            c.fill(disc(x + 0.5, y + 0.5, 1.4 * s), "K")
            c.set(x, y - 1 if s > 1 else y, "W")
    if cap:
        c.shape(polygon([(cx - 7 * s, cy - 9 * s), (cx - 5 * s, cy - 13 * s), (cx, cy - 14.5 * s),
                         (cx + 5 * s, cy - 13 * s), (cx + 7 * s, cy - 9 * s)]), "rH", "r", "rS")


def heart(c, cx, cy, s):
    c.shape(union(disc(cx - 2.6 * s, cy - 1.5 * s, 2.9 * s), disc(cx + 2.6 * s, cy - 1.5 * s, 2.9 * s),
                  polygon([(cx - 5.4 * s, cy - 0.5 * s), (cx + 5.4 * s, cy - 0.5 * s), (cx, cy + 5.5 * s)])),
            "rH", "r", "rS")


def draw_pet(selected):
    """The pet: AG Bear's face and a heart.  Clicked, he winks and the heart grows."""
    c = Canvas(46, 40)
    bear_head(c, 19, 23, 1.35, wink=selected)
    if selected:
        heart(c, 38, 9, 1.25)
    else:
        heart(c, 38, 10, 0.95)
    return c


def draw_talker(selected):
    """The talker: AG Bear with a speech bubble.  Clicked, he talks: mouth open, words in the bubble."""
    c = Canvas(48, 40)
    bear_head(c, 16, 25, 1.25, mouth=selected)
    bubble = union(polygon([(27, 2), (46, 2), (46, 16), (27, 16)]), polygon([(29, 14), (34, 14), (27, 21)]))
    c.shape(bubble, "sH", "s", "sS")
    if selected:
        for y, x1 in ((6, 42), (9, 43), (12, 37)):
            c.line(30, y, x1, y, "t")
    else:
        for x in (32, 36, 40):
            c.fill(disc(x + 0.5, 9.5, 1.1), "t")
    return c


def draw_folder(selected, emblem):
    """A manila folder with papers poking out and an emblem on the front
    (the same folder as AmiKart's, The Machine's and GeoDash's drawers)."""
    c = Canvas(46, 36)
    back = union(polygon([(1, 7), (1, 34), (44, 34), (44, 7)]),
                 polygon([(1, 8), (3, 3), (16, 3), (18, 8)]))
    c.shape(back, "fB", "fB", "fB")
    if selected:
        c.fill(polygon([(3, 9), (3, 32), (42, 32), (42, 9)]), "fD")
    top = 1 if selected else 6
    c.shape(polygon([(5, top + 3), (5, 30), (35, 30), (35, top + 3)]),
            "p", "pS", "pS")
    c.shape(polygon([(10, top), (10, 30), (41, 30), (41, top)]),
            "p", "p", "pS")
    for y in range(top + 3, 18, 3):
        c.line(13, y, 38, y, "pL")
    if selected:
        front = polygon([(1, 35.5), (6, 16), (44.5, 16), (44.5, 35.5)])
    else:
        front = polygon([(1, 35.5), (1, 12), (44.5, 12), (44.5, 35.5)])
    c.shape(front, "fH", "f", "fS")
    if emblem:
        emblem(c, selected)
    return c


def emblem_bear(c, selected):
    """AG Bear's face: the drawer's emblem."""
    bear_head(c, 23, 25 if selected else 26, 0.62, cap=True)


def draw_paper(selected):
    c = Canvas(34, 40)
    c.shape(polygon([(3, 1), (3, 39), (31, 39), (31, 9), (23, 1)]),
            "p", "p", "pS")
    c.shape(polygon([(23, 1), (23, 9.5), (31, 9.5)]), "p", "pS", "pS")
    for i, y in enumerate(range(6, 36, 3)):
        right = 19 if y < 10 else 27 - (5 if i % 3 == 2 else 0)
        c.line(7, y, right if y < 25 else 17, y, "pL")
    y = 30 if selected else 31
    c.shape(union(disc(24, y + 1, 4.6), polygon([(20.5, y - 5), (27.5, y - 5), (27.5, y - 2), (20.5, y - 2)])),
            "yH", "y", "yS")                                            # honey pot
    return c


WBDISK, WBDRAWER, WBTOOL, WBPROJECT = 1, 2, 3, 4
NO_ICON_POSITION = 0x80000000
DDFLAGS_SHOWICONS, DDFLAGS_SHOWALL = 1, 2
DDVM_BYICON = 1


def classic_pen(role, x, y):
    pen = ROLES[role][1]
    if isinstance(pen, tuple):
        return pen[(x + y) & 1]
    return pen


def planar_image(canvas):
    """struct Image plus two bitplanes of classic 4-colour data."""
    w, h = canvas.width, canvas.height
    words = (w + 15) // 16
    header = struct.pack(">hhhhhIBBI", 0, 0, w, h, 2, 1, 3, 0, 0)
    planes = []
    for plane in range(2):
        data = bytearray()
        for y in range(h):
            row = [0] * (words * 16)
            for x in range(w):
                row[x] = (classic_pen(canvas.px[y][x], x, y) >> plane) & 1
            for i in range(0, len(row), 8):
                byte = 0
                for bit in row[i:i + 8]:
                    byte = (byte << 1) | bit
                data.append(byte)
        planes.append(bytes(data))
    return header + b"".join(planes)


def rle_encode(data):
    """OS 3.5 icon run-length encoding at 8 bits per entry (ByteRun1)."""
    out = bytearray()
    i = 0
    literal = bytearray()

    def flush():
        while literal:
            chunk = literal[:128]
            out.append(len(chunk) - 1)
            out.extend(chunk)
            del literal[:128]

    while i < len(data):
        run = 1
        while i + run < len(data) and data[i + run] == data[i] and run < 128:
            run += 1
        if run >= 3:
            flush()
            out.append(257 - run)
            out.append(data[i])
            i += run
        else:
            literal.extend(data[i:i + run])
            i += run
    flush()
    return bytes(out)


def rle_decode(data, bits, entries):
    """Port of Decode35() from AROS's icon.library, used by --check."""
    out = []
    pos = bitbuf = numbits = 0
    def take(count):
        nonlocal pos, bitbuf, numbits
        if numbits < count:
            bitbuf = ((bitbuf << 8) | data[pos]) & 0xFFFF
            pos += 1
            numbits += 8
        numbits -= count
        return (bitbuf >> numbits) & ((1 << count) - 1)

    while len(out) < entries:
        control = take(8)
        if control <= 127:
            out.extend(take(bits) for _ in range(control + 1))
        elif control > 128:
            out.extend([take(bits)] * (257 - control))
    return out[:entries]


def chunk(name, payload):
    data = name + struct.pack(">I", len(payload)) + payload
    return data + (b"\0" if len(payload) & 1 else b"")


def colour_images(normal, selected):
    """An IFF FORM ICON with FACE and one IMAG chunk per state."""
    used = sorted({role for canvas in (normal, selected)
                   for row in canvas.px for role in row} - {"."},
                  key=ROLE_INDEX.get)
    pens = ["."] + used
    index = {role: i for i, role in enumerate(pens)}
    palette = b"".join(bytes(ROLES[role][0]) for role in pens)
    face = struct.pack(">BBBBH", normal.width - 1, normal.height - 1,
                       0, 0, len(palette) - 1)
    body = b"ICON" + chunk(b"FACE", face)
    for canvas in (normal, selected):
        pixels = bytes(index[role] for row in canvas.px for role in row)
        packed = rle_encode(pixels)
        assert rle_decode(packed, 8, len(pixels)) == list(pixels)
        fmt = 1
        if len(packed) >= len(pixels):
            packed, fmt = pixels, 0
        imag = struct.pack(">BBBBBBHH", 0, len(pens) - 1, 0x03, fmt, 0, 8,
                           len(packed) - 1, len(palette) - 1)
        body += chunk(b"IMAG", imag + packed + palette)
    return chunk(b"FORM", body)


def bstring(text):
    raw = text.encode("latin-1") + b"\0"
    return struct.pack(">I", len(raw)) + raw


def info_file(kind, normal, selected, default_tool=None, position=None,
              window=None, drawer_flags=DDFLAGS_SHOWICONS, stack=0,
              tool_types=()):
    assert (normal.width, normal.height) == (selected.width, selected.height)
    x, y = position if position else (NO_ICON_POSITION, NO_ICON_POSITION)
    gadget = struct.pack(
        ">IhhhhHHHIIIiIHI",
        0, 0, 0, normal.width, normal.height,
        0x0006,          # GFLG_GADGIMAGE | GFLG_GADGHIMAGE
        0x0003,          # GACT_RELVERIFY | GACT_IMMEDIATE
        0x0001,          # GTYP_BOOLGADGET
        1, 1, 0, 0, 0, 0,
        1)               # UserData: WB_DISKREVISION
    diskobj = struct.pack(">HH", 0xE310, 1) + gadget + struct.pack(
        ">BBIIIIIII", kind, 0,
        1 if default_tool else 0, 1 if tool_types else 0,
        x & 0xFFFFFFFF, y & 0xFFFFFFFF,
        1 if window else 0, 0, stack)
    assert len(diskobj) == 78
    out = diskobj
    if window:
        left, top, width, height = window
        out += struct.pack(">hhhhBBIIIIIIIhhHHH",
                           left, top, width, height, 0xFF, 0xFF,
                           0, 0, 0, 0, 0, 0, 0, 90, 40, 0xFFFF, 0xFFFF, 1)
        out += struct.pack(">ii", 0, 0)
    out += planar_image(normal) + planar_image(selected)
    if default_tool:
        out += bstring(default_tool)
    if tool_types:
        # the array's size in bytes, its NULL included, then each string
        out += struct.pack(">I", (len(tool_types) + 1) * 4)
        for text in tool_types:
            out += bstring(text)
    if window:
        out += struct.pack(">IH", drawer_flags, DDVM_BYICON)
    return out + colour_images(normal, selected)



def folder(emblem):
    return lambda selected: draw_folder(selected, emblem)


ICONS = {
    "AGBearPet.info": (draw_pet, dict(kind=WBTOOL, stack=32768)),
    "AGBearTalker.info": (draw_talker, dict(kind=WBTOOL, stack=32768)),
    "AGBear-drawer.info": (folder(emblem_bear), dict(kind=WBDRAWER, window=(80, 40, 320, 110))),
    "ReadMe.info": (draw_paper, dict(kind=WBPROJECT, default_tool="SYS:Utilities/MultiView")),
}


def build_icon(name):
    draw, options = ICONS[name]
    return info_file(normal=draw(False), selected=draw(True), **options)


def write_icons(directory):
    out = Path(directory)
    out.mkdir(parents=True, exist_ok=True)
    for name in ICONS:
        data = build_icon(name)
        parse_info(data)
        (out / name).write_bytes(data)


# ---- Checking and previewing --------------------------------------------

def parse_info(data):
    """Read back the parts of a .info file that Workbench relies on."""
    magic, version = struct.unpack_from(">HH", data, 0)
    assert (magic, version) == (0xE310, 1), "bad magic"
    width, height = struct.unpack_from(">hh", data, 12)
    kind = data[48]
    has_tool, has_types, _, _, has_drawer = struct.unpack_from(">IIIII",
                                                               data, 50)
    pos = 78 + (56 if has_drawer else 0)
    for _ in range(2):
        w, h, depth = struct.unpack_from(">hhh", data, pos + 4)
        assert (w, h, depth) == (width, height, 2), "image size mismatch"
        pos += 20 + ((w + 15) // 16) * 2 * h * depth
    if has_tool:
        pos += 4 + struct.unpack_from(">I", data, pos)[0]
    if has_types:
        count = struct.unpack_from(">I", data, pos)[0] // 4 - 1
        pos += 4
        for _ in range(count):
            pos += 4 + struct.unpack_from(">I", data, pos)[0]
    if has_drawer:
        pos += 6
    assert data[pos:pos + 4] == b"FORM" and data[pos + 8:pos + 12] == b"ICON"
    end = pos + 8 + struct.unpack_from(">I", data, pos + 4)[0]
    assert end == len(data), "trailing bytes after FORM ICON"
    pos += 12
    images = []
    while pos < end:
        name = data[pos:pos + 4]
        size = struct.unpack_from(">I", data, pos + 4)[0]
        payload = data[pos + 8:pos + 8 + size]
        if name == b"FACE":
            fw, fh = payload[0] + 1, payload[1] + 1
            assert (fw, fh) == (width, height), "FACE size mismatch"
        elif name == b"IMAG":
            _, ncol, flags, fmt, _, depth, nimg, npal = struct.unpack_from(
                ">BBBBBBHH", payload)
            raw = payload[10:10 + nimg + 1]
            pixels = rle_decode(raw, depth, fw * fh) if fmt else list(raw)
            assert len(pixels) == fw * fh and max(pixels) <= ncol
            assert flags & 2 and npal + 1 == (ncol + 1) * 3
            pal = payload[11 + nimg:11 + nimg + npal + 1]
            images.append((pixels, [tuple(pal[i:i + 3])
                                    for i in range(0, len(pal), 3)]))
        pos += 8 + size + (size & 1)
    assert len(images) == 2
    return kind, width, height, images


def write_png(path, rows):
    raw = b"".join(b"\0" + bytes(v for px in row for v in px) for row in rows)
    height, width = len(rows), len(rows[0])
    png = b"\x89PNG\r\n\x1a\n"
    for name, payload in ((b"IHDR", struct.pack(">IIBBBBB", width, height,
                                                8, 2, 0, 0, 0)),
                          (b"IDAT", zlib.compress(raw, 9)), (b"IEND", b"")):
        png += struct.pack(">I", len(payload)) + name + payload
        png += struct.pack(">I", zlib.crc32(name + payload) & 0xFFFFFFFF)
    Path(path).write_bytes(png)


def preview(path, zoom=4):
    """Colour (top) and classic (bottom) images, normal and selected."""
    cell_w, cell_h, pad = 50, 44, 6
    grey = (0x99, 0x99, 0x99)
    width = (len(ICONS) * (cell_w + pad) + pad) * zoom
    height = (4 * (cell_h + pad) + pad) * zoom
    rows = [[grey] * width for _ in range(height)]

    def paint(x, y, rgb):
        for dy in range(zoom):
            rows[y * zoom + dy][x * zoom:(x + 1) * zoom] = [rgb] * zoom

    for col, name in enumerate(ICONS):
        draw = ICONS[name][0]
        _, w, h, images = parse_info(build_icon(name))
        tiles = [[[grey if pixels[y * w + x] == 0 else pal[pixels[y * w + x]]
                   for x in range(w)] for y in range(h)]
                 for pixels, pal in images]
        for state in (False, True):
            canvas = draw(state)
            tiles.append([[CLASSIC_RGB[classic_pen(canvas.px[y][x], x, y)]
                           for x in range(w)] for y in range(h)])
        for row, tile in enumerate(tiles):
            cx = pad + col * (cell_w + pad)
            cy = pad + row * (cell_h + pad)
            if row >= 2:
                for y in range(cy, cy + cell_h):
                    for x in range(cx, cx + cell_w):
                        paint(x, y, CLASSIC_RGB[WB_GREY])
            ox = cx + (cell_w - w) // 2
            oy = cy + (cell_h - h) // 2
            for y, line in enumerate(tile):
                for x, rgb in enumerate(line):
                    paint(ox + x, oy + y, rgb)
    write_png(path, rows)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--preview", metavar="PNG")
    parser.add_argument("--write", metavar="DIR")
    args = parser.parse_args()
    for name in ICONS:
        parse_info(build_icon(name))
    if args.write:
        write_icons(args.write)
        print("icons written to", args.write)
    if args.preview:
        preview(args.preview)
        print("icons ok; preview written to", args.preview)
