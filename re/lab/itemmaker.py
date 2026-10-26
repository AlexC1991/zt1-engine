"""Studio item definitions and drawings, using the existing editor compiler.

New items inherit a working role but have separate identities and art. Guest
appearances target one of the game's four guest kinds; they are not new duties.
"""
import hashlib
import json
import os
import re
import base64
import io

from PIL import Image
import animaker

import animalmaker as am
import artmaker

DIRECTIONS = ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW']
KINDS = {'building': ('scenery/building', 'building'),
         'scenery': ('scenery/other', 'scenery'),
         'filter': ('scenery/other', 'filter'),
         'foliage': ('scenery/foliage', 'foliage'),
         'rock': ('scenery/other', 'scenery'),
         'shelter': ('scenery/building', 'building'),
         'toy': ('scenery/building', 'building'),
         'entrance': ('scenery/other', 'building'),
         'food': ('scenery/other', 'food'),
         'path': ('paths', 'paths'),
         'tankwall': ('fences', 'tankwall'),
         'fence': ('fences', 'fences'), 'staff': ('staff', 'staff')}


def safe(value):
    if not re.fullmatch(r'[a-z0-9_]{1,24}', value or ''):
        raise ValueError('Use a short name of lower-case letters, numbers and _.')
    return value


def metadata(req):
    return os.path.join(req['projects'], 'items', 'items.json')


def records(req):
    p = metadata(req)
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            return json.load(f)
    return []


def keep(req, rows):
    p = metadata(req)
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(rows, f, indent=2)
    os.replace(p + '.tmp', p)


def item(req):
    key = safe(req['item'])
    row = next((x for x in records(req) if x['key'] == key), None)
    if not row:
        raise ValueError('Select an item in the studio first.')
    return row


def set_value(text, section, key, value):
    secs = am.sections(text)
    lines = next((ls for nm, ls in secs if nm.lower() == section.lower()), None)
    if lines is None:
        lines = ['', '[' + section + ']']
        secs.append((section, lines))
    for i, line in enumerate(lines):
        if '=' in line and line.split('=', 1)[0].strip().lower() == key.lower():
            lines[i] = key + ' = ' + str(value)
            break
    else:
        lines.append(key + ' = ' + str(value))
    return am.join(secs)


def rows_of(files, row, text):
    if row['kind'] == 'ground':
        return {'base': [{'slot': 'texture', 'section': '', 'path': row['source'],
                         'status': 'drawn' if files.in_mod(row['file']) else 'linked',
                         'dirs': ['N'], 'frames': 1, 'ms': 1000}]}
    secs = am.sections(text)
    old = am.type_of(text, os.path.splitext(os.path.basename(row['source']))[0])
    subs = {}
    for section, lines in secs:
        low = section.lower()
        if low != 'animations' and not low.endswith('/animations'):
            continue
        sub = low.split('/')[0] if '/' in low else 'base'
        if row['kind'] == 'guest' and sub != row['guestKind']:
            continue
        base = ('guests/' + sub if row['kind'] == 'guest' else
                'staff/' + old + '/' + sub if row['kind'] == 'staff' else
                'fences/' + os.path.splitext(os.path.basename(row['source']))[0] + '/' + ('f' if sub == 'base' else sub) if row['kind'] in ('fence', 'tankwall') else
                'paths/' + old if row['kind'] == 'path' else
                'food/' + old if row['kind'] == 'food' else
                'objects/' + old)
        found = []
        for line in lines:
            if '=' not in line or line.strip().startswith(';'):
                continue
            slot, value = [x.strip() for x in line.split('=', 1)]
            path = am.anim_path(value, base)
            info = {'dirs': ['N'], 'frames': 20, 'ms': 1000} if row['kind'] == 'path' else am.anim_info(files, path) if path else None
            # Some building definitions omit Type; their source file is the art key.
            found.append({'slot': slot, 'section': section, 'path': path,
                          'status': 'missing' if not info else 'drawn' if path.startswith(row['assetRoot'] + '/') and files.in_mod(path + '.ani') else 'linked',
                          **(info or {})})
        subs[sub] = found
    return subs


def new(req):
    name = req['name'].strip()
    if not name or any(c in name for c in '\r\n[]'):
        raise ValueError('Give it a name on one line.')
    key = safe((re.sub('[^a-z0-9]', '', name.lower())[:16]))
    rows = records(req)
    if any(x['key'] == key for x in rows):
        raise ValueError('That item name is already used in this mod.')
    kind, source = req['kind'], req['source'].lower()
    mode = req.get('mode', 'new')
    if kind == 'ground':
        if not re.fullmatch(r'terrain/[a-z0-9_]+\.tga', source):
            raise ValueError('Choose a ground texture.')
        files = artmaker.Files(req['ztd'], req.get('engineDir'))
        files.read(source)  # Must exist in the player's game.
        row = {'key': key, 'name': name, 'kind': kind, 'file': source, 'source': source,
               'mode': 'change', 'workflow': mode, 'assetRoot': 'terrain/' + key, 'guestKind': 'man'}
        rows.append(row)
        keep(req, rows)
        return {'ok': True, **row}
    if kind not in KINDS and kind != 'guest':
        raise ValueError('Choose an item kind.')
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    text = files.read(source).decode('latin1')
    guest_kind = req.get('guestKind', 'man')
    if guest_kind not in ('man', 'woman', 'boy', 'girl'):
        raise ValueError('Choose an existing guest appearance.')
    entry = source if mode == 'change' or kind == 'guest' else KINDS[kind][0] + '/' + key + '.ai'
    if entry != source and entry in (set(files.index()) | set(files.names)):
        raise ValueError('An item already exists at ' + entry)
    root = ('staff/' if kind == 'staff' else 'fences/' if kind in ('fence', 'tankwall') else 'paths/' if kind == 'path' else 'food/' if kind == 'food' else 'guests/' if kind == 'guest' else 'objects/') + key
    row = {'key': key, 'name': name, 'kind': kind, 'file': entry, 'source': source,
           'mode': mode, 'guestKind': guest_kind, 'assetRoot': root}
    # Fix every inherited animation to its source before assigning a new identity.
    inherited = rows_of(files, row, text)
    for subrows in inherited.values():
        for r in subrows:
            text = set_value(text, r['section'], r['slot'], r['path'] + '.ani')
    entries = {}
    if kind != 'guest':
        number = 20000 + int(hashlib.sha1(key.encode()).hexdigest(), 16) % 8000
        for section, lines in am.sections(text):
            if section.lower().endswith('characteristics/integers'):
                text = set_value(text, section, 'cNameID', number)
        text = set_value(text, 'defaultLCID', 'LCID', 1033)
        text = set_value(text, '1033', 'cName', name)
        text = set_value(text, '1033', 'cLongHelp', name)
        if mode == 'new' and kind not in ('staff', 'fence', 'tankwall'):
            text = set_value(text, 'Global', 'Type', key)
            text = am.join([(nm, ls) for nm, ls in am.sections(text)
                            if nm.lower() not in ('colorrep', 'cr_color')])
        if mode == 'new':
            # Registry section selects function; staff Type preserves its duty.
            section = KINDS[kind][1]
            reg = '[' + section + ']\r\n' + key + ' = ' + entry + '\r\n'
            if kind in ('fence', 'tankwall', 'staff'):
                reg += '\r\n[' + key + '/subtypes]\r\n' + ('f\r\ng\r\n' if kind != 'staff' else 'm\r\nf\r\n')
            entries['lab_' + key + '.cfg'] = reg.encode('latin1')
    entries[entry] = text.encode('latin1')
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], entries)
    rows.append(row)
    keep(req, rows)
    if entry == source:
        meta_path = os.path.join(os.path.dirname(req['ztd']), 'mod.json')
        with open(meta_path, encoding='utf-8') as f:
            meta = json.load(f)
        meta['replaces'] = sorted(set(meta.get('replaces', []) + [entry]))
        with open(meta_path, 'w', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
    return {'ok': True, **row}


def slots(req):
    row = item(req)
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    text = '' if row['kind'] == 'ground' else files.read(row['file']).decode('latin1')
    return {'ok': True, 'type': row['key'], 'name': row['name'], 'subs': rows_of(files, row, text),
            'femaleOwn': True, 'file': row['file'], 'item': row}


def load(req):
    row = item(req)
    p = os.path.join(req['projects'], 'items', row['key'], safe(req['sub']), safe(req['anim']) + '.json')
    project = None
    if os.path.exists(p):
        with open(p, encoding='utf-8') as f:
            project = json.load(f)
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    ref = req.get('reference')
    definition = None
    if ref and row['kind'] in ('building', 'scenery') and files.game:
        definition = files.game.read(row['source']).decode('latin1')
    reference = None
    if row['kind'] == 'ground':
        image = Image.open(io.BytesIO(files.read(row['source']))).convert('RGBA')
        buf = io.BytesIO()
        image.save(buf, 'PNG')
        b64 = base64.b64encode(buf.getvalue()).decode()
        if project is None:
            project = {'version': 1, 'w': image.width, 'h': image.height, 'ax': image.width // 2,
                       'ay': image.height // 2, 'ms': 1000, 'layers': [{'name': 'Texture', 'visible': True}],
                       'dirs': {'N': [{'cells': [b64]}]} if row.get('workflow') == 'change' else {}}
        reference = {'w': image.width, 'h': image.height, 'ax': image.width // 2, 'ay': image.height // 2,
                     'ms': 1000, 'dirs': {'N': [b64]}, 'from': row['source']}
    elif row['kind'] == 'path' and ref:
        # Paths have twenty numbered shape files, rather than compass views.
        path = ref.removesuffix('.ani')
        folder = path.rsplit('/', 1)[0]
        class PathFrames:
            def read(self, name):
                if name == path + '.ani':
                    return ('[animation]\ndir0=' + folder + '\n' + '\n'.join('animation=' + str(n) for n in range(1, 21))).encode()
                return files.read(name)
        reference = am.rendered(PathFrames(), path)
        if reference:
            reference['dirs'] = {'N': [f for frames in reference['dirs'].values() for f in frames]}
    elif ref:
        reference = am.rendered(files, ref.removesuffix('.ani'), definition)
    return {'ok': True, 'project': project, 'reference': reference}


def save(req):
    row = item(req)
    sub, anim = safe(req['sub']), safe(req['anim'])
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    text = '' if row['kind'] == 'ground' else files.read(row['file']).decode('latin1')
    current = rows_of(files, row, text)
    target = next((r for r in current.get(sub, []) if r['slot'] == req.get('slot')), None)
    if not target:
        raise ValueError('Select an existing animation slot.')
    p = os.path.join(req['projects'], 'items', row['key'], sub, anim + '.json')
    os.makedirs(os.path.dirname(p), exist_ok=True)
    with open(p + '.tmp', 'w', encoding='utf-8') as f:
        json.dump(req['project'], f)
    os.replace(p + '.tmp', p)
    if row['kind'] == 'ground':
        frames = req['project'].get('dirs', {}).get('N', [])
        if len(frames) != 1:
            raise ValueError('A ground texture needs one frame facing N.')
        image, _ = am.flatten(req['project'], frames[0])
        if image.width != image.height or image.width not in (32, 64, 128, 256, 512):
            raise ValueError('Use a square ground texture of 32, 64, 128, 256 or 512 pixels.')
        if image.getextrema()[3] != (255, 255):
            raise ValueError('Cover the whole ground texture; ground cannot be transparent.')
        buf = io.BytesIO()
        image.convert('RGB').save(buf, 'TGA')
        files.z and files.z.close()
        artmaker.write_entries(req['ztd'], {row['file']: buf.getvalue()})
        meta_path = os.path.join(os.path.dirname(req['ztd']), 'mod.json')
        with open(meta_path, encoding='utf-8') as f:
            meta = json.load(f)
        meta['replaces'] = sorted(set(meta.get('replaces', []) + [row['file']]))
        with open(meta_path, 'w', encoding='utf-8') as f:
            json.dump(meta, f, indent=2)
        return {'ok': True, 'colours': len(image.getcolors(1 << 20) or []), 'merged': 0, 'animations': ['texture'], 'wrote': [row['file']]}
    entries, done, colours, merged = am.compile_all({**req, 'projectRoot': 'items/' + row['key'],
        'assetRoot': row['assetRoot'], 'subtypes': list(current), 'directions': DIRECTIONS}, row['key'], files)
    path = row['assetRoot'] + '/' + sub + '/' + anim + '/' + anim + '.ani'
    if row['kind'] == 'path':
        raw = entries.get(row['assetRoot'] + '/' + sub + '/' + anim + '/N')
        if not raw:
            raise ValueError('Put the twenty connected path shapes in direction N.')
        art = animaker.parse_frames(raw)
        if len(art['frames']) != 20:
            raise ValueError('A path needs twenty frames: four ramps, then sixteen connected flat tiles.')
        folder = path.rsplit('/', 1)[0]
        for n, frame in enumerate(art['frames'], 1):
            entries[folder + '/' + str(n)] = animaker.write_frames({**art, 'frames': [frame]})
    text = set_value(text, target['section'], target['slot'], path)
    if row['kind'] in ('building', 'scenery', 'shelter', 'toy', 'entrance', 'foliage', 'rock'):
        # This drawing has its own palette, rather than the source building's
        # indexed repaint slots. Keep the game from selecting that old idle art.
        text = am.join([(nm, ls) for nm, ls in am.sections(text)
                        if nm.lower() not in ('colorrep', 'cr_color')])
        text = set_value(text, 'Characteristics/Integers', 'cIsColorReplaced', 0)
    entries[row['file']] = text.encode('latin1')
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], entries)
    return {'ok': True, 'colours': colours, 'merged': merged, 'animations': done, 'wrote': sorted(entries)}


def link(req):
    row = item(req)
    files = artmaker.Files(req['ztd'], req.get('engineDir'))
    text = files.read(row['file']).decode('latin1')
    target = req['target'].lower().removesuffix('.ani')
    if not am.anim_info(files, target):
        raise ValueError('No animation at ' + target)
    slot = next((r for r in rows_of(files, row, text).get(req['sub'], []) if r['slot'] == req['slot']), None)
    if not slot:
        raise ValueError('Select an existing slot.')
    text = set_value(text, slot['section'], slot['slot'], target + '.ani')
    files.z and files.z.close()
    artmaker.write_entries(req['ztd'], {row['file']: text.encode('latin1')})
    return {'ok': True}


def run(req):
    if req.get('themePack'):
        import themepack
        return themepack.make(req)
    return {'new': new, 'slots': slots, 'load': load, 'save': save, 'link': link,
            'animals': lambda q: {'ok': True, 'items': records(q)}}[req['op']](req)
