"""Package an HD art pack as one mod per game (ours: packaging).

Reads the plan hd_split.py wrote (each art folder's game) and builds, for
each game, a mod folder holding one archive per art folder,
hd/<top>/<name>.ztd - a zip, stored (PNGs don't compress), with
<top>/<name>/... and that art's current light maps - which the engine reads
as it reads loose HD files (HdArt). A million loose frames were too many
files to share, check or unpack.

Light maps made before --since (an older build's, their fingerprints no
longer asked for) are left out. Each mod's archives are grouped into parts
of at most --part-gib for the mod repository (mod.json "parts"); the parts
all unpack into the same mod folder. The source pack is only read.

  python re/lab/hd_pack.py --pack <HD pack> --plan plan.json --out <mods folder> --since <epoch>
"""
import argparse, json, os, sys, time, zipfile
from collections import defaultdict
from pathlib import Path

GAMES = {
    'zoo': ('ZT1-HD-Zoo-Tycoon', 'zt1-hd-zoo-tycoon', 'Zoo Tycoon', ''),
    'marine': ('ZT1-HD-Marine-Mania', 'zt1-hd-marine-mania', 'Marine Mania', 'Needs Marine Mania. '),
    'dino': ('ZT1-HD-Dinosaur-Digs', 'zt1-hd-dinosaur-digs', 'Dinosaur Digs', 'Needs Dinosaur Digs. '),
}
# (the parts: by kind of art; the rest - awards, items, research, scenario
# pictures, terrain and staff icons - are its menus' icons and pictures)
GROUPS = {'animals': 'animals', 'guests': 'guests', 'objects': 'objects',
          'fences': 'walls', 'water': 'walls', 'paths': 'walls'}
LABELS = {'animals': 'Animals', 'guests': 'Guests', 'objects': 'Buildings and scenery',
          'walls': 'Fences, tank walls, water and paths', 'icons': 'Icons and pictures'}
WHAT = {'animals': 'animals', 'guests': 'guests', 'objects': 'buildings and scenery',
        'walls': 'fences, tank walls, water and paths', 'icons': 'plus the icons and pictures in its menus'}
ABOUT = ("The {game} map art - {what} - with four times the detail when you zoom in, and its own "
         "light maps for windows, signs and lamps at night. {needs}Zoomed out, and with the mod off, "
         "you see the original art.\n\n"
         "Why it says AI: every frame is the game's own artwork, enlarged by Real-ESRGAN, an "
         "open-source upscaler that ran locally on a PC's graphics card. It wasn't made by an image "
         "generator: nothing was drawn new or prompted, and the original pixels are copied back over "
         "each enlarged frame so their hand-drawn detail stays.")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--pack', required=True)
    ap.add_argument('--plan', required=True)
    ap.add_argument('--out', required=True)
    ap.add_argument('--since', type=float, required=True)
    ap.add_argument('--part-gib', type=float, default=3.5)  # (under 4 GB: no zip64 needed to unpack)
    ap.add_argument('--only', choices=list(GAMES))
    ap.add_argument('--resume', action='store_true')  # (keep archives a stopped run finished)
    a = ap.parse_args()
    hd = Path(a.pack) / 'hd'
    plan = json.loads(Path(a.plan).read_text())['folders']
    # (light maps by art folder: _lighting/<fingerprint>/<profile>/<view>/<top>/<name>/...)
    maps = defaultdict(list)
    stale = 0
    lighting = hd / '_lighting'
    for dp, dn, fn in os.walk(lighting):
        rel = Path(dp).relative_to(hd).as_posix().split('/')
        if len(rel) < 6:
            continue
        folder = '/'.join(rel[4:6]) + '/'
        for f in fn:
            p = Path(dp) / f
            if p.stat().st_mtime < a.since:
                stale += 1
                continue
            maps[folder].append(p)
    print(f'light maps: {sum(len(v) for v in maps.values())} current, {stale} left out (older build)')
    for game, (folder_name, mod_id, title, needs) in GAMES.items():
        if a.only and game != a.only:
            continue
        mod = Path(a.out) / folder_name
        folders = sorted(f for f, g in plan.items() if g == game)
        sizes = {}
        tops = defaultdict(set)
        for f in folders:
            top, name = f.strip('/').split('/')
            src = hd / top / name
            target = mod / 'hd' / top / (name + '.ztd')
            target.parent.mkdir(parents=True, exist_ok=True)
            if a.resume and target.exists():
                sizes[target.relative_to(mod).as_posix()] = target.stat().st_size
                tops[top].add(name)
                continue
            entries = []
            if src.is_dir():
                for dp, dn, fn in os.walk(src):
                    for n in fn:
                        p = Path(dp) / n
                        entries.append((p, p.relative_to(hd).as_posix()))
            entries += [(p, p.relative_to(hd).as_posix()) for p in maps.get(f, [])]
            if not entries:
                continue
            entries.sort(key=lambda e: e[1])
            tmp = target.with_suffix('.ztd.tmp')
            with zipfile.ZipFile(tmp, 'w', zipfile.ZIP_STORED) as z:
                for p, name_in in entries:
                    info = zipfile.ZipInfo(name_in, date_time=(2026, 11, 6, 0, 0, 0))
                    info.compress_type = zipfile.ZIP_STORED
                    z.writestr(info, p.read_bytes())
            with zipfile.ZipFile(tmp) as z:
                if len(z.namelist()) != len(entries):
                    raise RuntimeError(f'{tmp}: {len(z.namelist())} entries, {len(entries)} expected')
            os.replace(tmp, target)
            sizes[target.relative_to(mod).as_posix()] = target.stat().st_size
            tops[top].add(name)
            print(f'{title}: {f} {len(entries)} files', flush=True)
        # parts: by kind of art, then cut at --part-gib, in name order
        limit = a.part_gib * 2**30
        parts = []
        kinds = [k for k in LABELS if any(GROUPS.get(t, 'icons') == k for t in tops)]
        # (fences, water and paths: named by what this game has)
        walls = [{'fences': 'fences and tank walls' if game == 'marine' else 'fences'}.get(t, t)
                 for t in ('fences', 'water', 'paths') if t in tops]
        walls = ', '.join(walls[:-1]) + ' and ' + walls[-1] if len(walls) > 1 else ''.join(walls)
        labels = dict(LABELS, walls=walls[:1].upper() + walls[1:])
        what_of = dict(WHAT, walls=walls)
        for kind in kinds:
            files = sorted((p for p in sizes if GROUPS.get(p.split('/')[1], 'icons') == kind),
                           key=lambda p: (Path(p).stem, p))
            group, total = [], 0
            def close():
                if group:
                    n = sum(1 for p in parts if p['id'].rsplit('-', 1)[0] == kind)
                    parts.append({'id': f'{kind}-{n + 1}', 'files': list(group), 'size': total})
            for p in files:
                if group and total + sizes[p] > limit:
                    close(); group, total = [], 0
                group.append(p); total += sizes[p]
            close()
        for p in parts:
            kind = p['id'].rsplit('-', 1)[0]
            same = [q for q in parts if q['id'].rsplit('-', 1)[0] == kind]
            first, last = Path(p['files'][0]).stem, Path(p['files'][-1]).stem
            label = labels[kind]
            p['name'] = (label if len(same) == 1 else f'{label} ({first})' if len(p['files']) == 1
                         else f'{label} ({first[:1].upper()}-{last[:1].upper()})' if first[:1] != last[:1]
                         else f'{label} ({first}-{last})')
        what = ', '.join(what_of[k] for k in kinds if k != 'icons')
        what += (', ' + WHAT['icons']) if 'icons' in kinds else ''
        meta = {'id': mod_id, 'name': f'ZT1 HD Art (AI upscaled): {title}', 'author': 'AlexC1991',
                'version': '1.0', 'about': ABOUT.format(game=title, what=what, needs=needs),
                'replaces': [], 'parts': [{k: p[k] for k in ('id', 'name', 'files')} for p in parts]}
        if (mod / 'cover.png').exists():
            meta['picture'] = 'cover.png'
        (mod / 'mod.json').write_text(json.dumps(meta, indent=2) + '\n', encoding='utf-8')
        light = json.loads((Path(a.pack) / 'lighting.json').read_text())
        light['colour'] = 'hd/<top>/<name>.ztd: <native animation>/<direction>_<frame>.png'
        light['maps'] = 'hd/<top>/<name>.ztd: _lighting/<profile fingerprint>/<profile>/<view>/<native animation>/<direction>_<frame>.png'
        (mod / 'lighting.json').write_text(json.dumps(light, indent=2) + '\n', encoding='utf-8')
        print(f"{title}: {len(sizes)} archives, {sum(sizes.values()) / 2**30:.1f} GiB, parts: " +
              ', '.join(f"{p['name']} {p['size'] / 2**30:.1f} GiB" for p in parts), flush=True)


if __name__ == '__main__':
    sys.exit(main())
