"""Export the original UI art as PNGs, ready to be upscaled into an HD pack.

The engine replaces art with a PNG from an hd/ folder next to zoo.exe /
zt1-engine.exe when one exists:

    hd/<image path without extension>.png         e.g. hd/ui/sharedui/statr.png
    hd/<animation folder>/<file>_<frame>.png      e.g. hd/ui/main/trees/S_0.png

A replacement must be a whole multiple (2x, 3x, 4x...) of the original's size,
keeping the transparency, so it lands exactly where the original art does.

This script exports the art the engine actually loads (the game's resource
search order from zoo.ini, loose files first) under those same names:

    python tools/export_hd_art.py [game dir] [out dir]

Upscale the exported PNGs with any tool you like (an AI upscaler such as
Real-ESRGAN, chaiNNer or Upscayl works well on this pre-rendered art), keep
the file names, and copy the results into <game dir>/hd/.
"""

import configparser
import glob
import os
import struct
import sys
import zipfile
from io import BytesIO

from PIL import Image

IMAGE_EXTS = (".tga", ".bmp", ".png")


def resource_dirs(game_dir):
    cfg = configparser.ConfigParser(strict=False, interpolation=None)
    cfg.read(os.path.join(game_dir, "zoo.ini"), encoding="latin-1")
    paths = cfg.get("resource", "path", fallback=".").split(";")
    return [p.strip() for p in paths if p.strip()]


def open_archives(game_dir):
    """Archives in the game's search order (earlier ones win)."""
    archives = []
    for d in resource_dirs(game_dir):
        full = os.path.normpath(os.path.join(game_dir, d))
        for f in sorted(glob.glob(os.path.join(full, "*.ztd"))):
            try:
                archives.append(zipfile.ZipFile(f))
            except zipfile.BadZipFile:
                pass
    return archives


class Resources:
    def __init__(self, game_dir):
        self.game_dir = game_dir
        # lower-case name -> (archive or None, archive name or file path,
        # name as spelled in the game files)
        self.index = {}
        # Loose files override every archive
        for root, _, files in os.walk(os.path.join(game_dir, "ui")):
            for f in files:
                full = os.path.join(root, f)
                rel = os.path.relpath(full, game_dir).replace("\\", "/")
                self.index.setdefault(rel.lower(), (None, full, rel))
        for z in open_archives(game_dir):
            for n in z.namelist():
                if not n.endswith("/"):
                    self.index.setdefault(n.lower(), (z, n, n))

    def names(self, prefix):
        return sorted(v[2] for k, v in self.index.items()
                      if k.startswith(prefix))

    def read(self, name):
        z, real, _ = self.index[name.lower()]
        if z is None:
            with open(real, "rb") as f:
                return f.read()
        return z.read(real)

    def has(self, name):
        return name.lower() in self.index


def read_palette(data):
    count = struct.unpack_from("<H", data, 0)[0]
    count = min(count or 256, (len(data) - 4) // 4)
    colours = [tuple(data[4 + i * 4:7 + i * 4]) for i in range(count)]
    return colours + [(0, 0, 0)] * (256 - len(colours))


def decode_frames(data, res):
    """All frames of a ZT1 frame file, as RGBA images (None if not one)."""
    pos = 9 if data[:4] == b"FATZ" else 0
    try:
        pos += 4  # frame time
        pal_len = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if not 0 < pal_len < 256:
            return None
        pal_name = data[pos:pos + pal_len].split(b"\0")[0].decode("latin-1")
        pos += pal_len
        count = struct.unpack_from("<I", data, pos)[0]
        pos += 4
        if not res.has(pal_name) or not 0 < count < 1000:
            return None
        pal = read_palette(res.read(pal_name))
        frames = []
        for _ in range(count):
            size = struct.unpack_from("<I", data, pos)[0]
            start = pos + 4
            h, w = struct.unpack_from("<HH", data, start)
            p = start + 10
            img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
            px = img.load()
            for y in range(h):
                runs = data[p]
                p += 1
                x = 0
                for _ in range(runs):
                    x += data[p]
                    n = data[p + 1]
                    p += 2
                    for i in range(n):
                        if 0 <= x < w:
                            r, g, b = pal[data[p + i]]
                            px[x, y] = (r, g, b, 255)
                        x += 1
                    p += n
            frames.append(img)
            pos = start + size
        return frames
    except (struct.error, IndexError):
        return None


def main():
    game_dir = sys.argv[1] if len(sys.argv) > 1 else "build/Release"
    out_dir = sys.argv[2] if len(sys.argv) > 2 else "hd_export"
    res = Resources(game_dir)
    images = frames_out = 0
    for name in res.names("ui/"):
        base, ext = os.path.splitext(name)
        ext = ext.lower()
        if ext in IMAGE_EXTS:
            try:
                img = Image.open(BytesIO(res.read(name))).convert("RGBA")
            except Exception:
                continue
            dest = os.path.join(out_dir, base + ".png")
            os.makedirs(os.path.dirname(dest), exist_ok=True)
            img.save(dest)
            images += 1
        elif ext == "":
            frames = decode_frames(res.read(name), res)
            if not frames:
                continue
            for i, img in enumerate(frames):
                dest = os.path.join(out_dir, "%s_%d.png" % (name, i))
                os.makedirs(os.path.dirname(dest), exist_ok=True)
                img.save(dest)
                frames_out += 1
    print("Exported %d images and %d animation frames to %s"
          % (images, frames_out, out_dir))


if __name__ == "__main__":
    main()
