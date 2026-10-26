r"""The ZT1 Lab's species maker: a new animal from one of the game's, as a
mod (Write Code > Game files: an animal's .ai > New species from this).

Made from the player's own game (the engine folder's archives, as the
animation maker reads them), as old fan animals were made:
  - its species file, animals/<type>.uca: the original's .ai whole - its
    needs, biome, behaviour, sounds - with its own type, its own name
    ([1033] cName, as the game's user-made animals have) and its own
    number (cNameID), so other animals don't take it for the original
  - its art: every file in the original's folder (animals/<original>/:
    each animation, frame, icon, list and plaque picture, palette and
    sound) under animals/<type>/, the animations pointed at their new
    folder, the frames at their new palettes, and the palettes tinted
  - mod.json, and README.md: what to change next
in <mods folder>/<folder>/content.ztd.

One JSON request on stdin, one JSON answer on stdout:
  {"engineDir", "modsFolder", "from": "animals/zebra.ai", "name": "Quagga",
   "type": "quagga" (optional), "tint": "#b08850", "strength": 0.6,
   "mod": {"folder", "author"}}
      -> {"ok": true, "mod": <folder>, "type", "species": <its .uca>, "files": n}
"""
import hashlib
import json
import os
import re
import sys
import zipfile

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402


def files_under(game, prefix):
    """Every file under a folder, as the engine would read each (the first
    archive that has it wins)"""
    seen = {}
    for a in game.order:
        z = game.zips.get(a)
        if z is None:
            try:
                z = zipfile.ZipFile(a)
            except Exception:
                z = False
            game.zips[a] = z
        if not z:
            continue
        for n in z.namelist():
            k = n.lower().replace(chr(92), '/')
            if k.startswith(prefix) and not k.endswith('/') and k not in seen:
                seen[k] = (z, n)
    return seen


def tinted(cols, tint, strength):
    """A palette dyed a colour: each entry multiplied by it (the colour at
    its brightest is white's new colour), so white turns the colour, greys
    darker shades of it, and black - stripes, outlines, shade - stays black
    (entry 0, the clear one, as it was). (Keeping each entry's lightness
    instead left white white: no hue shows in white.)"""
    k = [int(tint[i:i + 2], 16) for i in (1, 3, 5)]
    k = [v / max(1, max(k)) for v in k]
    out = list(cols)
    for i, c in enumerate(cols):
        if i == 0 or not c:
            continue
        rgb = animaker.rgb(c)
        dyed = [round(v * (1 - strength + strength * kk)) for v, kk in zip(rgb, k)]
        out[i] = (c & 0xFF000000) | (animaker.entry(*dyed) & 0xFFFFFF)
    return out


def species_file(text, old, new, name, number):
    """The original's .ai as the new species' .uca"""
    # (every path into the original's folder, into the new one)
    text = re.sub(r'animals/' + re.escape(old) + r'/', 'animals/' + new + '/', text, flags=re.I)
    lines = text.replace('\r\n', '\n').split('\n')
    out, section = [], ''
    for line in lines:
        m = re.match(r'\s*\[([^\]]+)\]', line)
        if m:
            section = m.group(1).strip().lower()
        key = line.split('=', 1)[0].strip().lower() if '=' in line else ''
        if section == 'global' and key == 'type':
            line = 'Type = ' + new
        elif key == 'cnameid':
            line = 'cNameID = %d' % number
        out.append(line)
    text = '\n'.join(out).rstrip('\n')
    text += ('\n\n[defaultLCID]\nLCID = 1033\n\n'
             '; Its own name (the game\'s user-made animals have these: a section\n'
             '; for each language, by its number - 1033 is English)\n'
             '[1033]\ncName = {n}\ncLongHelp = Adopt a {n}\ncTheString = the {n}\n').format(n=name)
    head = ('; {n}: a new species, made in the ZT1 Lab from the game\'s {o}.\n'
            '; Change anything below: its needs, biome (cHabitat), continent\n'
            '; (cLocation), cost, herd size, what it likes (cCompatible...,\n'
            '; cSuitableObjects), its behaviour sets and sounds.\n\n').format(n=name, o=old)
    return (head + text + '\n').replace('\n', '\r\n').encode('latin1')


def make(req):
    game = animaker.Game(req['engineDir'])
    src = req['from'].lower()
    ai = game.read(src).decode('latin1')
    old = None
    for line in ai.splitlines():
        if line.split('=', 1)[0].strip().lower() == 'type' and '=' in line:
            old = line.split('=', 1)[1].strip().lower()
            break
    old = old or os.path.splitext(os.path.basename(src))[0]
    name = req['name'].strip()
    new = (req.get('type') or re.sub(r'[^a-z0-9]', '', name.lower()))[:8]
    if not new:
        raise ValueError('Give it a name with a letter or number in it.')
    for ext in ('.ai', '.uca'):
        try:
            game.read('animals/' + new + ext)
            raise ValueError('The game already has an animal called "%s": give it another name.' % new)
        except FileNotFoundError:
            pass
    tint, strength = req.get('tint') or '', float(req.get('strength') or 0)
    if tint and not re.fullmatch(r'#[0-9a-fA-F]{6}', tint):
        raise ValueError('The colour is #rrggbb, e.g. #b08850.')
    # (its own number: other animals' likes and dislikes go by it)
    number = 19000 + int(hashlib.sha1(new.encode()).hexdigest(), 16) % 900

    old_dir, new_dir = 'animals/' + old + '/', 'animals/' + new + '/'
    files = {}
    for k, (z, n) in sorted(files_under(game, old_dir).items()):
        data = z.read(n)
        rel = new_dir + n.replace(chr(92), '/')[len(old_dir):]
        ext = os.path.splitext(k)[1]
        if ext == '.ani':
            data = re.sub(rb'(?im)^(\s*dir\d\s*=\s*)' + re.escape(old.encode()) + rb'\s*$',
                          lambda m: m.group(1) + new.encode(), data)
        elif ext == '.pal' and tint and strength > 0:
            data = animaker.write_pal(tinted(animaker.read_pal(data), tint, strength))
        elif ext == '':
            try:
                art = animaker.parse_frames(data)
            except Exception:
                art = None
            if art and art['pal'].lower().startswith(old_dir):
                art['pal'] = new_dir + art['pal'][len(old_dir):]
                data = animaker.write_frames(art)
        files[rel] = data
    if not files:
        raise ValueError('No art in %s.' % old_dir)
    species = 'animals/' + new + '.uca'
    files[species] = species_file(ai, old, new, name, number)

    m = req['mod']
    out_dir = os.path.join(req['modsFolder'], m['folder'])
    os.makedirs(out_dir, exist_ok=True)
    # Into a mod that has a content.ztd already (a pack of animals; or this
    # one made again): what else it holds kept, and its species file too if
    # it has this one's - the changes made to it - only the art made again
    ztd = os.path.join(out_dir, 'content.ztd')
    kept, others = False, {}
    if os.path.exists(ztd):
        with zipfile.ZipFile(ztd) as old_z:
            for n in old_z.namelist():
                k = n.lower().replace(chr(92), '/')
                if k == species:
                    files[species] = old_z.read(n)
                    kept = True
                elif not k.startswith(new_dir):
                    others[n] = old_z.read(n)
    tmp = ztd + '.new'
    try:
        with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_DEFLATED) as z:
            for path, data in list(others.items()) + list(files.items()):
                z.writestr(path, data)
        os.replace(tmp, ztd)
    finally:
        if os.path.exists(tmp):
            os.remove(tmp)
    if os.path.exists(os.path.join(out_dir, 'mod.json')):
        return {'ok': True, 'mod': out_dir, 'type': new, 'species': species, 'files': len(files), 'number': number,
                'kept': kept}
    pal = next((p for p in files if p.endswith('/' + old + '.pal')), None)
    info = {'id': m.get('id') or re.sub(r'[^a-z0-9._-]+', '-', name.lower()).strip('-'), 'name': name,
            'author': m.get('author', ''), 'version': '1.0',
            'about': 'A new animal, the %s: made from the game\'s %s (its art tinted %s), with its own '
                     'name and needs.' % (name, old, tint or 'as it was'),
            'replaces': []}
    # (the Mod Menu's picture: the original standing, in the new palette)
    stand = next((p for p in sorted(files) if re.search(r'/m/stand/se$', p, re.I)), None)
    if pal and stand:
        info.update({'preview': (old_dir + stand[len(new_dir):]).lower(), 'palette': pal.lower(),
                     'originalPalette': (old_dir + pal[len(new_dir):]).lower()})
    with open(os.path.join(out_dir, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)
        f.write('\n')
    with open(os.path.join(out_dir, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write('# {n}\n\nA new animal made in the ZT1 Lab from the game\'s {o}.\n\n'
                '- `content.ztd` holds `{s}` (the species: needs, biome, behaviour, its name in\n'
                '  `[1033]`) and its art under `animals/{t}/`, made from your own game.\n'
                '- Change the species in the Lab: Mods > {n} > Contents > `{s}` opens it to edit;\n'
                '  saving puts it back in content.ztd. Then Try in the Test Zoo.\n'
                '- Its art came from the game you own: share the mod with people who have the game.\n'
                .format(n=name, o=old, s=species, t=new))
    return {'ok': True, 'mod': out_dir, 'type': new, 'species': species, 'files': len(files), 'number': number}


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = make(req)
    except Exception as e:
        res = {'ok': False, 'error': str(e) if isinstance(e, ValueError) else '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
