"""Build a theme pack snapshot from local mods, through Publish.

One normal mod.json and content.ztd let the existing repository and installer
handle the pack as one upload. Conflicting game paths require the creator to
resolve them first; dot-folders and authoring projects never enter a pack.
"""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import tempfile
import zipfile


def make(req):
    root = Path(req['modsFolder']).resolve()
    folders = req.get('folders') or []
    if len(folders) < 2 or len(set(folders)) != len(folders):
        raise ValueError('Choose at least two different mods.')
    name = str(req.get('name') or '').strip()
    if not name or re.search(r'[<>:"/\\|?*\x00-\x1f]', name) or name.startswith('.') or name.endswith(('.', ' ')):
        raise ValueError('Give the pack a name using letters, numbers, spaces or dashes.')
    id_ = str(req.get('id') or re.sub(r'[^a-z0-9._-]+', '-', name.lower()).strip('-'))
    if not re.fullmatch(r'[a-z0-9][a-z0-9._-]{0,63}', id_):
        raise ValueError('Use a repository id of up to 64 lower-case letters, numbers, . _ or -.')
    dest = root / name
    if dest.exists():
        raise ValueError('A mod already has that folder name. Choose a new pack name.')
    game, loose, owners, components, replaces = {}, {}, {}, [], set()

    def add(table, path, data, owner):
        path = path.replace('\\', '/').lower()
        if not path or path.startswith('/') or any(p in ('', '.', '..') for p in path.split('/')) or ':' in path:
            raise ValueError(owner + ': a file points outside its mod.')
        # A loose file and a zipped resource resolve to the same game path.
        if path in owners and owners[path][1] != hashlib.sha256(data).hexdigest():
            raise ValueError('Conflict at ' + path + ': ' + owners[path][0] + ' and ' + owner + '. Choose compatible mods or resolve this file first.')
        owners.setdefault(path, (owner, hashlib.sha256(data).hexdigest()))
        table[path] = data

    for folder in folders:
        source = (root / str(folder)).resolve()
        if source.parent != root or not source.is_dir() or source.is_symlink():
            raise ValueError('Choose mods from your mods folder.')
        meta = json.loads((source / 'mod.json').read_text(encoding='utf-8-sig'))
        if meta.get('settings'):
            raise ValueError(folder + ' has settings tied to its own mod identity. Keep it separate until those settings are migrated.')
        components.append({'id': meta.get('id', folder), 'name': meta.get('name', folder),
                           'version': meta.get('version', ''), 'author': meta.get('author', '')})
        replaces.update(meta.get('replaces') or [])
        for base, dirs, files in os.walk(source, followlinks=False):
            if any((Path(base) / d).is_symlink() for d in dirs):
                raise ValueError(folder + ' contains a folder link.')
            dirs[:] = sorted(d for d in dirs if not d.startswith('.'))
            for file in sorted(files):
                if file.startswith('.'):
                    continue
                p = Path(base) / file
                rel = p.relative_to(source).as_posix()
                if p.is_symlink():
                    raise ValueError(folder + ' contains a file link.')
                if '/' not in rel and (file.lower() in ('mod.json', 'repository-preview.png') or file.lower() == str(meta.get('picture', '')).lower() or file.lower().startswith('readme')):
                    continue
                if p.suffix.lower() == '.ztd':
                    with zipfile.ZipFile(p) as z:
                        for info in z.infolist():
                            if not info.is_dir():
                                add(game, info.filename, z.read(info), folder)
                else:
                    add(loose, rel, p.read_bytes(), folder)
    if not game and not loose:
        raise ValueError('The selected mods contain no game files.')
    work = Path(tempfile.mkdtemp(prefix='.theme-', dir=root))
    try:
        if game:
            with zipfile.ZipFile(work / 'content.ztd', 'w', zipfile.ZIP_DEFLATED) as z:
                for path, data in sorted(game.items()):
                    z.writestr(path, data)
        for path, data in loose.items():
            p = work / path
            p.parent.mkdir(parents=True, exist_ok=True)
            p.write_bytes(data)
        meta = {'id': id_, 'name': name, 'folder': name, 'author': str(req.get('author') or ''),
                'version': str(req.get('version') or '1.0'), 'about': str(req.get('about') or ''),
                'replaces': sorted(replaces), 'themePack': {'format': 1, 'components': components}}
        (work / 'mod.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
        os.rename(work, dest)
    finally:
        if work.exists():
            shutil.rmtree(work)
    return {'ok': True, 'folder': name, 'components': components, 'resources': len(game), 'looseFiles': len(loose)}
