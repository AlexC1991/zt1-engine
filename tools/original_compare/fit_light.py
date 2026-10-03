import re, numpy as np
from PIL import Image
from compare import view_mask
mask = view_mask() > 0
pos = {}
for l in open('compare_a0_log.txt'):
    m = re.match(r'(\S+)\s+(\S+)\s+score=([\d.]+) at \((\d+),(\d+)\)', l)
    if m and float(m.group(3)) > 0.55:
        pos[(m.group(1), m.group(2))] = (int(m.group(4)), int(m.group(5)))
maps = "svolcano,deathmtn,sm_cclif,fshore,lagoon,sm_rlake,med_isle,beach,tundra,sm_aqua,under,arcmaze,lavaland,med_crat,svalley,mythgard".split(',')
R, N = [], []
cache = {}
for (stem, view), (x, y) in pos.items():
    if stem not in maps: continue
    if stem not in cache:
        cache[stem] = (np.asarray(Image.open(f'ours_shade1/{stem}_textured.bmp').convert('RGB'), float),
                       np.asarray(Image.open(f'ours_shade2/{stem}_textured.bmp').convert('RGB'), float))
    unlit, nrm = cache[stem]
    o = np.asarray(Image.open(f'orig/{stem}_z2_{view}.png').convert('RGB'), float)
    u = unlit[y:y+600, x:x+800]; n = nrm[y:y+600, x:x+800]
    if u.shape[:2] != (600, 800): continue
    lo, lu = o.mean(2), u.mean(2)
    ok = mask & (lu > 25) & (lo > 5)
    # drop pixels whose hue differs a lot (objects): compare chroma direction
    co = o / np.maximum(o.sum(2, keepdims=True), 1); cu = u / np.maximum(u.sum(2, keepdims=True), 1)
    ok &= np.abs(co - cu).sum(2) < 0.08
    R.append((lo / lu)[ok]); N.append(n[ok])
R = np.concatenate(R); N = np.concatenate(N)
nx = (N[:, 0] - 128) / 127; ny = (N[:, 1] - 128) / 127; nz = N[:, 2] / 255
print('pixels', len(R))
flat = nz > 0.995
print(f'flat ground: median factor {np.median(R[flat]):.3f}  (n={flat.sum()})')
walls = nz < 0.05
for name, sel in [('wall facing +x', walls & (nx > 0.9)), ('wall facing -x', walls & (nx < -0.9)),
                  ('wall facing +y', walls & (ny > 0.9)), ('wall facing -y', walls & (ny < -0.9))]:
    if sel.sum() > 200: print(f'{name}: median {np.median(R[sel]):.3f} (n={sel.sum()})')
# slopes: bin by (nx, ny)
slope = (nz < 0.995) & (nz > 0.3)
print('slopes (nx, ny bins of 0.1): median factor, count')
bx = np.round(nx[slope] / 0.1).astype(int); by = np.round(ny[slope] / 0.1).astype(int); rs = R[slope]
rows = []
for a in range(-6, 7):
    for b in range(-6, 7):
        sel = (bx == a) & (by == b)
        if sel.sum() > 300: rows.append((a / 10, b / 10, np.median(rs[sel]), sel.sum()))
for r in rows: print(f'  nx={r[0]:+.1f} ny={r[1]:+.1f}  factor={r[2]:.3f}  n={r[3]}')
np.savez('light_samples.npz', R=R, nx=nx, ny=ny, nz=nz)
