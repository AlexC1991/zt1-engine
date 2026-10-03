"""Align original Zoo Tycoon screenshots inside our full-map render.

usage: compare.py <ours_dir> <out_dir> [stems...]
  ours_dir holds <stem>_textured.bmp rendered at the original's zoomed-out
  tile size (32 px wide). For each original view orig/<stem>_z2_*.png the
  script finds the best offset (masked normalised cross-correlation, HUD and
  minimap masked out), then writes side-by-side pairs and a score table.
"""
import glob
import os
import sys

import numpy as np
from numpy.fft import irfft2, rfft2
from PIL import Image, ImageDraw

SP = os.path.dirname(os.path.abspath(__file__))
ORIG = os.path.join(SP, "orig")
SCN_STEM = {"default": "ff01", "medium": "ff02", "large": "ff03"}


def view_mask():
    """1 where the original screenshot shows the world, 0 over the HUD."""
    m = np.ones((600, 800), dtype=float)
    m[:, :48] = 0          # left toolbar
    m[560:, :] = 0         # bottom bar
    m[470:, :190] = 0      # minimap and its buttons
    return m


def gray(im):
    return np.asarray(im.convert("L"), dtype=float)


def masked_ncc(image, templ, mask):
    """Masked NCC of templ over image (valid positions only), via FFT."""
    H, W = image.shape
    h, w = templ.shape
    fh, fw = H + h, W + w
    def corr(a, b):  # cross-correlation of a with b at valid offsets
        return irfft2(rfft2(a, (fh, fw)) * np.conj(rfft2(b, (fh, fw))),
                      (fh, fw))[:H - h + 1, :W - w + 1]
    n = mask.sum()
    tm = templ * mask
    sum_t = tm.sum()
    sum_t2 = (templ * templ * mask).sum()
    c_it = corr(image, tm)
    c_i = corr(image, mask)
    c_i2 = corr(image * image, mask)
    num = c_it - c_i * sum_t / n
    var_i = np.maximum(c_i2 - c_i * c_i / n, 1e-6)
    var_t = max(sum_t2 - sum_t * sum_t / n, 1e-6)
    return num / np.sqrt(var_i * var_t)


def locate(ours, orig, mask):
    """Best (x, y) of the original view within our render, and its score."""
    # Coarse: half resolution
    o2 = gray(ours.resize((ours.width // 2, ours.height // 2)))
    t2 = gray(orig.resize((400, 300)))
    m2 = (np.asarray(Image.fromarray((mask * 255).astype(np.uint8))
                     .resize((400, 300))) > 128).astype(float)
    score = masked_ncc(o2, t2, m2)
    cy, cx = np.unravel_index(np.argmax(score), score.shape)
    # Fine: full resolution in a small window
    x0, y0 = max(0, 2 * cx - 6), max(0, 2 * cy - 6)
    x1 = min(ours.width, 2 * cx + 6 + 800)
    y1 = min(ours.height, 2 * cy + 6 + 600)
    region = gray(ours.crop((x0, y0, x1, y1)))
    if region.shape[0] < 600 or region.shape[1] < 800:
        return 2 * cx, 2 * cy, float(score.max())
    fine = masked_ncc(region, gray(orig), mask)
    fy, fx = np.unravel_index(np.argmax(fine), fine.shape)
    return x0 + fx, y0 + fy, float(fine.max())


def main():
    ours_dir, out_dir = sys.argv[1], sys.argv[2]
    only = sys.argv[3:]
    os.makedirs(out_dir, exist_ok=True)
    mask = view_mask()
    stems = sorted({os.path.basename(f).split("_z2_")[0]
                    for f in glob.glob(os.path.join(ORIG, "*_z2_*.png"))})
    rows = []
    for stem in stems:
        if only and stem not in only:
            continue
        ours_path = os.path.join(ours_dir, SCN_STEM.get(stem, stem) + "_textured.bmp")
        if not os.path.exists(ours_path):
            print(f"{stem}: no render", flush=True)
            continue
        ours = Image.open(ours_path).convert("RGB")
        pairs = []
        for view in sorted(glob.glob(os.path.join(ORIG, f"{stem}_z2_*.png"))):
            orig = Image.open(view).convert("RGB")
            x, y, s = locate(ours, orig, mask)
            crop = ours.crop((x, y, x + 800, y + 600))
            name = os.path.basename(view)[len(stem) + 4:-4]
            pairs.append((name, orig, crop, s))
            rows.append((stem, name, s, x, y))
            print(f"{stem:10s} {name:6s} score={s:.3f} at ({x},{y})", flush=True)
        # Sheet: original | ours for every view
        tw, th = 400, 300
        sheet = Image.new("RGB", (2 * tw + 24, len(pairs) * (th + 18)), (30, 30, 30))
        d = ImageDraw.Draw(sheet)
        for i, (name, orig, crop, s) in enumerate(pairs):
            yy = i * (th + 18)
            sheet.paste(orig.resize((tw, th)), (0, yy + 16))
            sheet.paste(crop.resize((tw, th)), (tw + 24, yy + 16))
            d.text((4, yy + 2), f"{stem} {name}  original | ours   NCC {s:.3f}",
                   fill=(255, 220, 0))
        sheet.save(os.path.join(out_dir, f"{stem}.png"))
    with open(os.path.join(out_dir, "scores.tsv"), "a") as f:
        for r in rows:
            f.write("\t".join(str(v) for v in r) + "\n")


if __name__ == "__main__":
    main()
