r"""The ZT1 Lab's art kit: an animation's frames as masks and pictures on its
anchor (where the unit stands), for comparing the game's own art and making
what its files name but never shipped (Marine Mania's missing shadows).

Built on animaker.py (the game's frame format, FATZ and shadow frames).
A frame header's first offset holds the anchor's y, its second the anchor's
x (src/Animation.cpp: "offset_x holds the y anchor, offset_y the x one").
"""
import sys

import numpy as np

import animaker as A


def mask(f):
    """The frame's drawn pixels (a shadow frame's: its shadow)"""
    m = np.zeros((f['h'], f['w']), bool)
    for y, runs in enumerate(f['lines']):
        x = 0
        for skip, cols in runs:
            x += skip
            m[y, x:x + len(cols)] = True
            x += len(cols)
    return m


def points(f):
    """Its drawn pixels as (x, y) round the anchor"""
    ys, xs = np.nonzero(mask(f))
    return xs - f['oy'], ys - f['ox']


def canvas(f, size=320):
    """The frame's mask on a square round the anchor (anchor in the middle)"""
    c = np.zeros((size, size), bool)
    xs, ys = points(f)
    xs, ys = xs + size // 2, ys + size // 2
    ok = (xs >= 0) & (xs < size) & (ys >= 0) & (ys < size)
    c[ys[ok], xs[ok]] = True
    return c


def from_canvas(c, size=320):
    """A shadow frame from a mask on the anchor canvas"""
    ys, xs = np.nonzero(c)
    if len(xs) == 0:
        return {'h': 0, 'w': 0, 'ox': 0, 'oy': 0, 'flags': 0, 'lines': [], 'shadow': True}
    x0, x1, y0, y1 = xs.min(), xs.max(), ys.min(), ys.max()
    sub = c[y0:y1 + 1, x0:x1 + 1]
    lines = []
    for row in sub:
        runs, x, w = [], 0, len(row)
        while x < w:
            s = x
            while x < w and not row[x]:
                x += 1
            if x >= w:
                break
            e = x
            while e < w and row[e]:
                e += 1
            # (a run's skip and length are a byte each)
            while x - s > 255:
                runs.append((255, bytearray(0)))
                s += 255
            ln = e - x
            first = True
            while ln > 0:
                n = min(255, ln)
                runs.append((x - s if first else 0, bytearray(n)))
                first = False
                ln -= n
            s = x = e
        lines.append(runs)
    return {'h': sub.shape[0], 'w': sub.shape[1], 'ox': size // 2 - y0, 'oy': size // 2 - x0,
            'flags': 0, 'lines': lines, 'shadow': True}
