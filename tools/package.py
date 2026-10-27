#!/usr/bin/env python3
"""Build the SD-card payloads for Tux Racer (GB300 / SF2000)."""
from pathlib import Path
import shutil
import struct
import zipfile
from PIL import Image

ROOT = Path(__file__).resolve().parent.parent
DIST = ROOT / "dist"
ASSETS = ROOT / "assets"


def thumb():
    src = ROOT / "screenshots" / "tux_07_race_steer.png"
    if not src.exists():
        src = ROOT / "screenshots" / "tux_01_title.png"
    dst = DIST / "res" / "TuxRacer.png"
    dst.parent.mkdir(parents=True, exist_ok=True)
    im = Image.open(src).convert("RGB")
    side = min(im.size)
    left = (im.width - side) // 2
    top = (im.height - side) // 2
    im = im.crop((left, top, left + side, top + side)).resize((160, 160), Image.Resampling.LANCZOS)
    im.save(dst)
    raw = dst.with_suffix(".rgb565")
    with raw.open("wb") as f:
        for y in range(160):
            for x in range(160):
                r, g, b = im.getpixel((x, y))
                p = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
                f.write(struct.pack("<H", p))
    return dst, raw


def make_zip(name, core_name):
    path = DIST / name
    core = DIST / core_name
    if not core.exists():
        print("missing", core)
        return
    png, raw = thumb()
    stub = "tuxracer;tuxracer;TuxRacer.gba"
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("ROMS/TuxRacer.gba", stub)
        z.writestr("ROMS/tuxracer/TuxRacer.gba", stub)
        z.write(core, f"system/Deimos/cores/tuxracer.{ 'gb3k' if 'gb3k' in core_name else 'sf2k' }")
        z.write(core, "cores/tuxracer/core_87000000")
        z.write(core, "ROMS/tuxracer/core_87000000")
        for p in sorted(ASSETS.rglob("*")):
            if p.is_file() and p.name != "TuxRacer.gba":
                z.write(p, "ROMS/tuxracer/" + str(p.relative_to(ASSETS)))
        z.write(png, "ROMS/tuxracer/.res/TuxRacer.png")
        z.write(raw, "ROMS/tuxracer/.res/TuxRacer.rgb565")
        z.write(raw, "ROMS/TuxRacer.rgb565")
        z.write(raw, "ROMS/.res/TuxRacer.rgb565")
        z.writestr("configs/custom_cores/tuxracer/games.opt",
                   "TuxRacer.gba|tuxracer\nTuxRacer|tuxracer\ntuxracer.gb3k|tuxracer\ntuxracer.sf2k|tuxracer\n")
    print("wrote", path, path.stat().st_size // 1024, "KiB")


def main():
    DIST.mkdir(exist_ok=True)
    make_zip("tuxracer-sf2000-sd.zip", "tuxracer.sf2k")
    make_zip("tuxracer-gb300-sd.zip", "tuxracer.gb3k")


if __name__ == "__main__":
    main()
