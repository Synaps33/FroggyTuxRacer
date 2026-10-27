#!/usr/bin/env python3
"""Convert the original Tux Racer 0.61 assets to compact GB300/SF2000 files.

The converter intentionally mirrors the original course loading rules:
  * SGI RGB elevation/terrain/tree maps
  * terrain/tree/item colours from tuxracer_init.tcl and courseinit.tcl
  * deterministic tree size variation

Outputs:
  assets/tex/*.tex   little-endian RGB565 / RGB565+A8 textures
  assets/course/*.trk compact course heightfield + object list
  assets/font/*.fnt glyph metrics from trebuchet.tfm
"""
from __future__ import annotations

import math
import os
import random
import re
import struct
import sys
from pathlib import Path

from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
SRC = ROOT / "upstream" / "tuxracer-data-0.61"
OUT = ROOT / "assets"

W, H = 320, 240
TEX_RGB565 = 0
TEX_RGB565A8 = 1

COURSES = [
    ("bunny_hill", "Bunny Hill", 40.0,
     "Use clever turning to conquer the Bunny Hill."),
    ("twisty_slope", "Twisty Slope", 40.0,
     "Tight twists make grabbing herring difficult. Hard turns will lead you to victory."),
    ("bumpy_ride", "Bumpy Ride", 40.0,
     "This hill has a series of ramps to tackle. Line yourself up before getting airborne."),
    ("frozen_river", "Frozen River", 80.0,
     "Keep your speed down to collect herring!"),
    ("path_of_daggers", "Path of Daggers", 70.0,
     "Big spikes may be the least of your worries. Multiple paths lead to victory."),
]

TREE_TYPES = [
    # name, marker colour, diameter, height, size variance, texture
    (255, 255, 255, 1.4, 2.5, 0.5, "tree1"),
    (255, 96, 0,   1.4, 2.5, 0.5, "tree2"),
    (0, 255, 48,   1.4, 1.0, 0.5, "tree3"),
]

ITEM_TYPES = [
    # name, marker colour, diameter, height, above_ground, flags
    ("herring", (28, 185, 204), 1.0, 1.0, 0.2, 0),
    ("flag",    (194, 40, 40),  1.0, 1.0, 0.0, 1),  # nocollision
    ("start",   (128, 128, 0),   9.0, 6.0, 0.0, 1),
    ("finish",  (255, 255, 0),   9.0, 6.0, 0.0, 1),
    ("float",   (255, 128, 255), 0.5, 0.5, 0.0, 2),  # reset point, not drawable
]

ITEM_HERRING, ITEM_FLAG, ITEM_START, ITEM_FINISH, ITEM_FLOAT = range(5)


def color_dist(p, c):
    return abs(p[0] - c[0]) + abs(p[1] - c[1]) + abs(p[2] - c[2])


def write_tex(name: str, image: Image.Image, alpha: bool, resize=None):
    image = image.convert("RGBA")
    if resize:
        image = image.resize(resize, Image.Resampling.LANCZOS)
    w, h = image.size
    path = OUT / "tex" / f"{name}.tex"
    with path.open("wb") as f:
        f.write(b"TEX1")
        f.write(struct.pack("<HHB", w, h, TEX_RGB565A8 if alpha else TEX_RGB565))
        rgb = bytearray(w * h * 2)
        if alpha:
            a = bytearray(w * h)
        for i, (r, g, b, aa) in enumerate(image.getdata()):
            p = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
            rgb[i * 2] = p & 0xFF
            rgb[i * 2 + 1] = p >> 8
            if alpha:
                a[i] = aa
        f.write(rgb)
        if alpha:
            f.write(a)
    return w, h


def load_tfm(path: Path):
    data = path.read_bytes()
    if data[:4] != b"\xfftfm":
        raise ValueError("bad tfm magic")
    endian = struct.unpack_from("<I", data, 4)[0]
    if endian == 0x78563412:
        e = ">"
    elif endian == 0x12345678:
        e = "<"
    else:
        raise ValueError("bad tfm endian")
    texw, texh, max_asc, max_desc = struct.unpack_from(e + "IIII", data, 8)
    n = struct.unpack_from(e + "I", data, 24)[0]
    off = 28
    chars = {}
    for _ in range(n):
        ch, w, h, xo, yo, kern, _dummy, xp, yp = struct.unpack_from(
            e + "HBBbbbbhh", data, off)
        off += 12
        chars[ch] = (w, h, xo, yo, kern, xp, yp)
    return texw, texh, max_asc, max_desc, chars


def write_font(name: str, font_img: Path, tfm_path: Path):
    texw, texh, asc, desc, chars = load_tfm(tfm_path)
    im = Image.open(font_img)
    write_tex(name, im, alpha=True)
    src_scale = max(1, texw // max(1, im.width))
    with (OUT / "font" / f"{name}.fnt").open("wb") as f:
        f.write(b"FNT1")
        f.write(struct.pack("<IIIII", texw, texh, asc, desc, src_scale))
        for ch in range(256):
            if ch in chars:
                w, h, xo, yo, kern, xp, yp = chars[ch]
                f.write(struct.pack("<HBBbbbbhh", ch, w, h, xo, yo, kern, 0, xp, yp))
            else:
                f.write(struct.pack("<HBBbbbbhh", ch, 0, 0, 0, 0, 0, 0, 0, 0))


def parse_course_tcl(path: Path):
    text = path.read_text(errors="replace")
    def num(key, default=None):
        m = re.search(rf"{key}\s+([-+0-9.]+)", text)
        if not m:
            if default is None:
                raise KeyError(key)
            return default
        return float(m.group(1))
    def dims():
        m = re.search(r"tux_course_dim\s+([-+0-9.]+)\s+([-+0-9.]+)\s+([-+0-9.]+)\s+([-+0-9.]+)", text)
        return tuple(float(m.group(i)) for i in range(1, 5))
    cw, cl, pw, pl = dims()
    return {
        "width": cw, "length": cl, "play_width": pw, "play_length": pl,
        "start_x": num("tux_start_pt", 0.0),
        "start_y": 0.0,
        "angle": num("tux_angle", 20.0),
        "elev_scale": num("tux_elev_scale", 10.0),
    }


def start_y_from_tcl(path: Path):
    text = path.read_text(errors="replace")
    m = re.search(r"tux_start_pt\s+([-+0-9.]+)\s+([-+0-9.]+)", text)
    return float(m.group(2)) if m else 3.5


def convert_course(cid: str):
    cdir = SRC / "courses" / cid
    meta = parse_course_tcl(cdir / "course.tcl")
    meta["start_y"] = start_y_from_tcl(cdir / "course.tcl")

    elev_im = Image.open(cdir / "elev.rgb").convert("RGB")
    terrain_im = Image.open(cdir / "terrain.rgb").convert("RGB")
    trees_im = Image.open(cdir / "trees.rgb").convert("RGB")
    nx, ny = elev_im.size
    sx, sy = trees_im.size
    if terrain_im.size != elev_im.size:
        raise ValueError(f"{cid}: terrain size mismatch")

    slope = math.tan(math.radians(meta["angle"]))
    base_height_value = 127
    width_m = meta["width"]
    length_m = meta["length"]
    elev_scale = meta["elev_scale"]

    heights = [0] * (nx * ny)
    surfaces = bytearray(nx * ny)
    # PIL row 0 is the top of the image (the start of the course); the
    # original SGI reader/game stores world z=0 there.  x is mirrored by
    # the original loader, so world xg = nx-1-pixel_x.
    for yi in range(ny):
        for xi in range(nx):
            r = elev_im.getpixel((xi, yi))[0]
            mm = ((r - base_height_value) / 255.0) * elev_scale
            mm -= (yi / ny) * length_m * slope
            xg = nx - 1 - xi
            heights[yi * nx + xg] = int(round(mm * 1000.0))
            v = terrain_im.getpixel((xi, yi))[0]
            surfaces[yi * nx + xg] = 0 if v < 48 else (1 if v < 192 else 2)

    # Objects: exactly the original nearest-colour classification.
    trees = []
    items = []
    rng = random.Random(0x54555831 ^ sum(ord(c) for c in cid))
    for yi in range(sy):
        for xi in range(sx):
            p = trees_im.getpixel((xi, yi))
            init = sum(p)
            bt = min(TREE_TYPES, key=lambda t: color_dist(p, t[:3]))
            dt = color_dist(p, bt[:3])
            bi = min(ITEM_TYPES, key=lambda t: color_dist(p, t[1]))
            di = color_dist(p, bi[1])
            world_x = (sx - xi) / (sx - 1.0) * width_m
            # exact original mapping (PIL row yi == file row sy-1-yi)
            world_z = -(1.0 + yi) / (sy - 1.0) * length_m
            if di < init and di <= dt:
                name, col, diam, height, above, flags = bi
                idx = ITEM_TYPES.index(bi)
                items.append((int(round(world_x * 1000)), int(round(world_z * 1000)),
                              idx, flags, int(round(height * 1000)),
                              int(round(diam * 1000)), above))
            elif dt < init:
                idx = TREE_TYPES.index(bt)
                _, _, _, diam, height, vary, tex = bt
                # deterministic variation around the nominal height
                v = (rng.random() * 2.0 - 1.0) * vary
                h = height * (1.0 + v)
                d = diam * (h / height)
                trees.append((int(round(world_x * 1000)), int(round(world_z * 1000)),
                              idx, int(round(h * 1000)), int(round(d * 1000))))

    trees.sort(key=lambda t: t[2])
    items.sort(key=lambda t: (t[2], t[1]))

    finish_z = min((z for x, z, t, fl, h, d, ag in items if t == ITEM_FINISH), default=-int(length_m * 1000))
    start_item_z = min((z for x, z, t, fl, h, d, ag in items if t == ITEM_START),
                       default=-int(meta["start_y"] * 1000))

    path = OUT / "course" / f"{cid}.trk"
    with path.open("wb") as f:
        f.write(b"TRK1")
        f.write(struct.pack("<HH", 1, 0))
        f.write(struct.pack("<HH", nx, ny))
        f.write(struct.pack("<IIII", int(width_m * 1000), int(length_m * 1000),
                            int(meta["play_width"] * 1000), int(meta["play_length"] * 1000)))
        f.write(struct.pack("<iii", int(meta["start_x"] * 1000),
                            int(-meta["start_y"] * 1000),
                            int(elev_scale * 1000)))
        f.write(struct.pack("<h", base_height_value))
        f.write(struct.pack("<ii", int(finish_z), int(start_item_z)))
        f.write(struct.pack("<I", 0))
        f.write(struct.pack("<i", int(round(meta["angle"] * 1000))))
        for h in heights:
            f.write(struct.pack("<i", h))
        f.write(bytes(surfaces))
        f.write(struct.pack("<H", len(trees)))
        for x, z, typ, h, d in trees:
            f.write(struct.pack("<iiBBii", x, z, typ, 0, h, d))
        f.write(struct.pack("<H", len(items)))
        for x, z, typ, flags, h, d, ag in items:
            f.write(struct.pack("<iiBBhhhh", x, z, typ, flags, h, d, int(ag * 1000), 0))
    print(f"  {cid}: {nx}x{ny} heights, {len(trees)} trees, {len(items)} items, "
          f"finish z={finish_z/1000:.1f}m -> {path.stat().st_size//1024} KiB")



def convert_sounds():
    import wave
    outdir = OUT / "sfx"
    outdir.mkdir(parents=True, exist_ok=True)
    names = ["fish_pickup1", "fish_pickup2", "fish_pickup3", "tux_hit_tree1",
             "tux_on_snow1", "tux_on_rock1", "tux_on_ice1"]
    for name in names:
        src = SRC / "sounds" / (name + ".wav")
        if not src.exists():
            print("  missing", src)
            continue
        w = wave.open(str(src), "rb")
        ch = w.getnchannels()
        rate = w.getframerate()
        width = w.getsampwidth()
        frames = w.getnframes()
        raw = w.readframes(frames)
        w.close()
        if width != 2:
            print("  unsupported wav", name)
            continue
        vals = []
        for i in range(frames):
            if ch == 2:
                l = int.from_bytes(raw[i*4:i*4+2], "little", signed=True)
                r = int.from_bytes(raw[i*4+2:i*4+4], "little", signed=True)
                vals.append((l + r) // 2)
            else:
                vals.append(int.from_bytes(raw[i*2:i*2+2], "little", signed=True))
        with (outdir / (name + ".snd")).open("wb") as f:
            f.write(b"SND1")
            f.write(struct.pack("<II", len(vals), rate))
            for v in vals:
                f.write(struct.pack("<h", max(-32768, min(32767, v))))
        print(f"  {name}: {len(vals)/rate:.2f}s")


def main():
    for d in ("tex", "course", "font"):
        (OUT / d).mkdir(parents=True, exist_ok=True)

    print("Textures")
    for name, src, alpha, size in [
        ("snow", "courses/common/snow.rgb", False, (128, 128)),
        ("rock", "courses/common/rock.rgb", False, (128, 128)),
        ("ice", "courses/common/ice.rgb", False, (128, 128)),
        ("tree1", "courses/common/tree.rgb", True, (128, 128)),
        ("tree2", "courses/common/tree_barren.rgb", True, (128, 128)),
        ("tree3", "courses/common/shrub.rgb", True, (128, 128)),
        ("herring", "courses/common/herring_standard.rgb", True, (64, 64)),
        ("flag", "courses/common/flag.rgb", True, (64, 64)),
        ("start", "courses/common/start.rgb", True, (128, 128)),
        ("finish", "courses/common/finish.rgb", True, (128, 128)),
        ("herring_icon", "textures/herringicon.rgb", True, (32, 32)),
        ("logo", "textures/menu_title.rgb", True, (256, 128)),
        ("snow_particle", "courses/common/snowparticles.rgb", True, (32, 32)),
    ]:
        p = SRC / src
        if not p.exists():
            print("  missing", p)
            continue
        w, h = write_tex(name, Image.open(p), alpha, size)
        print(f"  {name}: {w}x{h} {'A' if alpha else 'RGB'}")

    for cid, name, *_ in COURSES:
        im = Image.open(SRC / "courses" / cid / "preview.rgb")
        write_tex(f"preview_{cid}", im, False, (128, 128))
    print("  course previews")

    print("Fonts")
    write_font("font_white", SRC / "fonts/trebuchet_white.rgb", SRC / "fonts/trebuchet.tfm")
    write_font("font_yellow", SRC / "fonts/trebuchet_yel_blk.rgb", SRC / "fonts/trebuchet.tfm")
    print("  font_white, font_yellow")

    print("Sounds")
    convert_sounds()

    print("Courses")
    for cid, *_ in COURSES:
        convert_course(cid)

    # Metadata used by the core to locate the five courses.
    with (OUT / "courses.idx").open("wb") as f:
        f.write(b"CIDX")
        f.write(struct.pack("<H", len(COURSES)))
        for cid, name, par, desc in COURSES:
            f.write(struct.pack("<BB", len(name), len(desc)))
            f.write(name.encode("ascii"))
            f.write(desc.encode("ascii"))
            f.write(struct.pack("<H", int(par * 10)))
    print("Done ->", OUT)


if __name__ == "__main__":
    main()
