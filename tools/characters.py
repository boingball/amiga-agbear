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
    dict(name="fudgie", file="fudgie-arms.png", crop=(0, 0, 2576, 2320),
         jaw=(82, 82, 114, 96), drop=5, hat=(92, 10), glasses=(95, 46), tash=(98, 72), bow=(98, 102)),
    dict(name="grandmaw", file="grandmaw.png", crop=(0, 0, 2292, 2576),
         jaw=(58, 87, 92, 100), drop=6, hat=(72, 18), glasses=(66, 52), tash=(68, 80), bow=(74, 108)),
    dict(name="hulihoop", file="hulihoop.png", crop=(0, 0, 2322, 2576),
         jaw=(62, 89, 100, 102), drop=6, hat=(90, 22), glasses=(82, 46), tash=(80, 76), bow=(84, 118)),
    dict(name="longnose", file="long-nose.png", crop=(0, 0, 2380, 2576),
         jaw=(68, 80, 102, 94), drop=5, hat=(88, 10), glasses=(86, 46), tash=(90, 74), bow=(90, 100)),
    dict(name="tim", file="tim-cat.png", crop=(0, 0, 2443, 2299),
         jaw=(72, 128, 128, 142), drop=6, hat=(100, 34), glasses=(98, 96), tash=(98, 120), bow=(100, 160)),
]
