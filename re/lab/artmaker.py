r"""The ZT1 Lab's art maker (Mods > a mod > Art): a mod's art read, changed
and saved back into its .ztd - palettes recoloured, frames painted, a
pattern faded along a body in every frame, frames out to PNG files and back.

A frame (the game's format: src/AniFile.cpp, re/lab/animaker.py) is lines of
runs of palette indices; here it's its w x h indices and a mask (1: drawn).
Files are read from the mod's .ztd first, then the game's own (so a mod that
changes only a palette still shows its animal).

One JSON request on stdin, one JSON answer on stdout:
  {"op": "anims", "ztd"}                  -> {"anims": [{"ani", "folder", "dirs"}]}
  {"op": "load", "ztd", "engineDir", "ani"}
      -> {"folder", "ms", "dirs": [{"name", "pal", "frames": [{"w","h","ox","oy","px","mask","shadow"}]}],
          "palettes": {name: ["#rrggbb", ...]}}         (px, mask: base64)
  {"op": "save", "ztd", "engineDir", "ani", "palettes": {name: [...]},
   "frames": {dir: {index: {"px", "mask"}}}}         -> {"ok", "wrote": [...]}
  {"op": "fade", "ztd", "engineDir", "folder": "animals/quagga/", "from": [indices],
   "to": index, "start": 0.35, "end": 0.85, "pal": name}
      -> every frame under folder: the from colours, past start (0 the head,
         1 the tail) turned into the to colour, all of them by end
  {"op": "stripes", ..., "folder", "from", "pal", "start", "end", "count", "width", "darkness", "depth"}
      -> stripes across the body in every frame: the picked colours made darker
         in count bands between start and end (0 the head), on its back only
  {"op": "export", "ztd", "engineDir", "ani", "out": <folder>}  -> PNGs, one a frame
  {"op": "import", "ztd", "engineDir", "ani", "src": <folder>}  -> frames from PNGs
"""
import base64
import json
import os
import re
import struct
import sys
import tempfile
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402

# Where each facing's head is, on screen: the body runs from the head (0) to
# the tail (1) along this, so a pattern can fade along it in every frame
HEAD = {'n': (0, -1), 's': (0, 1), 'e': (1, 0), 'w': (-1, 0),
        'ne': (0.7, -0.7), 'nw': (-0.7, -0.7), 'se': (0.7, 0.7), 'sw': (-0.7, 0.7)}


class Files:
    """The mod's .ztd, then the game's archives"""

    def __init__(self, ztd, engine_dir):
        self.ztd = ztd
        self.z = zipfile.ZipFile(ztd) if os.path.exists(ztd) else None
        self.names = {n.lower().replace(chr(92), '/'): n for n in (self.z.namelist() if self.z else [])}
        self.game = animaker.Game(engine_dir) if engine_dir else None

    def index(self):
        """The game's files, by lower-case name, made once (the first archive
        that has one wins) - looking each one up across every archive was
        most of the time"""
        if getattr(self, '_index', None) is None:
            order = self.game.order if self.game else []
            self._index = {k: (order[i], n) for k, (i, n) in self._cached(order).items()}
        return self._index

    def _cached(self, order):
        """The index as {name: (archive number, entry)}, kept on disk while
        the archives are the same (their sizes and dates): opening every one
        of them took a few seconds each time the art maker ran"""
        sig = []
        for a in order:
            try:
                st = os.stat(a)
                sig.append([a, st.st_size, int(st.st_mtime)])
            except OSError:
                sig.append([a, 0, 0])
        path = os.path.join(tempfile.gettempdir(), 'zt1-lab-artindex.json')
        try:
            with open(path, encoding='utf-8') as f:
                kept = json.load(f)
            if kept.get('sig') == sig:
                return kept['files']
        except (OSError, ValueError):
            pass
        files = {}
        for i, a in enumerate(order):
            try:
                z = zipfile.ZipFile(a)
            except Exception:
                continue
            self.game.zips[a] = z
            for n in z.namelist():
                files.setdefault(n.lower().replace(chr(92), '/'), (i, n))
        try:
            tmp = path + '.tmp'
            with open(tmp, 'w', encoding='utf-8') as f:
                json.dump({'sig': sig, 'files': files}, f)
            os.replace(tmp, path)
        except OSError:
            pass
        return files

    def _zip(self, a):
        z = self.game.zips.get(a)
        if not z:
            z = self.game.zips[a] = zipfile.ZipFile(a)
        return z

    def under(self, prefix):
        return [k for k in self.index() if k.startswith(prefix) and not k.endswith('/')]

    def read(self, name):
        k = name.lower().replace(chr(92), '/')
        if k in self.names:
            return self.z.read(self.names[k])
        hit = self.index().get(k)
        if hit:
            return self._zip(hit[0]).read(hit[1])
        raise FileNotFoundError(name)

    def in_mod(self, name):
        return name.lower().replace(chr(92), '/') in self.names


def to_grid(f):
    w, h = f['w'], f['h']
    px, mask = bytearray(w * h), bytearray(w * h)
    for y, runs in enumerate(f['lines']):
        x = 0
        for skip, colours in runs:
            x += skip
            for c in colours:
                if 0 <= x < w:
                    px[y * w + x] = c
                    mask[y * w + x] = 1
                x += 1
    return px, mask


def from_grid(f, px, mask):
    w, h = f['w'], f['h']
    lines = []
    for y in range(h):
        runs, x = [], 0
        while x < w:
            start = x
            while x < w and not mask[y * w + x]:
                x += 1
            if x >= w:
                break
            skip = x - start
            # (a run's skip and length are a byte each)
            while skip > 255:
                runs.append((255, bytearray()))
                skip -= 255
            colours = bytearray()
            while x < w and mask[y * w + x] and len(colours) < 255:
                colours.append(px[y * w + x])
                x += 1
            runs.append((skip, colours))
        lines.append(runs)
    f['lines'] = lines


def hexes(cols):
    return [animaker.hexcol(c) for c in cols] + ['#000000'] * (256 - len(cols))


def art_prefixes(path, text):
    """Where a thing's art is: its [AnimPath] folders, and its type's folder
    (animals/<type>/, or objects/<type>/ for the rest)"""
    kind = path.split('/')[0]
    typ = os.path.splitext(os.path.basename(path))[0]
    out = set()
    section = ''
    for line in text.splitlines():
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            section = m.group(1).strip().lower()
            continue
        if '=' not in line:
            continue
        k, v = [s.strip() for s in line.split('=', 1)]
        if section == 'global' and k.lower() == 'type' and v:
            typ = v
        elif section == 'animpath' and v:
            out.add(v.lower().rstrip('/') + '/')
    out.add(('animals/' if kind == 'animals' else 'objects/') + typ.lower() + '/')
    return out


def anims(req):
    """A mod's animations: its own, and - for each thing it changes or adds
    (an .ai, .uca, .ucs, .ucb) - the game's art for it, which saving copies
    into the mod"""
    files = Files(req['ztd'], req.get('engineDir'))
    out, seen = [], set()
    for k, n in files.names.items():
        if k.endswith('.ani'):
            folder, dirs = animaker.parse_ani(files.z.read(n))
            out.append({'ani': n.replace(chr(92), '/'), 'folder': folder, 'dirs': dirs, 'inMod': True})
            seen.add(k)
    if files.game:
        for k, n in list(files.names.items()):
            if not re.search(r'\.(ai|uca|ucs|ucb)$', k):
                continue
            for prefix in art_prefixes(k, files.z.read(n).decode('latin1')):
                for g in files.under(prefix):
                    if g.endswith('.ani') and g not in seen:
                        seen.add(g)
                        try:
                            folder, dirs = animaker.parse_ani(files.read(g))
                        except Exception:
                            continue
                        out.append({'ani': g, 'folder': folder, 'dirs': dirs, 'inMod': False})
    out.sort(key=lambda a: a['ani'].lower())
    return {'ok': True, 'anims': out}


def load(req):
    files = Files(req['ztd'], req.get('engineDir'))
    folder, dirs = animaker.parse_ani(files.read(req['ani']))
    out, pals, ms = [], {}, 0
    for d in dirs:
        art = animaker.parse_frames(files.read(folder + '/' + d))
        ms = ms or art['ms']
        if art['pal'] not in pals:
            try:
                pals[art['pal']] = hexes(animaker.read_pal(files.read(art['pal'])))
            except FileNotFoundError:
                pals[art['pal']] = ['#ff00ff'] * 256
        frames = []
        for f in art['frames']:
            px, mask = to_grid(f)
            # (the frame header's first offset is its ground point's y, the
            # second its x - src/Animation.cpp; animaker names them ox, oy)
            frames.append({'w': f['w'], 'h': f['h'], 'ox': f['oy'], 'oy': f['ox'], 'shadow': f.get('shadow', False),
                           'px': base64.b64encode(bytes(px)).decode(), 'mask': base64.b64encode(bytes(mask)).decode()})
        out.append({'name': d, 'pal': art['pal'], 'frames': frames, 'inMod': files.in_mod(folder + '/' + d)})
    return {'ok': True, 'folder': folder, 'ms': ms, 'dirs': out, 'palettes': pals,
            'palInMod': {p: files.in_mod(p) for p in pals}}


def write_entries(ztd, entries):
    """The .ztd again with these entries replaced or added, swapped in whole"""
    fd, tmp = tempfile.mkstemp(suffix='.ztd', dir=os.path.dirname(ztd))
    os.close(fd)
    try:
        want = {k.lower().replace(chr(92), '/'): (k, v) for k, v in entries.items()}
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as out:
            if os.path.exists(ztd):
                with zipfile.ZipFile(ztd) as z:
                    for info in z.infolist():
                        k = info.filename.lower().replace(chr(92), '/')
                        if k in want:
                            out.writestr(info.filename, want.pop(k)[1])
                        else:
                            out.writestr(info, z.read(info.filename))
            for k, (name, data) in want.items():
                out.writestr(name, data)
        os.replace(tmp, ztd)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)


def entry_of(c):
    return animaker.argb(c)


def save(req):
    files = Files(req['ztd'], req.get('engineDir'))
    folder, dirs = animaker.parse_ani(files.read(req['ani']))
    entries = {}
    for name, cols in (req.get('palettes') or {}).items():
        old = animaker.read_pal(files.read(name))
        new = [entry_of(c) for c in cols[:len(old)]]
        new[0] = old[0]  # (the clear one)
        entries[name] = animaker.write_pal(new)
    for d, changed in (req.get('frames') or {}).items():
        path = folder + '/' + d
        art = animaker.parse_frames(files.read(path))
        for i, g in changed.items():
            f = art['frames'][int(i)]
            from_grid(f, base64.b64decode(g['px']), base64.b64decode(g['mask']))
        entries[path] = animaker.write_frames(art)
    # (which of them take the place of the game's own: mod.json's "replaces")
    replaced = sorted(k.lower() for k in entries if k.lower().replace(chr(92), '/') in files.index())
    files.z and files.z.close()
    if entries:
        write_entries(req['ztd'], entries)
    return {'ok': True, 'wrote': sorted(entries), 'replaced': replaced}


def fade(req):
    """A pattern faded along the body in every frame under a folder: the from
    colours, from start (0 at the head) on, turned into the to colour - more
    of them the further back, all of them past end (a dither, the same in
    every frame, so it doesn't crawl)"""
    files = Files(req['ztd'], req.get('engineDir'))
    prefix = req['folder'].lower().rstrip('/') + '/'
    src, to = set(int(i) for i in req['from']), int(req['to'])
    start, end = float(req.get('start', 0.35)), float(req.get('end', 0.85))
    pal = (req.get('pal') or '').lower()
    names = set(files.names)
    if files.game:
        names.update(files.under(prefix))
    entries, changed = {}, 0
    bayer = [0, 8, 2, 10, 12, 4, 14, 6, 3, 11, 1, 9, 15, 7, 13, 5]
    for k in sorted(names):
        if not k.startswith(prefix) or os.path.splitext(k)[1]:
            continue
        try:
            art = animaker.parse_frames(files.read(k))
        except Exception:
            continue
        if pal and art['pal'].lower() != pal:
            continue
        hx, hy = HEAD.get(k.rsplit('/', 1)[-1].lower(), (0, 0))
        if not hx and not hy:
            continue
        touched = False
        for f in art['frames']:
            if f.get('shadow'):
                continue
            px, mask = to_grid(f)
            w, h = f['w'], f['h']
            xs = [i % w for i in range(w * h) if mask[i]]
            ys = [i // w for i in range(w * h) if mask[i]]
            if not xs:
                continue
            # (along the body: the drawn pixels' extent, head end 0, tail end 1)
            proj = [x * hx + y * hy for x, y in zip(xs, ys)]
            lo, hi = min(proj), max(proj)
            span = max(1e-6, hi - lo)
            # (the shadow kept: a patch of the picked colours touching the
            # bottom of what's drawn is the shadow on the ground, drawn in
            # the same black as the stripes - left as it is)
            keep = bytearray(w * h)
            if req.get('keepShadow', True):
                floor = max(ys) - 2
                stack = [i for i in range(w * h) if mask[i] and px[i] in src and i // w >= floor]
                for i in stack:
                    keep[i] = 1
                while stack:
                    i = stack.pop()
                    x, y = i % w, i // w
                    for j in ((i - 1) if x > 0 else -1, (i + 1) if x < w - 1 else -1, i - w, i + w):
                        if 0 <= j < w * h and not keep[j] and mask[j] and px[j] in src:
                            keep[j] = 1
                            stack.append(j)
            for i in range(w * h):
                if not mask[i] or px[i] not in src or keep[i]:
                    continue
                x, y = i % w, i // w
                t = 1 - ((x * hx + y * hy) - lo) / span
                amount = 0 if t <= start else 1 if t >= end else (t - start) / (end - start)
                if amount * 16 > bayer[(y % 4) * 4 + x % 4]:
                    px[i] = to
                    touched = True
            if touched:
                from_grid(f, px, mask)
        if touched:
            entries[k] = animaker.write_frames(art)
            changed += 1
    replaced = sorted(k for k in entries if k in files.index())
    files.z and files.z.close()
    if entries:
        write_entries(req['ztd'], entries)
    return {'ok': True, 'frames': changed, 'replaced': replaced}


def stripes(req):
    """Stripes across the body in every frame under a folder: the picked
    colours, between start and end along the body (0 the head, 1 the tail),
    in count bands - each band width of its stretch - made darker (each
    colour the palette's nearest to it times darkness, so the fur's shading
    stays), on the top depth of the body only (its back, not its legs)"""
    files = Files(req['ztd'], req.get('engineDir'))
    prefix = req['folder'].lower().rstrip('/') + '/'
    src = set(int(i) for i in req['from'])
    start, end = float(req.get('start', 0.45)), float(req.get('end', 0.95))
    count, width = max(1, int(req.get('count', 6))), float(req.get('width', 0.45))
    dark, depth = float(req.get('darkness', 0.45)), float(req.get('depth', 0.6))
    min_period = float(req.get('minPeriod', 4))
    pal_name = (req.get('pal') or '').lower()
    pal = [animaker.rgb(c) for c in animaker.read_pal(files.read(pal_name))] if pal_name else None
    # (each picked colour's darker one: the nearest in the palette to it,
    # darkened - the clear one never)
    darker = {}
    for i in src:
        r, g, b = (v * dark for v in pal[i])
        darker[i] = min(range(1, len(pal)), key=lambda k: (pal[k][0] - r) ** 2 + (pal[k][1] - g) ** 2 + (pal[k][2] - b) ** 2)
    names = set(files.names)
    if files.game:
        names.update(files.under(prefix))
    entries, changed = {}, 0
    for k in sorted(names):
        if not k.startswith(prefix) or os.path.splitext(k)[1]:
            continue
        try:
            art = animaker.parse_frames(files.read(k))
        except Exception:
            continue
        if pal_name and art['pal'].lower() != pal_name:
            continue
        hx, hy = HEAD.get(k.rsplit('/', 1)[-1].lower(), (0, 0))
        if not hx and not hy:
            continue
        touched = False
        for f in art['frames']:
            if f.get('shadow'):
                continue
            px, mask = to_grid(f)
            w, h = f['w'], f['h']
            drawn = [i for i in range(w * h) if mask[i] and px[i] in src]
            if not drawn:
                continue
            proj = [(i % w) * hx + (i // w) * hy for i in drawn]
            lo, hi = min(proj), max(proj)
            span = max(1e-6, hi - lo)
            # (each column's top and bottom: the back is the top part)
            tops, bots = {}, {}
            for i in range(w * h):
                if mask[i]:
                    x, y = i % w, i // w
                    tops[x] = min(tops.get(x, y), y)
                    bots[x] = max(bots.get(x, y), y)
            for i in drawn:
                x, y = i % w, i // w
                if y > tops[x] + depth * (bots[x] - tops[x] + 1):
                    continue
                t = 1 - ((x * hx + y * hy) - lo) / span
                if not start <= t <= end:
                    continue
                # (a stripe repeats every so many pixels along the body - never
                # under min_period: in a small frame, or one facing on a
                # slant, thinner bands step into a checkerboard)
                period = max(min_period, span * (end - start) / count)
                phase = (t - start) * span / period
                if phase - int(phase) < width:
                    px[i] = darker[px[i]]
                    touched = True
            if touched:
                from_grid(f, px, mask)
        if touched:
            entries[k] = animaker.write_frames(art)
            changed += 1
    replaced = sorted(k for k in entries if k in files.index())
    files.z and files.z.close()
    if entries:
        write_entries(req['ztd'], entries)
    return {'ok': True, 'frames': changed, 'replaced': replaced}


# ---- PNG (straight RGBA out; RGB, RGBA or palette PNGs in, 8 bits)
def png_write(path, w, h, rgba):
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def png_read(path):
    data = open(path, 'rb').read()
    if data[:8] != b'\x89PNG\r\n\x1a\n':
        raise ValueError('%s is not a PNG.' % path)
    pos, idat, plte, trns = 8, b'', None, None
    while pos < len(data):
        n, kind = struct.unpack('>I4s', data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + n]
        if kind == b'IHDR':
            w, h, depth, ctype, _, _, inter = struct.unpack('>IIBBBBB', body)
        elif kind == b'PLTE':
            plte = body
        elif kind == b'tRNS':
            trns = body
        elif kind == b'IDAT':
            idat += body
        pos += 12 + n
    if depth != 8 or inter or ctype not in (2, 3, 6):
        raise ValueError('%s: save it as an 8-bit RGB, RGBA or palette PNG (not interlaced).' % path)
    bpp = {2: 3, 3: 1, 6: 4}[ctype]
    raw = zlib.decompress(idat)
    stride = w * bpp
    out, prev = bytearray(), bytearray(stride)
    for y in range(h):
        ft = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ft == 1:
                line[i] = (line[i] + a) & 255
            elif ft == 2:
                line[i] = (line[i] + b) & 255
            elif ft == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ft == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        out += line
        prev = line
    rgba = bytearray(w * h * 4)
    for i in range(w * h):
        if ctype == 6:
            rgba[i * 4:i * 4 + 4] = out[i * 4:i * 4 + 4]
        elif ctype == 2:
            rgba[i * 4:i * 4 + 4] = out[i * 3:i * 3 + 3] + b'\xff'
        else:
            k = out[i]
            rgba[i * 4:i * 4 + 3] = plte[k * 3:k * 3 + 3]
            rgba[i * 4 + 3] = trns[k] if trns and k < len(trns) else 255
    return w, h, bytes(rgba)


def export(req):
    data = load(req)
    os.makedirs(req['out'], exist_ok=True)
    n = 0
    for d in data['dirs']:
        pal = [(int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16)) for c in data['palettes'][d['pal']]]
        for i, f in enumerate(d['frames']):
            px, mask = base64.b64decode(f['px']), base64.b64decode(f['mask'])
            rgba = bytearray(f['w'] * f['h'] * 4)
            for p in range(f['w'] * f['h']):
                if mask[p]:
                    r, g, b = (0, 0, 0) if f['shadow'] else pal[px[p]]
                    rgba[p * 4:p * 4 + 4] = bytes((r, g, b, 128 if f['shadow'] else 255))
            png_write(os.path.join(req['out'], '%s_%d.png' % (d['name'], i)), f['w'], f['h'], bytes(rgba))
            n += 1
    return {'ok': True, 'files': n, 'out': req['out']}


def import_(req):
    """Frames from PNGs (as export wrote them; the same sizes): each pixel
    the palette's nearest colour, clear where it's under half opaque"""
    data = load(req)
    frames, n = {}, 0
    for d in data['dirs']:
        pal = [(int(c[1:3], 16), int(c[3:5], 16), int(c[5:7], 16)) for c in data['palettes'][d['pal']]]
        near = {}
        for i, f in enumerate(d['frames']):
            p = os.path.join(req['src'], '%s_%d.png' % (d['name'], i))
            if not os.path.exists(p) or f['shadow']:
                continue
            w, h, rgba = png_read(p)
            if (w, h) != (f['w'], f['h']):
                raise ValueError('%s is %dx%d: the frame is %dx%d.' % (os.path.basename(p), w, h, f['w'], f['h']))
            px, mask = bytearray(w * h), bytearray(w * h)
            for q in range(w * h):
                r, g, b, a = rgba[q * 4:q * 4 + 4]
                if a < 128:
                    continue
                key = (r, g, b)
                if key not in near:
                    near[key] = min(range(1, len(pal)), key=lambda k: (pal[k][0] - r) ** 2 + (pal[k][1] - g) ** 2 + (pal[k][2] - b) ** 2)
                px[q], mask[q] = near[key], 1
            frames.setdefault(d['name'], {})[str(i)] = {'px': base64.b64encode(bytes(px)).decode(),
                                                        'mask': base64.b64encode(bytes(mask)).decode()}
            n += 1
    if not n:
        raise ValueError('No PNGs named like <direction>_<frame>.png in %s.' % req['src'])
    r = save({**req, 'frames': frames, 'palettes': {}})
    return {**r, 'frames': n}


def main():
    req = json.loads(sys.stdin.read())
    try:
        op = req.get('op')
        res = {'anims': anims, 'load': load, 'save': save, 'fade': fade, 'stripes': stripes, 'export': export, 'import': import_}[op](req)
    except KeyError as e:
        res = {'ok': False, 'error': 'No op %s.' % e}
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, (ValueError, FileNotFoundError)) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
