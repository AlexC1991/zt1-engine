#!/usr/bin/env python3
"""The everything zoo, judged by the original game.

The engine's everything zoo (ZT_EVERYTHING_ZOO, src/EverythingZoo.cpp) saves a
shared zoo holding every type in the registry, and beside it what its
original part holds: everything-<map>-<part>.json, by class/subclass/type.
The Lab loads that zoo in the original (zoo.exe) and reads, from its memory,
every entity it actually made of it (Play Game's census: origLive). This
compares the two: a type the original made fewer of than were saved was
dropped by it - a record it refused or skipped without a word, as the update
packs' trees once were - and fails.

What the original makes on its own is listed, not failed: the water's
ripples and waves, wildlife, guests that walked in since.

  python everything.py <everything-...json> <census.json>   -> report, exit 1 on a drop
  one JSON request on stdin {"manifest": path, "census": path or object}
      -> {"ok", "dropped": [...], "extra": [...], "checked": n}
"""
import json
import sys

# The engine's record class -> the census's class (the original's class
# tables, as the Lab names them), and which part of the record names its type
CLASSES = {
    'animals': ('Animals', 'sub'),
    'keeper': ('ZTKeeper', 'sub'),
    'maint': ('ZTMaintenanceWorker', 'sub'),
    'tour': ('ZTTourGuide', 'sub'),
    'helicopter': ('ZTHelicopter', 'sub'),
    'guests': ('Guests', 'sub'),
    'building': ('Buildings', 'type'),
    'objects': ('Scenery', 'type'),
    'paths': ('Paths', 'type'),
    'fences': ('Fences', 'sub'),
    'tankwall': ('Tank walls', 'sub'),
    'food': ('ZTFood', 'type'),
    'filter': ('ZTTankFilter', 'sub'),
    'ambient': ('Ambient animals', 'type'),
}
# Made by the original itself, whatever the save holds
OWN = {('Scenery', 'tankripl'), ('Scenery', 'twaterwv'), ('Scenery', 'frshwav')}
# What the original turns one thing into as it loads it: the mermaid statue
# in a tank becomes a mermaid (Marine Mania's own; census-verified)
BECOMES = {('Scenery', 'statmerm'): ('Animals', 'mmaid')}


def compare(manifest, census):
    # (the map's own records the original has no type for - it skips them in
    # its own map too: lg_sandy's jeep1, which ZUPDATE's scenery.cfg leaves out)
    skips = set()
    for key in manifest.get('originalSkips', []):
        cls, sub, typ = key.split('/')
        if cls in CLASSES:
            name, by = CLASSES[cls]
            skips.add((name, (sub if by == 'sub' else typ).lower()))
    expected = {}
    for key, n in manifest['records'].items():
        cls, sub, typ = key.split('/')
        if cls not in CLASSES:
            expected.setdefault(('?' + cls, sub + '/' + typ), 0)
            expected[('?' + cls, sub + '/' + typ)] += n
            continue
        name, by = CLASSES[cls]
        k = (name, (sub if by == 'sub' else typ).lower())
        expected[k] = expected.get(k, 0) + n
    made = {}
    for name, c in census.get('classes', {}).items():
        for t, n in c.get('types', {}).items():
            made[(name, t.lower())] = made.get((name, t.lower()), 0) + n
    dropped, extra = [], []
    for k, n in sorted(expected.items()):
        if k[0].startswith('?'):
            dropped.append({'class': k[0][1:], 'type': k[1], 'saved': n, 'made': None,
                            'why': 'no census class for this record class: add it to CLASSES'})
            continue
        got = made.get(k, 0)
        if got < n and k in skips:
            extra.append({'class': k[0], 'type': k[1], 'saved': n, 'made': got, 'own': True,
                          'skipped': 'the map\'s own, which the original skips'})
            continue
        if got < n and k in BECOMES:
            into = BECOMES[k]
            spare = made.get(into, 0) - expected.get(into, 0)
            if spare >= n - got:
                made[into] = made.get(into, 0) - (n - got)
                extra.append({'class': k[0], 'type': k[1], 'saved': n, 'made': got,
                              'own': True, 'became': '%s %s' % into})
                continue
        if got < n:
            dropped.append({'class': k[0], 'type': k[1], 'saved': n, 'made': got})
    for k, n in sorted(made.items()):
        have = expected.get(k, 0)
        if n > have:
            extra.append({'class': k[0], 'type': k[1], 'saved': have, 'made': n, 'own': k in OWN})
    return {'ok': not dropped, 'dropped': dropped, 'extra': extra, 'checked': len(expected),
            'writer': manifest.get('writer', ''), 'map': manifest.get('map', ''),
            'saved': sum(manifest['records'].values()), 'entities': census.get('entities')}


def load(x):
    if isinstance(x, dict):
        return x
    with open(x, encoding='utf-8') as f:
        return json.load(f)


def main():
    if len(sys.argv) >= 3:
        r = compare(load(sys.argv[1]), load(sys.argv[2]))
        print(f"{r['checked']} kinds checked, {r['saved']} records saved, {r['entities']} entities in the original")
        for d in r['dropped']:
            print(f"DROPPED {d['class']} {d['type']}: saved {d['saved']}, the original made {d['made']}"
                  + (f" ({d['why']})" if d.get('why') else ''))
        for e in r['extra']:
            print(f"{'own  ' if e['own'] else 'extra'} {e['class']} {e['type']}: saved {e['saved']}, made {e['made']}"
                  + (f" (the original made it a {e['became']})" if e.get('became') else '')
                  + (f" ({e['skipped']})" if e.get('skipped') else ''))
        print('PASS: the original made everything saved' if r['ok'] else f"FAIL: {len(r['dropped'])} kinds dropped")
        sys.exit(0 if r['ok'] else 1)
    req = json.loads(sys.stdin.read() or '{}')
    try:
        print(json.dumps(compare(load(req['manifest']), load(req['census']))))
    except Exception as e:
        print(json.dumps({'ok': False, 'error': str(e)}))


if __name__ == '__main__':
    main()
