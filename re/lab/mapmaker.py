r"""The ZT1 Lab's map maker: a freeform map designed in the Test Zoo, packed
into a mod (Test Zoo > Save as a map).

The engine writes the design (SaveGame::saveMap: the zoo as it was made -
ground, paths, fences, objects - over the game's map it was loaded from);
here it goes into the mod's content.ztd with what the game needs to list and
start it, as the game's own freeform maps have:
  freeform/<id>.scn      the map it's over's .scn (its ambience, goals,
                         research), with [start] design= and [freeform]
                         title= (a mod has no string table entry for a name)
  freeform/<id>.zt1map   the design
  freeform/<id>.txt      its description (the map list shows it)
  freeform/<id>/         its preview: a picture of it (a frame as the game's
                         map previews are, 367 x 276, with its palette)

One JSON request on stdin, one JSON answer on stdout:
  {"engineDir", "ztd", "design": <the engine's file>, "scn": "freeform/ff01.scn",
   "id", "title", "about", "size": "Small|Medium|Large", "preview": <a PNG, or "">}
      -> {"ok": true, "scn", "files": [...]}
"""
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402
import artmaker  # noqa: E402

PREVIEW_W, PREVIEW_H = 367, 276


def set_key(text, section, key, value):
    """The .scn with key = value in [section] (replaced, or added at its top)"""
    lines = text.split('\n')
    out, inside, done = [], False, False
    for line in lines:
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            if inside and not done:
                out.insert(len(out) - (1 if out and not out[-1].strip() else 0), '%s=%s' % (key, value))
                done = True
            inside = m.group(1).strip().lower() == section
            out.append(line)
            continue
        if inside and line.split('=', 1)[0].strip().lower() == key.lower():
            if not done:
                out.append('%s=%s' % (key, value))
                done = True
            continue
        out.append(line)
    if inside and not done:
        out.append('%s=%s' % (key, value))
        done = True
    if not done:
        out += ['', '[%s]' % section, '%s=%s' % (key, value)]
    return '\n'.join(out)


def drop_key(text, section, key):
    out, inside = [], False
    for line in text.split('\n'):
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            inside = m.group(1).strip().lower() == section
        elif inside and line.split('=', 1)[0].strip().lower() == key.lower():
            continue
        out.append(line)
    return '\n'.join(out)


def preview_files(png, id_):
    """A picture as the game's map previews are: one frame (FATZ, its anchor
    in the middle - stored y first), its colours in a palette of its own"""
    w, h, rgba = artmaker.png_read(png)
    # (filling the preview's size: the picture's middle cut to its shape -
    # fitted whole, a wide view left bands above and below - each pixel the
    # average of the ones it covers)
    k = min(w / PREVIEW_W, h / PREVIEW_H)
    W, H = PREVIEW_W, PREVIEW_H
    cw, ch = PREVIEW_W * k, PREVIEW_H * k
    ox, oy = (w - cw) / 2, (h - ch) / 2
    small = []
    for y in range(H):
        y0 = int(oy + y * ch / H)
        y1 = max(y0 + 1, int(oy + (y + 1) * ch / H))
        for x in range(W):
            x0 = int(ox + x * cw / W)
            x1 = max(x0 + 1, int(ox + (x + 1) * cw / W))
            r = g = b = n = 0
            for yy in range(y0, y1):
                for xx in range(x0, x1):
                    p = (yy * w + xx) * 4
                    r += rgba[p]
                    g += rgba[p + 1]
                    b += rgba[p + 2]
                    n += 1
            small.append((r // n, g // n, b // n))
    # (its palette: a 6 x 7 x 6 colour cube - every picture fits it - and
    # entry 0, the clear one, left unused)
    cube = [(int(r * 51), int(g * 42.5), int(b * 51)) for r in range(6) for g in range(7) for b in range(6)]
    pal = [0] + [animaker.entry(*c) for c in cube]
    pal += [0xFF000000] * (256 - len(pal))
    px = bytearray(W * H)
    for i, (r, g, b) in enumerate(small):
        px[i] = 1 + (round(r / 51) * 42 + round(g / 42.5) * 6 + round(b / 51))
    frame = {'w': W, 'h': H, 'ox': H // 2, 'oy': W // 2, 'flags': 0, 'shadow': False}
    artmaker.from_grid(frame, px, bytearray([1]) * (W * H))
    folder = 'freeform/%s' % id_
    art = {'ms': 1000, 'pal': folder + '/' + id_ + '.pal', 'frames': [frame], 'head': b'FATZ\0\0\0\0\0', 'extra': 0}
    ani = ('[animation]\r\ndir0 = freeform\r\ndir1 = %s\r\nanimation = N\r\nx0 = %d\r\ny0 = %d\r\nx1 = %d\r\ny1 = %d\r\n'
           % (id_, -(W // 2), -(H // 2), W - W // 2, H - H // 2)).encode('latin1')
    return {folder + '/' + id_ + '.ani': ani, folder + '/N': animaker.write_frames(art),
            folder + '/' + id_ + '.pal': animaker.write_pal(pal)}, folder + '/' + id_


def make(req):
    id_ = re.sub(r'[^a-z0-9]', '', req['id'].lower())[:16] or 'mymap'
    game = animaker.Game(req['engineDir'])
    scn = game.read(req['scn']).decode('latin1').replace('\r\n', '\n')
    design = open(req['design'], 'rb').read()
    if design[:4] != b'S1MP':
        raise ValueError("The engine's design file isn't a map design.")
    files = {}
    scn = set_key(scn, 'start', 'design', 'freeform/%s.zt1map' % id_)
    scn = set_key(scn, 'freeform', 'title', req['title'].strip() or id_)
    scn = set_key(scn, 'freeform', 'size', req.get('size') or 'Medium')
    # (its name is its title: the game's string number for the map it's over goes)
    scn = drop_key(scn, 'freeform', 'name')
    scn = drop_key(scn, 'freeform', 'description')
    # (made from nothing, a blank map: its size, and no map of the game's
    # under it - its goals, research and ambience still the .scn's)
    blank = str(req.get('blank') or '').strip()
    if blank:
        if not re.fullmatch(r'\d{2,3} \d{2,3}', blank):
            raise ValueError('A blank map\'s size is "w h".')
        scn = set_key(scn, 'start', 'blank', blank)
        scn = drop_key(scn, 'start', 'savegame')
    if req.get('preview') and os.path.exists(req['preview']):
        pics, icon = preview_files(req['preview'], id_)
        files.update(pics)
        scn = set_key(scn, 'freeform', 'icon', icon)
    head = ('; %s: a freeform map made in the ZT1 Lab, %s.\n; [start] design= is the map as it was made '
            '(its ground, paths, fences, objects).\n\n') % (
        req['title'], ('from a blank map, ' + blank.replace(' ', ' x ')) if blank else 'over ' + req['scn'])
    files['freeform/%s.scn' % id_] = (head + scn).replace('\n', '\r\n').encode('latin1', 'replace')
    files['freeform/%s.zt1map' % id_] = design
    files['freeform/%s.txt' % id_] = (req.get('about') or req['title']).replace('\n', '\r\n').encode('latin1', 'replace')
    artmaker.write_entries(req['ztd'], files)
    return {'ok': True, 'scn': 'freeform/%s.scn' % id_, 'files': sorted(files)}


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = make(req)
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, (ValueError, FileNotFoundError)) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
