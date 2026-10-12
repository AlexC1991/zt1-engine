r"""The ZT1 Lab's animation maker: a new animation from one of the game's,
packed as a mod.

Reads an animation (its .ani and each direction's frames, in the game's own
format) and its palette from the game's archives, as the engine finds them
(the engine folder's zoo.ini [resource] path: the first wins), then:
  - recolours one palette colour so it cycles through new colours, a frame
    at a time (a flicker), with the new colours in a new palette;
  - sets a new frame time (its speed);
and writes a mod: <mods folder>/<folder>/mod.json and content.ztd, the new
files under their game paths, so they take the original's place while the
mod is on.

One JSON request on stdin, one JSON answer on stdout:
  {"op": "colours", "engineDir": ..., "anim": "fences/dzbar/f/idle"}
      -> every colour the animation uses, most used first: [{index, hex, count}]
  {"op": "make", "engineDir": ..., "anim": ..., "index": 205,
   "colours": ["#ff3030", "#ffd000"], "frameMs": 50, "modsFolder": ...,
   "mod": {"folder", "name", "author", "about"}}
      -> {"ok": true, "mod": <its folder>, "files": [...]}
The frame format (as src/AniFile.cpp reads it): a frame time (u32), the
palette's name (u32 length, then the name), a frame count (u32), then each
frame: its size (u32, counting what follows), height, width, x and y
offsets, a flags word (u16 each), then each line: a run count (u8) and
runs of (skip u8, length u8, length colour bytes).
"""
import collections
import configparser
import glob
import io
import json
import os
import struct
import sys
import zipfile


def archives(engine_dir):
    """The game's archives in the engine's order: the first one wins."""
    ini = configparser.ConfigParser(strict=False, interpolation=None)
    ini.read(os.path.join(engine_dir, 'zoo.ini'), encoding='latin1')
    paths = [p for p in ini.get('resource', 'path', fallback='').split(';') if p.strip()]
    for x in ('xpack1', 'xpack2'):
        if os.path.isdir(os.path.join(engine_dir, x)):
            paths.append(x)
    out = []
    for p in paths:
        full = p if os.path.isabs(p) else os.path.join(engine_dir, p)
        if not os.path.isdir(full):
            continue
        found = [f for f in glob.glob(os.path.join(full, '*')) if f.lower().endswith(('.ztd', '.zip'))]
        # (in one folder, a later archive wins: last first here)
        out.extend(sorted(found, key=lambda f: os.path.basename(f).lower(), reverse=True))
    return out


class Game:
    def __init__(self, engine_dir):
        self.order = archives(engine_dir)
        self.zips = {}

    def read(self, name):
        name = name.lower().replace('\\', '/')
        for a in self.order:
            z = self.zips.get(a)
            if z is None:
                try:
                    z = zipfile.ZipFile(a)
                except Exception:
                    z = False
                self.zips[a] = z
            if not z:
                continue
            for n in z.namelist():
                if n.lower().replace('\\', '/') == name:
                    return z.read(n)
        raise FileNotFoundError(name)


def parse_ani(text):
    """The .ani: its folder and its directions."""
    dirs, folder = [], {}
    for line in text.decode('latin1').splitlines():
        if '=' not in line:
            continue
        k, v = [s.strip() for s in line.split('=', 1)]
        k = k.lower()
        if k in ('dir0', 'dir1', 'dir2', 'dir3'):
            folder[k] = v
        elif k == 'animation':
            dirs.append(v)
    path = '/'.join(folder[k] for k in ('dir0', 'dir1', 'dir2', 'dir3') if folder.get(k))
    return path, dirs


def parse_frames(data):
    if struct.unpack_from('<I', data, 0)[0] == 0x5A544146:
        raise ValueError('FATZ art (a background frame) is not supported yet')
    ms, n = struct.unpack_from('<II', data, 0)
    pal = data[8:8 + n].split(b'\0')[0].decode('latin1')
    pos = 8 + n
    count = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    frames = []
    for _ in range(count):
        size, h, w, ox, oy, flags = struct.unpack_from('<IHHhhH', data, pos)
        if h & 0x8000:
            raise ValueError('shadow frames are not supported yet')
        p = pos + 14
        lines = []
        for _ in range(h):
            runs = []
            k = data[p]
            p += 1
            for _ in range(k):
                skip, length = data[p], data[p + 1]
                runs.append((skip, bytearray(data[p + 2:p + 2 + length])))
                p += 2 + length
            lines.append(runs)
        frames.append({'h': h, 'w': w, 'ox': ox, 'oy': oy, 'flags': flags, 'lines': lines})
        pos += 4 + size
    return {'ms': ms, 'pal': pal, 'frames': frames}


def write_frames(art):
    out = io.BytesIO()
    name = art['pal'].encode('latin1') + b'\0'
    out.write(struct.pack('<II', art['ms'], len(name)))
    out.write(name)
    out.write(struct.pack('<I', len(art['frames'])))
    for f in art['frames']:
        body = io.BytesIO()
        for runs in f['lines']:
            body.write(bytes([len(runs)]))
            for skip, colours in runs:
                body.write(bytes([skip, len(colours)]))
                body.write(bytes(colours))
        pixels = body.getvalue()
        out.write(struct.pack('<IHHhhH', 10 + len(pixels), f['h'], f['w'], f['ox'], f['oy'], f['flags']))
        out.write(pixels)
    return out.getvalue()


def read_pal(data):
    n = struct.unpack_from('<I', data, 0)[0]
    return list(struct.unpack_from('<%dI' % n, data, 4))


def write_pal(cols):
    return struct.pack('<I', len(cols)) + struct.pack('<%dI' % len(cols), *cols)


# (a palette entry is 0xAABBGGRR: red in the low byte, as the engine reads it)
def rgb(c):
    return c & 255, (c >> 8) & 255, (c >> 16) & 255


def entry(r, g, b):
    return 0xFF000000 | (b << 16) | (g << 8) | r


def hexcol(c):
    return '#%02x%02x%02x' % rgb(c)


def hsv(c):
    import colorsys
    r, g, b = rgb(c)
    return colorsys.rgb_to_hsv(r / 255, g / 255, b / 255)


def hue_near(a, b, d):
    return min(abs(a - b), 1 - abs(a - b)) <= d


def shade(new, old, picked):
    """The new colour at the old one's lightness (against the one picked)"""
    lum = lambda c: 0.3 * rgb(c)[0] + 0.59 * rgb(c)[1] + 0.11 * rgb(c)[2]
    k = lum(old) / max(1.0, lum(picked))
    r, g, b = [min(255, int(v * k)) for v in rgb(new)]
    return entry(r, g, b)


def argb(h):
    """#rrggbb as a palette entry"""
    h = h.lstrip('#')
    return entry(int(h[0:2], 16), int(h[2:4], 16), int(h[4:6], 16))


def load(game, anim):
    ani_name = anim + '/' + anim.split('/')[-1] + '.ani'
    ani = game.read(ani_name)
    folder, dirs = parse_ani(ani)
    arts = {d: parse_frames(game.read(folder + '/' + d)) for d in dirs}
    return ani_name, ani, folder, arts


def colours(req):
    game = Game(req['engineDir'])
    _, _, _, arts = load(game, req['anim'])
    pal_name = next(iter(arts.values()))['pal']
    pal = read_pal(game.read(pal_name))
    used = collections.Counter()
    for art in arts.values():
        for f in art['frames']:
            for runs in f['lines']:
                for _, cs in runs:
                    used.update(cs)
    return {'ok': True, 'palette': pal_name, 'frames': len(next(iter(arts.values()))['frames']),
            'frameMs': next(iter(arts.values()))['ms'],
            'colours': [{'index': i, 'hex': hexcol(pal[i]), 'count': n} for i, n in used.most_common()]}


def make(req):
    game = Game(req['engineDir'])
    ani_name, ani, folder, arts = load(game, req['anim'])
    pal_name = next(iter(arts.values()))['pal']
    pal = read_pal(game.read(pal_name))
    pal += [0] * (256 - len(pal))
    used = set()
    for art in arts.values():
        for f in art['frames']:
            for runs in f['lines']:
                for _, cs in runs:
                    used.update(cs)
    index = int(req['index'])
    new_cols = [argb(c) for c in req['colours']] or [pal[index]]
    # The colours to change: the one picked, or (similar) every one used of
    # its hue - each keeps its own lightness, so the shading stays
    picks = [index]
    if req.get('similar'):
        # (its hue, at least about as strong, and about as light: a wire's
        # highlights, not the dark steel under them)
        h0, s0, v0 = hsv(pal[index])
        picks = [i for i in sorted(used)
                 if i and hue_near(hsv(pal[i])[0], h0, 0.06) and hsv(pal[i])[1] >= max(0.12, s0 * 0.6)
                 and hsv(pal[i])[2] >= v0 * 0.75]
        if index not in picks:
            picks.insert(0, index)
    spare = [i for i in range(255, 0, -1) if i not in used]
    # (a frame each colour: the first in the old entries, the others in free
    # ones - as many as there's room for)
    while len(new_cols) > 1 and len(spare) < len(picks) * (len(new_cols) - 1):
        new_cols.pop()
    remap = []  # per new colour: old index -> its entry
    for n, c in enumerate(new_cols):
        m = {}
        for i in picks:
            slot = i if n == 0 else spare.pop(0)
            pal[slot] = shade(c, pal[i], pal[index])
            m[i] = slot
        remap.append(m)
    new_pal_name = req.get('newPalette') or (pal_name.rsplit('.', 1)[0] + '_' + req['mod']['folder'].lower() + '.pal')
    files = {}
    for d, art in arts.items():
        art['pal'] = new_pal_name
        if req.get('frameMs'):
            art['ms'] = int(req['frameMs'])
        for i, f in enumerate(art['frames']):
            m = remap[i % len(remap)]
            for runs in f['lines']:
                for _, cs in runs:
                    for k, c in enumerate(cs):
                        if c in m:
                            cs[k] = m[c]
        files[(folder + '/' + d).lower()] = write_frames(art)
    # (names in lower case, as the game's archives have them)
    files[ani_name.lower()] = ani
    files[new_pal_name.lower()] = write_pal(pal)
    m = req['mod']
    out_dir = os.path.join(req['modsFolder'], m['folder'])
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(os.path.join(out_dir, 'content.ztd'), 'w', zipfile.ZIP_DEFLATED) as z:
        for name, data in files.items():
            z.writestr(name, data)
    info = {'name': m.get('name') or m['folder'], 'author': m.get('author', ''), 'version': m.get('version', '1.0'),
            'about': m.get('about', ''), 'replaces': sorted(f for f in files if f != new_pal_name.lower()),
            # (the Mod Menu's picture of it: a frame it changes, its palette and the original's)
            'preview': (folder + '/' + next((d for d in arts if d.upper() == 'SE'), next(iter(arts)))).lower(),
            'palette': new_pal_name.lower(), 'originalPalette': pal_name.lower(),
            'made': {'from': req['anim'], 'colour': index, 'changed': len(picks), 'colours': req['colours'][:len(remap)],
                     'frameMs': req.get('frameMs')}}
    with open(os.path.join(out_dir, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)
    return {'ok': True, 'mod': out_dir, 'files': sorted(files), 'changed': len(picks), 'flicker': len(remap)}


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = colours(req) if req.get('op') == 'colours' else make(req)
    except Exception as e:
        res = {'ok': False, 'error': '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
