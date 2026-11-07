"""Sort an HD art pack into one mod per game: Zoo Tycoon, Marine Mania and
Dinosaur Digs (ours: packaging; players download only the games they own).

Each art folder (animals/lion/, objects/restrant/ ...) goes with its item's
[Member] tag, as the engine's content filter reads it (ItemCatalog: dinosaur
-> Dinosaur Digs, aqua -> Marine Mania). Art with no tagged item goes with the
game whose archives first had it, earliest release first. Light maps go with
the art they light.

  python re/lab/hd_split.py --game <game folder> --pack <HD pack> --plan plan.json
  python re/lab/hd_split.py ... --plan plan.json --move <out folder>

--move moves (renames, same drive) each file into <out>/<pack folder>/hd/...
"""
import argparse, json, os, re, sys, zipfile
from collections import defaultdict
from pathlib import Path

PACKS = {'zoo': 'Zoo Tycoon', 'marine': 'Marine Mania', 'dino': 'Dinosaur Digs'}
# (each game's own folders, earliest release first; the downloads last)
SOURCES = [('zoo', ['.', 'loc', 'zupdate', 'zupdate1']), ('dino', ['xpack1', 'xpack1/loc', 'dupdate']),
           ('marine', ['xpack2', 'xpack2/loc']), (None, ['updates', 'dlupdate'])]


def archives(game, folder):
    d = Path(game) / folder
    if not d.is_dir():
        # (the folders' case varies: XPACK1, ZUPDATE)
        for e in Path(game).iterdir() if Path(game).is_dir() else []:
            if e.is_dir() and e.name.lower() == folder.split('/')[0].lower():
                d = e / '/'.join(folder.split('/')[1:]) if '/' in folder else e
    return sorted(d.glob('*.ztd')) + sorted(d.glob('*.ZTD')) if d.is_dir() else []


def survey(game):
    first = {}   # art folder (two levels) -> game of the earliest archive with it
    tags = {}    # item name -> game by its [Member] tag
    for pack, folders in SOURCES:
        for folder in folders:
            for z in archives(game, folder):
                try:
                    f = zipfile.ZipFile(z)
                except zipfile.BadZipFile:
                    continue
                with f:
                    for n in f.namelist():
                        low = n.lower().replace('\\', '/')
                        parts = low.split('/')
                        if len(parts) >= 3 and pack:
                            first.setdefault('/'.join(parts[:2]) + '/', pack)
                        if low.endswith('.ai'):
                            t = f.read(n).decode('latin1').lower()
                            m = re.search(r'\[member\]([^\[]*)', t)
                            members = set(m.group(1).split()) if m else set()
                            tag = 'dino' if 'dinosaur' in members else 'marine' if 'aqua' in members else None
                            name = Path(low).stem
                            if tag:
                                tags[name] = tag
                            elif pack and name not in tags:
                                tags.setdefault('_untagged:' + name, pack)
    return first, tags


def pack_of(folder, first, tags):
    name = folder.strip('/').split('/')[-1]
    if name in tags:
        return tags[name]
    if folder in first:
        return first[folder]
    return tags.get('_untagged:' + name, 'zoo')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--game', required=True)
    ap.add_argument('--pack', required=True)
    ap.add_argument('--plan', required=True)
    ap.add_argument('--move')
    a = ap.parse_args()
    hd = Path(a.pack) / 'hd'
    first, tags = survey(a.game)
    totals = defaultdict(lambda: {'files': 0, 'bytes': 0, 'maps': 0})
    folders = {}
    def walk(d):
        for e in os.scandir(d):
            if e.is_dir(follow_symlinks=False):
                yield from walk(e.path)
            else:
                yield e
    for e in walk(hd):
        rel = os.path.relpath(e.path, hd).replace(os.sep, '/')
        parts = rel.split('/')
        art = parts[4:] if parts[0] == '_lighting' else parts
        if len(art) < 3:
            continue
        folder = '/'.join(art[:2]) + '/'
        p = folders.get(folder) or folders.setdefault(folder, pack_of(folder, first, tags))
        t = totals[p]
        t['files'] += 1
        t['bytes'] += e.stat().st_size
        if parts[0] == '_lighting':
            t['maps'] += 1
        if a.move:
            dest = Path(a.move) / p / 'hd' / rel
            dest.parent.mkdir(parents=True, exist_ok=True)
            os.replace(e.path, dest)
    plan = {'packs': {p: dict(totals[p], name=PACKS[p]) for p in PACKS}, 'folders': dict(sorted(folders.items()))}
    Path(a.plan).write_text(json.dumps(plan, indent=1))
    for p in PACKS:
        t = totals[p]
        print(f"{PACKS[p]}: {t['files']} files ({t['maps']} light maps), {t['bytes'] / 2**30:.1f} GiB")


if __name__ == '__main__':
    sys.exit(main())
