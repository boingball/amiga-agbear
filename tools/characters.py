"""The Talker's characters, from the owner's photos in art/photos (cut-outs with alpha).

Coordinates are in the scaled picture (TALK_PHOTO_W wide).  jaw = (x0, y0, x1, y1): the part
under the lips that drops by `drop` pixels to open the mouth; the gap is filled with mouth colour.
hat = (centre x, bottom y), glasses/bow/tash = centre.  The original AG Bear photo has real mouth
pictures and is handled in mkassets.py."""
CHARACTERS = [
    dict(name="party", file="party-bear.png", crop=(120, 0, 1714, 1900),
         jaw=(60, 158, 118, 186), drop=6, hat=(102, 100), glasses=(86, 126), tash=(86, 150), bow=(88, 196)),
    dict(name="prof", file="professor-bear.png", crop=(0, 0, 1453, 1655),
         jaw=(86, 84, 138, 106), drop=6, hat=(113, 22), glasses=(118, 40), tash=(112, 72), bow=(110, 126)),
    dict(name="ted", file="ted-bear.png", crop=(0, 0, 2316, 2570),
         jaw=(58, 60, 106, 86), drop=6, hat=(88, 8), glasses=(84, 24), tash=(80, 52), bow=(84, 96)),
    dict(name="seal", file="seal.png", crop=(0, 0, 1378, 1206),
         jaw=(34, 84, 74, 100), drop=5, hat=(84, 10), glasses=(50, 64), tash=(52, 80), bow=(80, 130)),
    dict(name="starfish", file="starfish-bear.png", crop=(0, 0, 2576, 2536),
         jaw=(52, 52, 92, 66), drop=6, hat=(72, 12), glasses=(68, 24), tash=(68, 46), bow=(72, 72)),
]
