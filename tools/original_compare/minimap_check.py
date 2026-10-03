"""Project a .zoo's terrain like the original minimap, normal vs transposed."""
import os, struct, sys
import numpy as np
from PIL import Image, ImageDraw

MAPS = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "Release", "maps")
PAL = {0:(60,160,60),1:(220,190,60),2:(225,195,130),3:(130,90,50),4:(40,110,40),5:(120,80,55),
       6:(140,140,140),7:(180,180,180),8:(245,245,245),9:(80,150,230),10:(40,70,170),11:(100,140,40),
       12:(80,200,230),13:(40,80,60),14:(200,190,160),15:(60,60,60),16:(170,140,100),17:(120,170,190)}

def load(stem):
    d = open(f"{MAPS}/{stem}.zoo", "rb").read()
    ver = struct.unpack_from("<I", d, 4)[0]
    off = 0x10 if ver >= 82 else 0x0C
    W, H = struct.unpack_from("<II", d, off)
    # same scan as ZooReader
    a = np.frombuffer(d, np.uint8); L = len(a) - 10
    i32 = (a[0:L].astype(np.int64) | (a[1:L+1].astype(np.int64) << 8) |
           (a[2:L+2].astype(np.int64) << 16) | (a[3:L+3].astype(np.int64) << 24))
    i32 = np.where(i32 >= 2**31, i32 - 2**32, i32)
    v = (i32 >= -64) & (i32 <= 64) & (a[5:L+5] <= 17) & (a[7:L+7] == 0) & (a[8:L+8] == 0) & (a[9:L+9] == 0)
    N = W * H
    run = np.zeros(len(v) + 10, np.int64)
    for k in range(len(v) - 1, -1, -1):
        run[k] = run[k + 10] + 1 if v[k] else 0
    first = next(s for s in range(off + 8, len(v)) if run[s] >= N)
    start = max(s for s in range(first, first + 10) if run[s] >= N)
    t = np.frombuffer(d, np.uint8, count=N * 10, offset=start).reshape(H, W, 10)[:, :, 5]
    return W, H, t

def iso(grid, scale=2):
    """Default-view iso: world x=0 edge faces lower-left (view u=y, v=W-1-x)."""
    H, W = grid.shape
    img = Image.new("RGB", ((W + H) * scale, (W + H) * scale // 2 + 2), (20, 20, 20))
    px = img.load()
    for y in range(H):
        for x in range(W):
            u, v = y, W - 1 - x
            cx = (u - v + W - 1) * scale; cy = (u + v) * scale // 2
            for dx in range(2 * scale):
                for dy in range(scale):
                    px[min(cx + dx, img.width - 1), min(cy + dy, img.height - 1)] = PAL.get(int(grid[y, x]), (255, 0, 255))
    return img

if __name__ == "__main__":
    stem = sys.argv[1]
    W, H, t = load(stem)
    mm = Image.open(f"orig/{stem}_z2_start.png").crop((5, 512, 150, 598)).resize((435, 258), Image.NEAREST)
    a = iso(t); b = iso(t.T)
    a.thumbnail((435, 258)); b.thumbnail((435, 258))
    s = Image.new("RGB", (435 * 3 + 40, 280), (30, 30, 30)); d = ImageDraw.Draw(s)
    for i, (im, lab) in enumerate([(mm, "original minimap"), (a, "ours: row-major [y][x]"), (b, "ours: transposed")]):
        s.paste(im, (i * 455, 20)); d.text((i * 455, 2), lab, fill=(255, 220, 0))
    s.save(f"mm_{stem}.png")
