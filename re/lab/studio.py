r"""The ZT1 Lab's mod studio (Mods): what a mod holds, by kind, for its tree.

Every .ztd in the mod is read: its entity files (.ai, .uca, .ucs, .ucb) are
its items - animals, buildings, scenery, foliage, fences, paths, staff - each
marked new or a change to one of the game's own (the game has a file at that
path); its freeform/*.scn are its maps; any other file of the game's it
replaces is listed too (a palette, an animation changed in place).

One JSON request on stdin, one JSON answer on stdout:
  {"op": "tree", "engineDir", "dir": <the mod's folder>}
      -> {"ok", "items": [{"file", "ztd", "kind", "name", "nameId", "changes"}],
          "maps": [{"file", "ztd", "title", "blank", "about"}],
          "changed": [{"file", "ztd"}]}
"""
import configparser
import json
import os
import re
import sys
import tempfile
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402
import artmaker  # noqa: E402

ENTITY = re.compile(r'\.(ai|uca|ucs|ucb)$', re.I)


def kind_of(path):
    """What an entity file is, by where the game keeps its kind"""
    parts = path.lower().split('/')
    top = parts[0]
    if top == 'scenery' and len(parts) > 2:
        return {'building': 'building', 'foliage': 'foliage'}.get(parts[1], 'scenery')
    return {'animals': 'animal', 'staff': 'staff', 'fences': 'fence', 'paths': 'path', 'guests': 'guest',
            'scenery': 'scenery', 'items': 'item'}.get(top, 'other')


def read_ini(data):
    """An .ai's or .scn's sections, keys lower case (its [Member] lines and
    other bare lines left out)"""
    p = configparser.ConfigParser(strict=False, interpolation=None, allow_no_value=True,
                                  comment_prefixes=(';', '#'), inline_comment_prefixes=(';',))
    p.optionxform = str.lower
    try:
        p.read_string(data.decode('latin1'))
    except configparser.Error:
        pass
    return p


def own_name(ini):
    """A user-made entity's own name: [1033] cName, else its [defaultLCID]'s"""
    for sec in ['1033'] + [ini.get('defaultlcid', 'lcid', fallback='')]:
        if sec and ini.has_section(sec):
            n = ini.get(sec, 'cname', fallback='')
            if n:
                return n
    return ''


def game_names(engine_dir):
    """The game's file names (lower case), for "does the game have this?":
    kept on disk as plain lines while the archives are the same - the art
    maker's whole index is ten times the size and took a second to read"""
    if not engine_dir:
        return set()
    order = animaker.archives(engine_dir)
    sig = []
    for a in order:
        try:
            st = os.stat(a)
            sig.append('%s|%d|%d' % (a, st.st_size, int(st.st_mtime)))
        except OSError:
            sig.append(a + '|0|0')
    sig = '\t'.join(sig)
    path = os.path.join(tempfile.gettempdir(), 'zt1-lab-names.txt')
    try:
        with open(path, encoding='utf-8') as f:
            head = f.readline().rstrip('\n')
            if head == sig:
                return set(f.read().split('\n'))
    except OSError:
        pass
    names = set(artmaker.Files('', engine_dir).index())
    try:
        tmp = path + '.tmp'
        with open(tmp, 'w', encoding='utf-8') as f:
            f.write(sig + '\n' + '\n'.join(sorted(names)))
        os.replace(tmp, path)
    except OSError:
        pass
    return names


def tree(req):
    d = req['dir']
    if not os.path.isdir(d):
        raise ValueError('No such mod.')
    game = game_names(req.get('engineDir') or '')
    items, maps, changed = [], [], []
    for name in sorted(os.listdir(d)):
        if not name.lower().endswith('.ztd'):
            continue
        try:
            z = zipfile.ZipFile(os.path.join(d, name))
        except Exception:
            continue
        for n in z.namelist():
            if n.endswith('/'):
                continue
            p = n.replace('\\', '/')
            low = p.lower()
            if ENTITY.search(low):
                ini = read_ini(z.read(n))
                nid = ini.get('characteristics/integers', 'cnameid', fallback='')
                items.append({'file': p, 'ztd': name, 'kind': kind_of(low), 'name': own_name(ini),
                              'nameId': int(nid) if nid.strip().lstrip('-').isdigit() else 0, 'changes': low in game})
            elif low.startswith('freeform/') and low.endswith('.scn') and low.count('/') == 1:
                ini = read_ini(z.read(n))
                about = ''
                txt = p[:-4] + '.txt'
                try:
                    about = z.read(txt).decode('latin1').strip()
                except KeyError:
                    pass
                maps.append({'file': p, 'ztd': name, 'title': ini.get('freeform', 'title', fallback='') or os.path.basename(p)[:-4],
                             'blank': ini.get('start', 'blank', fallback=''), 'about': about,
                             'size': ini.get('freeform', 'size', fallback='')})
            elif low in game:
                changed.append({'file': p, 'ztd': name})
    return {'ok': True, 'items': items, 'maps': maps, 'changed': changed}


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = tree(req) if req.get('op') == 'tree' else {'ok': False, 'error': 'No such op.'}
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, (ValueError, FileNotFoundError)) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
