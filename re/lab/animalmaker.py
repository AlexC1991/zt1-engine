r"""The ZT1 Lab's animal maker: a new animal made from scratch, its
animations drawn in the Lab (Mods > a mod > Animals), as a mod.

A new animal starts as its species file alone, animals/<type>.uca: a name,
its own type and number, and the needs, biome and behaviour of an animal
you pick for it to behave like (all of which you change). It has no art of
its own yet: each of its animations (walk, run, eat, sleep, ...) is LINKED -
its line in [m/Animations] is a whole path to an animation that exists
(walk = animals/zebra/m/walk/walk.ani), the way the game's own files point
at shared art (box_idle = objects/box/idle/idle.ani) - so it works in the
game from the start. Each one you DRAW (the editor: layers, frames, one set
for each way it faces) is saved as the game's own art under
animals/<type>/<sub>/<anim>/ and its line points there; any line can be
linked to any animation, the game's or one you drew, at any time.

What you draw is kept as you drew it (layers, full colour) in the mod's
.art/animals/<type>/<sub>/<anim>.json (left out of uploads, as every
dot-folder is); saving makes the game's art from it: every drawn animation
of the animal shares one 256-colour palette, animals/<type>/<type>.pal
(colour 0 clear, colour 1 the ground shadow - a Shadow layer's pixels -
and the rest made from every colour drawn), so the palette is made again
from all of them and each one's frames written again.

The art's format (src/AniFile.cpp, re/lab/animaker.py): a direction's file
holds its frames, each cut to what's drawn, with the ground point's place
in it (x, y); the .ani names the folder and the directions drawn - N, NE,
E, SE, S, the game mirroring the other three - and the box round them all.

One JSON request on stdin, one JSON answer on stdout:
  {"op": "new", "ztd", "engineDir", "name", "type"?, "like": "animals/zebra.ai"}
      -> {"ok", "type", "species", "number"}
  {"op": "animals", "ztd", "engineDir"} -> {"ok", "animals": [{type, name, species}]}
  {"op": "slots", "ztd", "engineDir", "type"}
      -> {"ok", "name", "subs": {"m": [{slot, value, path, status, dirs, frames, ms}]}, "femaleOwn"}
  {"op": "link", "ztd", "engineDir", "type", "sub", "slot", "target"} -> {"ok"}
  {"op": "load", "ztd", "engineDir", "type", "sub", "anim", "projects"}
      -> {"ok", "project" | null, "reference": {w, h, ax, ay, ms, dirs: {N: [png...]}} | null}
  {"op": "save", "ztd", "engineDir", "type", "sub", "anim", "project", "projects"}
      -> {"ok", "colours", "merged", "animations", "wrote"}
"projects" is the mod's .art folder.
"""
import base64
import hashlib
import io
import json
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402
import artmaker  # noqa: E402

from PIL import Image  # noqa: E402

DIRS = ['N', 'NE', 'E', 'SE', 'S']
SUBS = ['m', 'f', 'y']
SHADOW = 1  # (palette colour 1: the ground shadow drawn under an animal)


# ------------------------------------------------------------ species file
def sections(text):
    """[(name lower, [lines])] in order, the head (before any section) as ''"""
    out, cur, lines = [], '', []
    for line in text.replace('\r\n', '\n').split('\n'):
        m = re.match(r'\s*\[([^\]]+)\]\s*$', line)
        if m:
            out.append((cur, lines))
            cur, lines = m.group(1).strip(), [line]
        else:
            lines.append(line)
    out.append((cur, lines))
    return out


def join(secs):
    return '\r\n'.join(line for _, lines in secs for line in lines).rstrip('\r\n') + '\r\n'


def value_of(lines, key):
    for line in lines:
        if '=' in line and line.split('=', 1)[0].strip().lower() == key:
            return line.split('=', 1)[1].split(';')[0].strip()
    return ''


def anim_lines(secs, sub):
    """The [sub/Animations] slot -> value lines (none: the male's)"""
    for name, lines in secs:
        if name.lower() == sub + '/animations':
            return name, lines
    return None, None


def type_of(text, fallback=''):
    for name, lines in sections(text):
        if name.lower() == 'global':
            return value_of(lines, 'type').lower() or fallback
    return fallback


def anim_path(value, base):
    """An [x/Animations] line's value as an animation's path (no .ani)"""
    v = value.split(';')[0].strip().lower()
    if not v:
        return ''
    if '/' in v:
        return v[:-4] if v.endswith('.ani') else v
    return base + '/' + v + '/' + v


def species(req, files):
    t = req['type'].lower()
    data = files.read('animals/' + t + '.uca')
    return data.decode('latin1')


def write_species(ztd, t, text):
    artmaker.write_entries(ztd, {'animals/' + t + '.uca': text.encode('latin1')})


def new(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    like = req['like'].lower()
    src = files.read(like).decode('latin1')
    old = type_of(src, os.path.splitext(os.path.basename(like))[0])
    name = req['name'].strip()
    t = (req.get('type') or re.sub(r'[^a-z0-9]', '', name.lower()))[:8]
    if not t:
        raise ValueError('Give it a name with a letter or number in it.')
    for ext in ('.ai', '.uca'):
        try:
            files.read('animals/' + t + ext)
            raise ValueError('There is already an animal called "%s" (in the game or a mod on): give it another name.' % t)
        except FileNotFoundError:
            pass
    number = 19000 + int(hashlib.sha1(t.encode()).hexdigest(), 16) % 900
    secs = sections(src)
    # (where the animal it behaves like keeps its art, for its own lines'
    # whole paths: linked to that art until it has its own)
    paths = {}
    for nm, lines in secs:
        if nm.lower() == 'animpath':
            for s in SUBS:
                paths[s] = value_of(lines, s).lower()
    paths.setdefault('m', 'animals/%s/m' % old)
    out = []
    for nm, lines in secs:
        low = nm.lower()
        if low == 'global':
            lines = ['Type = ' + t if '=' in ln and ln.split('=', 1)[0].strip().lower() == 'type' else ln for ln in lines]
        elif low == 'animpath':
            # (its own folders, for what's drawn; the female shares the male's
            # art, as most of the game's animals do, until she has her own)
            lines = [lines[0], 'm = animals/%s/m' % t, 'f = animals/%s/m' % t, 'y = animals/%s/y' % t, '']
        elif re.fullmatch(r'[mfy]/animations', low):
            s = low[0]
            base = paths.get(s) or paths['m']
            fixed = [lines[0]]
            for ln in lines[1:]:
                if '=' in ln and not ln.strip().startswith(';'):
                    k, v = ln.split('=', 1)
                    p = anim_path(v, base)
                    fixed.append('%s = %s.ani' % (k.strip(), p) if p else ln)
                else:
                    fixed.append(ln)
            lines = fixed
        elif re.fullmatch(r'[mfy]/characteristics/integers', low):
            lines = ['cNameID = %d' % number if '=' in ln and ln.split('=', 1)[0].strip().lower() == 'cnameid'
                     else ln for ln in lines]
        elif low in ('defaultlcid', '1033'):
            continue
        out.append((nm, lines))
    head = ['; %s: a new animal, made in the ZT1 Lab. It behaves like the game\'s %s' % (name, old),
            '; (its needs, biome, behaviour and sounds - change any of them). Its',
            '; animations are linked to the %s\'s until you draw its own (Mods > Animals).' % old, '']
    out[0] = ('', head + out[0][1])
    out.append(('defaultlcid', ['', '[defaultLCID]', 'LCID = 1033']))
    out.append(('1033', ['', '; Its own name (a section for each language: 1033 is English)', '[1033]',
                         'cName = %s' % name, 'cLongHelp = Adopt a %s' % name, 'cTheString = the %s' % name]))
    files.z and files.z.close()
    write_species(req['ztd'], t, join(out))
    return {'ok': True, 'type': t, 'species': 'animals/%s.uca' % t, 'number': number, 'like': old}


def animals(req):
    """The mod's own animals: its .uca species files"""
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    out = []
    for k, n in sorted(files.names.items()):
        if re.fullmatch(r'animals/[^/]+\.uca', k):
            text = files.z.read(n).decode('latin1')
            nm = ''
            for s, lines in sections(text):
                if s == '1033':
                    nm = value_of(lines, 'cname')
            out.append({'type': type_of(text, k[8:-4]), 'name': nm or k[8:-4], 'species': k})
    return {'ok': True, 'animals': out}


def anim_info(files, path):
    """An animation's directions, frames and frame time (none: missing)"""
    try:
        folder, dirs = animaker.parse_ani(files.read(path + '.ani'))
    except (FileNotFoundError, KeyError):
        return None
    frames, ms = 0, 0
    try:
        art = animaker.parse_frames(files.read(folder + '/' + dirs[0]))
        frames, ms = len(art['frames']) - art['extra'], art['ms']
    except Exception:
        pass
    return {'dirs': dirs, 'frames': frames, 'ms': ms}


def slots(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    t = req['type'].lower()
    text = species(req, files)
    secs = sections(text)
    paths = {}
    for nm, lines in secs:
        if nm.lower() == 'animpath':
            for s in SUBS:
                paths[s] = value_of(lines, s).lower()
    own = 'animals/%s/' % t
    subs = {}
    for s in SUBS:
        nm, lines = anim_lines(secs, s)
        if nm is None:
            continue
        rows = []
        for ln in lines[1:]:
            if '=' not in ln or ln.strip().startswith(';'):
                continue
            k, v = [x.strip() for x in ln.split('=', 1)]
            p = anim_path(v, paths.get(s) or paths.get('m', ''))
            info = anim_info(files, p) if p else None
            status = 'missing' if not info else 'drawn' if p.startswith(own) and files.in_mod(p + '.ani') else 'linked'
            rows.append({'slot': k, 'value': v.split(';')[0].strip(), 'path': p, 'status': status, **(info or {})})
        subs[s] = rows
    name = ''
    for s, lines in secs:
        if s == '1033':
            name = value_of(lines, 'cname')
    # (its kind, and what that kind needs from its file: a marine animal's
    # underwater behaviour, a dinosaur's egg)
    n = needs_of(text)
    names = set(nm.lower() for nm, _ in secs)
    slots_m = set(r['slot'].lower() for r in subs.get('m', []))
    return {'ok': True, 'type': t, 'name': name or t, 'subs': subs,
            'femaleOwn': anim_lines(secs, 'f')[0] is not None, 'kind': n['kind'],
            'underwater': 'ambientanimsunderwater' in names,
            'egg': all(e in slots_m for e in ('egg_idle', 'egg_hatch', 'egg_break'))}


def set_slot(text, sub, slot, value):
    """The species file with [sub/Animations] slot = value (added if new; a
    female's list made from the male's when she gets one of her own)"""
    secs = sections(text)
    nm, lines = anim_lines(secs, sub)
    if nm is None:
        if sub == 'm':
            raise ValueError('It has no [m/Animations] list.')
        _, male = anim_lines(secs, 'm')
        secs.append((sub + '/Animations', ['', '[%s/Animations]' % sub] + [ln for ln in (male or [])[1:] if ln.strip()]))
        nm, lines = anim_lines(secs, sub)
    for i, ln in enumerate(lines):
        if '=' in ln and not ln.strip().startswith(';') and ln.split('=', 1)[0].strip().lower() == slot.lower():
            lines[i] = '%s = %s' % (ln.split('=', 1)[0].strip(), value)
            break
    else:
        # (after its last line that says something)
        at = max(i for i, ln in enumerate(lines) if ln.strip()) + 1
        lines.insert(at, '%s = %s' % (slot, value))
    return join(secs)


def link(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    t, sub, slot = req['type'].lower(), req['sub'], req['slot'].strip()
    if not re.fullmatch(r'[A-Za-z0-9_]+', slot):
        raise ValueError('An animation\'s name is letters, numbers and _ (walk, lie_down).')
    target = req['target'].strip().lower()
    target = target[:-4] if target.endswith('.ani') else target
    if not anim_info(files, target):
        raise ValueError('There is no animation %s.' % target)
    text = set_slot(species(req, files), sub, slot, target + '.ani')
    files.z and files.z.close()
    write_species(req['ztd'], t, text)
    return {'ok': True}


def stencils(req):
    """Any item's animations, to draw over or remake: {sub: [{slot, path,
    frames}]} - only the ones that exist. An animal's (or a mod's animal's)
    by its [x/Animations] lines; anything else - a building, tree, rock,
    fence, member of staff, a guest ("guests/man") - every animation in its
    art folder: objects/<name>/, fences/<name>/, staff/<name>/, guests/<kind>/
    by the game's own layout, else the folders its file names (a palette's)"""
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    like = req['like'].strip().lower().replace(chr(92), '/')
    text = ''
    if not like.startswith('guests/') or like.endswith('.ai'):
        text = files.read(like).decode('latin1')
    subs = {}
    if like.startswith('animals/'):
        secs = sections(text)
        paths = {}
        for nm, lines in secs:
            if nm.lower() == 'animpath':
                for s in SUBS:
                    paths[s] = value_of(lines, s).lower()
        for s in SUBS:
            nm, lines = anim_lines(secs, s)
            if nm is None:
                continue
            rows, seen = [], set()
            for ln in lines[1:]:
                if '=' not in ln or ln.strip().startswith(';'):
                    continue
                k, v = [x.strip() for x in ln.split('=', 1)]
                p = anim_path(v, paths.get(s) or paths.get('m', ''))
                if not p or p in seen or p.startswith('objects/'):
                    continue
                info = anim_info(files, p)
                if info:
                    seen.add(p)
                    rows.append({'slot': k, 'path': p, 'frames': info['frames']})
            subs[s] = rows
    if not any(subs.values()):
        subs = {'m': folder_rows(files, like, text)}
    return {'ok': True, 'subs': subs}


def folder_rows(files, like, text):
    """Every animation under an item's art folder, named by where it is
    (idle, f/idle, m/walk) - its buy-panel icons and list pictures left out"""
    names = set(files.index()) | set(files.names)
    stem = os.path.splitext(os.path.basename(like.rstrip('/')))[0]
    top = like.split('/')[0]
    roots = [like.rstrip('/') + '/' if top == 'guests' else
             '%s/%s/' % ({'fences': 'fences', 'staff': 'staff'}.get(top, 'objects'), stem)]
    for m in re.finditer(r'(?i)\b((?:objects|staff|fences|guests)/[a-z0-9_]+)/', text):
        roots.append(m.group(1).lower() + '/')
    for root in roots:
        rows = []
        for n in sorted(k for k in names if k.startswith(root) and k.endswith('.ani')):
            parts = n[len(root):-4].split('/')
            if len(parts) >= 2 and parts[-1] == parts[-2]:
                parts = parts[:-1]
            info = anim_info(files, n[:-4])
            if not info:
                continue
            # (a one-frame picture named as the game names its icons, list
            # and info pictures: not an animation of the thing itself)
            if info['frames'] <= 1 and re.match(r'(ic|ls|pl|in|sm)[a-z]', parts[-1]) and parts[-1] != 'idle':
                continue
            rows.append({'slot': '/'.join(parts), 'path': n[:-4], 'frames': info['frames']})
        if rows:
            return rows
    return []


# ------------------------------------------------------------ its needs
# What an animal needs and likes, as its species file says it (the same keys
# and lists the game's .ai files use):
#   kind      its [Member]: "aqua" Marine Mania, "dinosaur" Dinosaur Digs,
#             neither a zoo animal
#   keeper    cKeeper: who looks after it (9551 a paleontologist, 9552 a
#             marine specialist; none: a keeper)
#   food      cKeeperFoodType: herbivore / carnivore, and which food (food.cfg's
#             order, Dinosaur Digs' and Marine Mania's after: 0 herbivore chow
#             ... 13 sea urchins)
#   habitat, location, family, genus, needShelter: their cKeys
#   terrain   [cCompatibleTerrain]: (ground type, how much it likes it)
#   objects   [cSuitableObjects]: (an item's or a biome's text number, how
#             much) - its plants, rocks, shelters, toys, biomes
#   animals   [cCompatibleAnimals]: (a family's or a species' text number, how
#             much it likes living with them)
INT_SECS = ('m/characteristics/integers', 'characteristics/integers')
STR_SECS = ('m/characteristics/strings', 'characteristics/strings')
NEED_INTS = ('cKeeper', 'cHabitat', 'cLocation', 'cFamily', 'cGenus', 'cNeedShelter')
LISTS = {'terrain': 'cCompatibleTerrain', 'objects': 'cSuitableObjects', 'animals': 'cCompatibleAnimals'}


def sec_value(secs, names, key):
    for want in names:
        for nm, lines in secs:
            if nm.lower().replace(chr(92), '/') == want:
                v = value_of(lines, key.lower())
                if v != '':
                    return v
    return ''


def sec_pairs(secs, name):
    for nm, lines in secs:
        if nm.lower() == name.lower():
            vs = []
            for ln in lines[1:]:
                m = re.match(r'\s*v\s*=\s*(-?\d+)', ln)
                if m:
                    vs.append(int(m.group(1)))
            return [[vs[i], vs[i + 1]] for i in range(0, len(vs) - 1, 2)]
    return []


def entity_text(req, files):
    """The species file the needs are in: a new animal's .uca, or the game
    animal (.ai) a mod changes"""
    f = (req.get('file') or 'animals/%s.uca' % req['type'].lower()).replace(chr(92), '/')
    return f, files.read(f).decode('latin1')


def needs_of(text):
    secs = sections(text)
    mem = []
    for nm, lines in secs:
        if nm.lower() == 'member':
            mem = [ln.strip().lower() for ln in lines[1:] if ln.strip() and not ln.strip().startswith(';')]
    num = lambda k: (lambda v: int(v) if re.fullmatch(r'-?\d+', v) else None)(sec_value(secs, INT_SECS, k))
    out = {'kind': 'dinosaur' if 'dinosaur' in mem else 'aqua' if 'aqua' in mem else 'zoo', 'members': mem,
           'food': {'type': sec_value(secs, STR_SECS, 'ckeeperfoodtype').lower(), 'chow': num('cKeeperFoodType'),
                    'custom': sec_value(secs, STR_SECS, 'ckeeperfood').lower()}}
    for k in NEED_INTS:
        out[k[1].lower() + k[2:]] = num(k)
    for k, sec in LISTS.items():
        out[k] = sec_pairs(secs, sec)
    return out


def set_int(secs, key, value):
    for want in INT_SECS:
        for nm, lines in secs:
            if nm.lower().replace(chr(92), '/') == want:
                for i, ln in enumerate(lines):
                    if '=' in ln and not ln.strip().startswith(';') and ln.split('=', 1)[0].strip().lower() == key.lower():
                        lines[i] = '%s = %s' % (key, value)
                        return
    for want in INT_SECS:
        for nm, lines in secs:
            if nm.lower().replace(chr(92), '/') == want:
                lines.insert(1, '%s = %s' % (key, value))
                return
    secs.append(('m/Characteristics/Integers', ['', '[m/Characteristics/Integers]', '%s = %s' % (key, value)]))


def set_str(secs, key, value):
    for want in STR_SECS:
        for nm, lines in secs:
            if nm.lower().replace(chr(92), '/') == want:
                for i, ln in enumerate(lines):
                    if '=' in ln and ln.split('=', 1)[0].strip().lower() == key.lower():
                        lines[i] = '%s = %s' % (key, value)
                        return
                lines.insert(1, '%s = %s' % (key, value))
                return
    secs.append(('m/Characteristics/Strings', ['', '[m/Characteristics/Strings]', '%s = %s' % (key, value)]))


def set_pairs(secs, name, pairs):
    body = []
    for a, b in pairs:
        body += ['v = %d' % int(a), 'v = %d' % int(b), '']
    for i, (nm, lines) in enumerate(secs):
        if nm.lower() == name.lower():
            secs[i] = (nm, [lines[0]] + body)
            return
    # (a list it hadn't: after its [m/Characteristics/Mixed] header, or at the end)
    secs.append((name, ['', '[%s]' % name] + body))


def set_kind(secs, kind):
    for i, (nm, lines) in enumerate(secs):
        if nm.lower() == 'member':
            keep = [ln for ln in lines[1:] if ln.strip().lower() not in ('aqua', 'dinosaur')]
            while keep and not keep[-1].strip():
                keep.pop()
            add = {'aqua': ['aqua'], 'dinosaur': ['dinosaur']}.get(kind, [])
            secs[i] = (nm, [lines[0]] + keep + add + [''])
            return


def write_needs(text, n):
    secs = sections(text)
    if n.get('kind') in ('zoo', 'aqua', 'dinosaur'):
        set_kind(secs, n['kind'])
    food = n.get('food') or {}
    if food.get('type') in ('herbivore', 'carnivore'):
        set_str(secs, 'cKeeperFoodType', food['type'])
    if isinstance(food.get('chow'), int):
        set_int(secs, 'cKeeperFoodType', food['chow'])
    if 'custom' in food:
        custom = food['custom'] or ''
        if custom and not re.fullmatch(r'[a-z0-9_]{1,24}', custom):
            raise ValueError('Choose a custom food from the list.')
        set_str(secs, 'cKeeperFood', custom)
    for k in NEED_INTS:
        v = n.get(k[1].lower() + k[2:])
        if isinstance(v, int):
            set_int(secs, k, v)
    for k, sec in LISTS.items():
        if isinstance(n.get(k), list):
            set_pairs(secs, sec, n[k])
    return join(secs)


def needs(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    f, text = entity_text(req, files)
    return {'ok': True, 'file': f, **needs_of(text)}


def setneeds(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    f, text = entity_text(req, files)
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], {f: write_needs(text, req['needs']).encode('latin1')})
    return {'ok': True, 'file': f}


def copyneeds(req):
    """Another animal's needs - its food, home, ground, plants, rocks,
    shelters and neighbours - as this one's (its kind and keeper too, if
    asked): a preset"""
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    src = needs_of(files.read(req['from']).decode('latin1'))
    f, text = entity_text(req, files)
    keep = {k: src[k] for k in ('food', 'habitat', 'location', 'needShelter', 'terrain', 'objects', 'animals')}
    if req.get('kindToo'):
        keep.update({'kind': src['kind'], 'keeper': src['keeper']})
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], {f: write_needs(text, keep).encode('latin1')})
    return {'ok': True, 'file': f}


# ------------------------------------------------------------ drawing
def project_path(req, t, sub, anim):
    return os.path.join(req['projects'], 'animals', t, sub, anim + '.json')


def rendered(files, path, definition=None):
    """An animation drawn on one canvas per direction: {w, h, ax, ay, ms,
    dirs: {N: [PNG base64 ...]}} - the game's art as a starting point or a
    guide under what's drawn"""
    info = anim_info(files, path)
    if not info:
        return None
    folder, dirs = animaker.parse_ani(files.read(path + '.ani'))
    arts = {}
    for d in dirs:
        try:
            arts[d] = animaker.parse_frames(files.read(folder + '/' + d))
        except Exception:
            pass
    if not arts:
        return None
    # (one canvas for every frame: the box round them all, the ground point
    # where it is in each, with room round it)
    # (a frame header's first offset is its ground point's y, the second
    # its x - src/Animation.cpp; animaker names them ox, oy in that order)
    x0 = min(-f['oy'] for a in arts.values() for f in a['frames'])
    y0 = min(-f['ox'] for a in arts.values() for f in a['frames'])
    x1 = max(f['w'] - f['oy'] for a in arts.values() for f in a['frames'])
    y1 = max(f['h'] - f['ox'] for a in arts.values() for f in a['frames'])
    pad = 8
    w, h = x1 - x0 + 2 * pad, y1 - y0 + 2 * pad
    ax, ay = -x0 + pad, -y0 + pad
    pals = {}
    out = {}
    for d, art in arts.items():
        if art['pal'] not in pals:
            try:
                pals[art['pal']] = [animaker.rgb(c) for c in animaker.read_pal(files.read(art['pal']))]
            except FileNotFoundError:
                pals[art['pal']] = [(255, 0, 255)] * 256
        pal = pals[art['pal']]
        if definition:
            secs = sections(definition)
            full = sec_value(secs, ('cr_color',), 'fullpal').lower()
            if full == art['pal'].lower():
                # Building colour slots are filled from building.ai in game.
                # Bake those default colours into a remake's own RGBA drawing.
                at = int(sec_value(secs, ('cr_color',), 'ncolors') or 232)
                parts, defaults = [], []
                for nm, lines in secs:
                    if nm.lower() == 'colorrep':
                        for ln in lines:
                            if '=' not in ln or ln.strip().startswith(';'):
                                continue
                            k, v = [x.strip() for x in ln.split('=', 1)]
                            if k.lower() == 'replace': parts.append(v.lower())
                            if k.lower() == 'defaultpal': defaults.append(v.lower())
                shared = sections(files.read('building.ai').decode('latin1')) if parts else []
                pal = list(pal)
                for part, default in zip(parts, defaults):
                    count = int(sec_value(shared, (part,), 'ncolors') or 16)
                    replacement = [animaker.rgb(c) for c in animaker.read_pal(files.read(default))]
                    for i in range(count):
                        if at + i < len(pal) and i + 1 < len(replacement):
                            pal[at + i] = replacement[i + 1]
                    at += count
        frames = []
        def paint(img, f):
            px = img.load()
            ox, oy = ax - f['oy'], ay - f['ox']
            for y, runs in enumerate(f['lines']):
                x = 0
                for skip, cols in runs:
                    x += skip
                    for c in cols:
                        X, Y = ox + x, oy + y
                        if 0 <= X < w and 0 <= Y < h:
                            if f['shadow']:
                                px[X, Y] = (0, 0, 0, 110)
                            else:
                                r, g, b = pal[c] if c < len(pal) else (255, 0, 255)
                                px[X, Y] = (r, g, b, 140 if c == SHADOW else 255)
                        x += 1
        for f in art['frames'][:len(art['frames']) - art['extra']]:
            img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
            if art['extra']:
                paint(img, art['frames'][-1])
            paint(img, f)
            buf = io.BytesIO()
            img.save(buf, 'PNG')
            frames.append(base64.b64encode(buf.getvalue()).decode())
        out[d] = frames
    return {'w': w, 'h': h, 'ax': ax, 'ay': ay, 'ms': info['ms'] or 100, 'dirs': out, 'from': path}


def load(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    t, sub, anim = req['type'].lower(), req['sub'], req['anim']
    proj = None
    p = project_path(req, t, sub, anim)
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            proj = json.load(f)
    # (what the slot shows now - linked art, or the game's for that name -
    # as a guide to draw over, or to start from)
    ref = None
    src = req.get('reference')
    if src:
        ref = rendered(files, src[:-4] if src.endswith('.ani') else src)
    return {'ok': True, 'project': proj, 'reference': ref}


def flatten(proj, frame):
    """A frame's layers as one picture: (RGBA image, shadow mask) - a
    layer marked shadow is the ground shadow (colour 1), under the rest"""
    w, h = proj['w'], proj['h']
    img = Image.new('RGBA', (w, h), (0, 0, 0, 0))
    shadow = Image.new('L', (w, h), 0)
    for i, layer in enumerate(proj['layers']):
        if not layer.get('visible', True):
            continue
        cell = frame['cells'][i] if i < len(frame['cells']) else None
        if not cell:
            continue
        c = Image.open(io.BytesIO(base64.b64decode(cell))).convert('RGBA')
        if c.size != (w, h):
            c = c.crop((0, 0, w, h))
        a = c.getchannel('A').point(lambda v: 255 if v >= 128 else 0)
        if layer.get('shadow'):
            shadow.paste(255, mask=a)
        else:
            solid = c.copy()
            solid.putalpha(a)
            img.alpha_composite(solid)
    return img, shadow


def compile_all(req, t, files):
    """Every drawn animation of the animal written as the game's art, on one
    palette made from all of them"""
    root = os.path.join(req['projects'], req.get('projectRoot') or 'animals/' + t)
    asset_root = req.get('assetRoot') or 'animals/' + t
    directions = req.get('directions') or DIRS
    projects = []
    for sub in req.get('subtypes') or SUBS:
        d = os.path.join(root, sub)
        if not os.path.isdir(d):
            continue
        for fn in sorted(os.listdir(d)):
            if fn.endswith('.json'):
                with open(os.path.join(d, fn), encoding='utf-8') as f:
                    projects.append((sub, fn[:-5], json.load(f)))
    flat = []  # (sub, anim, proj, {dir: [(img, shadow)]})
    colours = {}
    for sub, anim, proj in projects:
        per = {}
        for d in directions:
            frames = (proj.get('dirs') or {}).get(d) or []
            if not frames:
                continue
            per[d] = [flatten(proj, fr) for fr in frames]
            for img, _ in per[d]:
                for count, c in (img.getcolors(1 << 20) or []):
                    if c[3]:
                        colours[c[:3]] = colours.get(c[:3], 0) + count
        if per:
            flat.append((sub, anim, proj, per))
    # The palette: 0 clear, 1 the shadow, then the colours drawn - all of
    # them if they fit, else 254 made from them (the most used kept best)
    pal = [(0, 0, 0), (0, 0, 0)]
    merged = 0
    if len(colours) <= 254:
        pal += sorted(colours)
        lookup = {c: i + 2 for i, c in enumerate(sorted(colours))}
    else:
        merged = len(colours) - 254
        strip = Image.new('RGB', (len(colours), 1))
        strip.putdata(list(colours))
        q = strip.quantize(254, method=Image.Quantize.MEDIANCUT)
        qp = q.getpalette()[:254 * 3]
        pal += [tuple(qp[i:i + 3]) for i in range(0, len(qp), 3)]
        idx = list(q.getdata())
        lookup = {c: idx[i] + 2 for i, c in enumerate(colours)}
    pal += [(0, 0, 0)] * (256 - len(pal))
    pal_path = '%s/%s.pal' % (asset_root, t)
    entries = {pal_path: animaker.write_pal([0] + [animaker.entry(*c) for c in pal[1:]])}
    done = []
    for sub, anim, proj, per in flat:
        folder = '%s/%s/%s' % (asset_root, sub, anim)
        ax, ay = proj['ax'], proj['ay']
        box = [None, None, None, None]
        for d, frames in per.items():
            art = {'ms': int(proj.get('ms') or 100), 'pal': pal_path, 'frames': [], 'head': b'', 'extra': 0}
            for img, shadow in frames:
                w, h = img.size
                src = img.load()
                sh = shadow.load()
                bbox = None
                xs = [x for y in range(h) for x in range(w) if src[x, y][3] or sh[x, y]]
                if xs:
                    ys = [y for y in range(h) for x in range(w) if src[x, y][3] or sh[x, y]]
                    bbox = (min(xs), min(ys), max(xs) + 1, max(ys) + 1)
                if not bbox:
                    f = {'w': 1, 'h': 1, 'ox': 0, 'oy': 0, 'flags': 1, 'shadow': False}
                    artmaker.from_grid(f, bytes(1), bytes(1))
                    art['frames'].append(f)
                    continue
                bx0, by0, bx1, by1 = bbox
                fw, fh = bx1 - bx0, by1 - by0
                px, mask = bytearray(fw * fh), bytearray(fw * fh)
                for y in range(fh):
                    for x in range(fw):
                        c = src[bx0 + x, by0 + y]
                        if c[3]:
                            px[y * fw + x], mask[y * fw + x] = lookup[c[:3]], 1
                        elif sh[bx0 + x, by0 + y]:
                            px[y * fw + x], mask[y * fw + x] = SHADOW, 1
                # (the header's first offset the ground point's y, then its x)
                f = {'w': fw, 'h': fh, 'ox': ay - by0, 'oy': ax - bx0, 'flags': 1, 'shadow': False}
                artmaker.from_grid(f, px, mask)
                art['frames'].append(f)
                for i, v in enumerate((bx0 - ax, by0 - ay, bx1 - ax, by1 - ay)):
                    box[i] = v if box[i] is None else (min(box[i], v) if i < 2 else max(box[i], v))
            entries[folder + '/' + d] = animaker.write_frames(art)
        ani = ['[animation]'] + ['dir%d = %s' % (i, p) for i, p in enumerate(folder.split('/'))]
        ani += ['animation = ' + d for d in directions if d in per]
        ani += ['x0 = %d' % (box[0] or 0), 'y0 = %d' % (box[1] or 0), 'x1 = %d' % (box[2] or 0), 'y1 = %d' % (box[3] or 0)]
        entries[folder + '/' + anim + '.ani'] = ('\r\n'.join(ani) + '\r\n').encode('latin1')
        done.append({'sub': sub, 'anim': anim, 'path': folder + '/' + anim})
    return entries, done, len(colours), merged


# ------------------------------------------------------------ its pictures
# The pictures the game shows of it, each one frame facing N with a palette
# of its own (as the game's are): the buy panel's icon ([m/Icon] Icon, 44 x
# 32), the list picture (cListImageName, the Zoo Status list's - about 36 x
# 28) and the plaque (cPlaqueImageName, its info panel's, 179 x 194)
PICS = {'icon': ('Buy-panel icon', 'ic', 44, 32), 'list': ('List picture', 'ls', 36, 28), 'plaque': ('Plaque', 'pl', 179, 194)}


def pic_paths(secs):
    out = {'icon': '', 'list': '', 'plaque': ''}
    for nm, lines in secs:
        low = nm.lower().replace(chr(92), '/')
        if low in ('m/icon', 'icon') and not out['icon']:
            out['icon'] = value_of(lines, 'icon').lower()
    out['list'] = sec_value(secs, STR_SECS, 'clistimagename').lower()
    out['plaque'] = sec_value(secs, STR_SECS, 'cplaqueimagename').lower()
    return out


def pictures(req):
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    f, text = entity_text(req, files)
    t = req['type'].lower()
    paths = pic_paths(sections(text))
    out = []
    for key, (label, _, w, h) in PICS.items():
        path = paths[key]
        has = bool(path) and anim_info(files, path) is not None
        # (its size: the current picture's, so a remake fits it - the list
        # pictures differ from animal to animal)
        if has:
            try:
                folder, dirs = animaker.parse_ani(files.read(path + '.ani'))
                fr = animaker.parse_frames(files.read(folder + '/' + dirs[0]))['frames'][0]
                w, h = fr['w'], fr['h']
            except Exception:
                pass
        out.append({'key': key, 'label': label, 'path': path, 'w': w, 'h': h, 'exists': has,
                    'own': bool(path) and path.startswith('animals/%s/' % t) and files.in_mod(path + '.ani'),
                    'drawn': os.path.exists(project_path(req, t, 'pics', key))})
    return {'ok': True, 'pictures': out}


def set_pic(secs, key, path):
    """The species file pointing at its own picture"""
    if key == 'icon':
        for nm, lines in secs:
            if nm.lower().replace(chr(92), '/') in ('m/icon', 'f/icon', 'icon'):
                for i, ln in enumerate(lines):
                    if '=' in ln and ln.split('=', 1)[0].strip().lower() == 'icon':
                        lines[i] = 'Icon = ' + path
                        break
        return
    set_str(secs, {'list': 'cListImageName', 'plaque': 'cPlaqueImageName'}[key], path)


def savepic(req):
    """A picture drawn: kept as drawn (.art/animals/<type>/pics/<key>.json),
    made the game's art in the mod - one frame, N, its own palette (up to 255
    colours) - and the species file pointed at it"""
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    t, key, proj = req['type'].lower(), req['key'], req['project']
    if key not in PICS:
        raise ValueError('No such picture.')
    pp = project_path(req, t, 'pics', key)
    os.makedirs(os.path.dirname(pp), exist_ok=True)
    with open(pp, 'w', encoding='utf-8') as fh:
        json.dump(proj, fh)
    frames = (proj.get('dirs') or {}).get('N') or []
    if not frames:
        raise ValueError('Nothing drawn yet.')
    img, _ = flatten(proj, frames[0])
    w, h = img.size
    src = img.load()
    pts = [(x, y) for y in range(h) for x in range(w) if src[x, y][3]]
    if not pts:
        raise ValueError('Nothing drawn yet.')
    colours = {}
    for x, y in pts:
        c = src[x, y][:3]
        colours[c] = colours.get(c, 0) + 1
    pal = [(0, 0, 0)]
    merged = 0
    if len(colours) <= 255:
        pal += sorted(colours)
        lookup = {c: i + 1 for i, c in enumerate(sorted(colours))}
    else:
        merged = len(colours) - 255
        strip = Image.new('RGB', (len(colours), 1))
        strip.putdata(list(colours))
        q = strip.quantize(255, method=Image.Quantize.MEDIANCUT)
        qp = q.getpalette()[:255 * 3]
        pal += [tuple(qp[i:i + 3]) for i in range(0, len(qp), 3)]
        idx = list(q.getdata())
        lookup = {c: idx[i] + 1 for i, c in enumerate(colours)}
    pal += [(0, 0, 0)] * (256 - len(pal))
    name = (PICS[key][1] + t)[:8]
    folder = 'animals/%s/%s' % (t, name)
    pal_path = '%s/%s.pal' % (folder, name)
    # (the whole picture, its anchor in the middle, as the game's own are:
    # the panels draw it from its corner)
    bx0, by0, bx1, by1 = 0, 0, w, h
    fw, fh = w, h
    px, mask = bytearray(fw * fh), bytearray(fw * fh)
    for y in range(fh):
        for x in range(fw):
            c = src[bx0 + x, by0 + y]
            if c[3]:
                px[y * fw + x], mask[y * fw + x] = lookup[c[:3]], 1
    ax, ay = proj['ax'], proj['ay']
    fr = {'w': fw, 'h': fh, 'ox': ay - by0, 'oy': ax - bx0, 'flags': 1, 'shadow': False}
    artmaker.from_grid(fr, px, mask)
    art = {'ms': 100, 'pal': pal_path, 'frames': [fr], 'head': b'', 'extra': 0}
    ani = ['[animation]', 'dir0 = animals', 'dir1 = ' + t, 'dir2 = ' + name, 'animation = N',
           'x0 = %d' % (bx0 - ax), 'y0 = %d' % (by0 - ay), 'x1 = %d' % (bx1 - ax), 'y1 = %d' % (by1 - ay)]
    f, text = entity_text(req, files)
    secs = sections(text)
    set_pic(secs, key, folder + '/' + name)
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], {
        pal_path: animaker.write_pal([0] + [animaker.entry(*c) for c in pal[1:]]),
        folder + '/N': animaker.write_frames(art),
        folder + '/' + name + '.ani': ('\r\n'.join(ani) + '\r\n').encode('latin1'),
        f: join(secs).encode('latin1'),
    })
    return {'ok': True, 'path': folder + '/' + name, 'colours': len(colours), 'merged': merged}


def save(req):
    t, sub, anim = req['type'].lower(), req['sub'], req['anim']
    if not re.fullmatch(r'[a-z0-9_]{1,24}', anim):
        raise ValueError('An animation\'s name is lower-case letters, numbers and _ (walk, lie_down).')
    if sub not in SUBS:
        raise ValueError('Which one: m, f or y.')
    proj = req['project']
    p = project_path(req, t, sub, anim)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(proj, f)
    os.replace(p + '.tmp', p)
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    entries, done, colours, merged = compile_all(req, t, files)
    text = species(req, files)
    # (the line it's drawn for points at what's drawn: its whole path, which
    # the game reads wherever the AnimPath points)
    value = 'animals/%s/%s/%s/%s.ani' % (t, sub, anim, anim)
    if req.get('slot'):
        text = set_slot(text, sub, req['slot'], value)
    entries['animals/%s.uca' % t] = text.encode('latin1')
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], entries)
    return {'ok': True, 'colours': colours, 'merged': merged, 'animations': done,
            'wrote': sorted(entries)}


def main():
    req = json.loads(sys.stdin.read() or '{}')
    ops = {'new': new, 'animals': animals, 'slots': slots, 'link': link, 'load': load, 'save': save, 'stencils': stencils, 'needs': needs, 'setneeds': setneeds, 'copyneeds': copyneeds, 'pictures': pictures, 'savepic': savepic}
    try:
        if req.get('op') not in ops:
            raise ValueError('No op %s.' % req.get('op'))
        if req.get('itemOp'):
            import itemmaker
            res = itemmaker.run(req)
        else:
            res = ops[req['op']](req)
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, (ValueError, FileNotFoundError)) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
