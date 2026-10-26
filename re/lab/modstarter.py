r"""The ZT1 Lab's starter kit: a new mod with one of everything a mod can
have, working, for a modder to copy from (the Mods tab's New mod > Starter
kit).

Made from the player's own game (the engine folder's archives, as the
animation maker reads them: src/AniFile.cpp's frame format), so no game art
is shipped with the Lab:
  - content.ztd   a new scenery object, a gold copy of the base game's
                  Large Rock (objects/lrock1): its .ucs (the game's
                  user-made entity file: name, cost, where it stands), its
                  map art and buy-panel icons, each with its own palette
  - shaders/      an art shader, off at first (Game Options > Shaders)
  - hd/           the object's map art at 4x (shown zoomed in)
  - mod.json      the mod's details and one setting of its own
  - README.md     what each file is and how to make it yours

One JSON request on stdin, one JSON answer on stdout:
  {"engineDir": ..., "modsFolder": ..., "mod": {"folder", "name", "author"}}
      -> {"ok": true, "mod": <its folder>, "object": <its type>, "files": [...]}
"""
import hashlib
import json
import os
import struct
import sys
import zipfile
import zlib

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import animaker  # noqa: E402

BASE = 'lrock1'
GOLD = (255, 196, 64)


def gold(cols):
    """A palette turned gold, each colour keeping its lightness (entry 0,
    the clear one, as it was)"""
    out = list(cols)
    for i, c in enumerate(cols):
        if i == 0 or not c:
            continue
        r, g, b = animaker.rgb(c)
        lum = (0.3 * r + 0.59 * g + 0.11 * b) / 160.0
        mixed = [min(255, int(0.75 * k * lum + 0.25 * v)) for k, v in zip(GOLD, (r, g, b))]
        out[i] = (c & 0xFF000000) | animaker.entry(*mixed) & 0xFFFFFF
    return out


def renamed_ani(text, name):
    """An .ani pointed at the new object's folder (dir1)"""
    lines = []
    for line in text.decode('latin1').splitlines():
        k = line.split('=', 1)[0].strip().lower()
        lines.append('dir1 = ' + name if k == 'dir1' else line)
    return ('\r\n'.join(lines) + '\r\n').encode('latin1')


def png(w, h, rgba):
    """A PNG of w x h straight-alpha pixels (bytes, 4 a pixel)"""
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xFFFFFFFF)
    rows = b''.join(b'\0' + rgba[y * w * 4:(y + 1) * w * 4] for y in range(h))
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b''))


def hd_frame(frame, pal, scale=4):
    """A frame drawn with its palette, scale times bigger (each pixel a
    scale x scale block: a modder paints over it, or runs it through the
    Lab's HD art script)"""
    w, h = frame['w'], frame['h']
    px = bytearray(w * h * 4)
    for y, runs in enumerate(frame['lines']):
        x = 0
        for skip, colours in runs:
            x += skip
            for c in colours:
                if 0 <= x < w:
                    r, g, b = animaker.rgb(pal[c])
                    px[(y * w + x) * 4:(y * w + x) * 4 + 4] = bytes((r, g, b, 255))
                x += 1
    W, H = w * scale, h * scale
    big = bytearray(W * H * 4)
    for y in range(H):
        row = (y // scale) * w
        for x in range(W):
            s = (row + x // scale) * 4
            big[(y * W + x) * 4:(y * W + x) * 4 + 4] = px[s:s + 4]
    return png(W, H, bytes(big))


UCS = """; {title}: a new scenery object (the game's user-made entity file, .ucs).
; The game lists every .ucs it finds (here scenery/other/); [Member]
; says which buy panel: rocks are the Habitat panel's Rocks tab (its Mods
; filter shows mods' things only). Type (below) is the folder its art is
; in: objects/{name}/.

[Icon]
Icon = objects/{name}/SE/SE
Icon = objects/{name}/SW/SW
Icon = objects/{name}/NW/NW
Icon = objects/{name}/NE/NE

[Member]
rocks

[Characteristics/Integers]
cPurchaseCost = 150
cHelpID = 9200
cHabitat = 9411
cLocation = 9618
cFootprintX = 2
cFootprintY = 2
cAutoRotate = 1
cRock = 1
cHeight = 2
cSwims = 1
cOnlySwims = 0
cUseNumbersInName = 0
walkableByTall = 1
cRubbleable = 0

[Animations]
idle = idle

[defaultLCID]
LCID = 1033

[Global]
Type = {name}

[1033]
cName = {title}
cLongHelp = {title}: a gold rock, made with the ZT1 Lab's starter kit.
cTheString = the {title}

[EstheticBonus]
v = 9503
v = 20
v = 9504
v = 20
"""

FRAG = """#version 120
// {title}'s art shader: a slow golden shine over the map's art, seen
// zoomed in. Runs once on each frame of art (animals, people, fences,
// objects) as it's made - see src/ShaderMods.hpp for everything it's given.

uniform sampler2D uColor;   // the frame's colour (straight alpha)
uniform sampler2D uNormal;  // its surface: rgb a normal, a its height
uniform vec2 uSize;         // the frame, in pixels
uniform float uKind;        // 0 animal or person, 1 fence, 2 object or building

// Its settings, by name: shaders/shine.json's slider and mod.json's toggle
uniform float shine;
uniform float objectsOnly;

void main() {
  vec2 at = (floor(gl_FragCoord.xy) + 0.5) / uSize;
  vec4 c = texture2D(uColor, at);
  vec4 n = texture2D(uNormal, at);
  float lit = clamp(dot(n.rgb * 2.0 - 1.0, normalize(vec3(-0.5, 0.6, 0.6))), 0.0, 1.0);
  float amount = shine * lit * n.a;
  if (objectsOnly > 0.5 && uKind < 1.5)
    amount = 0.0;
  gl_FragColor = vec4(mix(c.rgb, c.rgb * vec3(1.25, 1.12, 0.8), amount), c.a);
}
"""

README = """# {title}

A starter kit: a working mod with one of everything a mod can have. Copy
from it, rename things, and make it yours. It was made from your own copy of
Zoo Tycoon by the ZT1 Lab (Mods > New mod > Starter kit), so nothing of the
game's is shipped with it.

## What's in it

| File | What it is |
| --- | --- |
| `mod.json` | The mod's details (name, author, version, about) and its own settings |
| `content.ztd` | Game files, in a zip renamed .ztd, under the game's own paths |
| `shaders/shine.json` + `shine.frag` | An art shader (off at first: Game Options > Shaders) |
| `hd/` | HD art: the new object's map frames at 4x, shown zoomed in |

### content.ztd

A zip with the game's paths inside. Files with the same path as the game's
take the game's place while the mod is on; new paths add new things.

| Path | What it is |
| --- | --- |
| `scenery/other/{name}.ucs` | The new object: its name, cost, footprint, and where its art is |
| `objects/{name}/idle/idle.ani` | Its map art: the .ani names the folder and its four directions |
| `objects/{name}/idle/NE`, `SE`, `SW`, `NW` | Each direction's frames (the game's own art format) |
| `objects/{name}/{name}.pal` | The map art's palette (a gold copy of the Large Rock's) |
| `objects/{name}/SE/SE.ani` + `SE/N` (and NE, SW, NW) | Its buy-panel icon, one a direction |
| `objects/{name}/i.pal` | The icons' palette |

The .ucs is an INI file. What to change first:

- `[1033] cName` - its name in the game (1033 is English; add a section for
  another language's number, e.g. `[1031]` for German).
- `cPurchaseCost` - what it costs.
- `cFootprintX` / `cFootprintY` - how many tiles it takes.
- `[Member]` - which buy panel and tab it's in (`rocks`: Habitat > Rocks;
  `foliage`, `shelters`, `scenery`... - copy it from a game object of the kind).
- `cHabitat` / `cLocation` - the biome and continent it counts towards.
- `[EstheticBonus]` - pairs of an animal-likes id and how much they like it.

**Making it your own object.** `{name}` is its type: the folder its art is in
and the .ucs's name. To rename it, change all three together - the folder
`objects/{name}/`, the .ucs's file name and `Type =`, and the `dir1 =` line
in each .ani. Keep it short (8 letters or fewer) and different from every
other object's.

**Its art.** Each frame file names its palette inside it; this kit's are
already pointed at their own palettes, so recolouring is just editing the
.pal (a count, then that many colours as 0xAABBGGRR). For new shapes, the
Lab's Game files tab shows every frame of the game's, and its animation
maker (Mods > Lab scripts) makes a recoloured animation as a mod.

### shaders/

`shine.json` names the shader, when it runs (`"stage": "art"` - each frame of
map art as it's made; `"screen"` - the whole picture, every frame) and its
settings. Each setting is a slider in Game Options > Shaders and reaches the
shader as a `uniform float` of its name. `shine.frag` is GLSL 1.20.

### mod.json settings

`settings` are the mod's own options: a row each on its page in Game Options
(Mods & Updates > its Settings button), kept in zoo.ini. Types: `toggle`,
`slider` (min, max) and `choice` (a list). Its shaders get them as uniforms
by name too: this kit's `objectsOnly` toggle keeps the shine to objects.

### hd/

`hd/<the .ani's folder>/<direction>_<frame>.png`, a whole multiple of the
frame's size (here 4x). These are the plain frames made bigger: paint over
them, or run them through the Lab's HD art script for an upscaled set.

## Trying it

1. In the Lab's Mods tab, pick this mod and switch it **On**.
2. **Try in the Test Zoo**: the Habitat panel's Rocks tab, with the **Mods**
   filter, has *{title}*. Place it, turn the camera (it has all four directions),
   zoom in (its HD art).
3. **Check it** (Publish page) says what the mod repository would turn down.
4. Change something, then try again: the Test Zoo reads the mod afresh each
   time it starts.

## Sharing it

The Publish page uploads it to the mod repository (sign in to CodeRook
first). Give it your own `id` and `name` in mod.json before you do.
"""


def make(req):
    game = animaker.Game(req['engineDir'])
    m = req['mod']
    title = m.get('name') or m['folder']
    # (its type: short, and its own - from the folder's name)
    name = 'sk' + hashlib.sha1(m['folder'].encode('utf-8')).hexdigest()[:6]
    base = 'objects/' + BASE
    own = 'objects/' + name
    files = {}

    # Its map art: the Large Rock's idle animation, pointed at its own palette
    ani = game.read(base + '/idle/idle.ani')
    _, dirs = animaker.parse_ani(ani)
    pal_cols = gold(animaker.read_pal(game.read(base + '/' + BASE + '.pal')))
    pal_cols += [0] * (256 - len(pal_cols))
    files[own + '/idle/idle.ani'] = renamed_ani(ani, name)
    files[own + '/' + name + '.pal'] = animaker.write_pal(pal_cols)
    hd = {}
    for d in dirs:
        art = animaker.parse_frames(game.read(base + '/idle/' + d))
        art['pal'] = own + '/' + name + '.pal'
        files[own + '/idle/' + d] = animaker.write_frames(art)
        for i, f in enumerate(art['frames']):
            hd['hd/' + own + '/idle/%s_%d.png' % (d, i)] = hd_frame(f, pal_cols)

    # Its buy-panel icons: a direction each, one palette for them all
    icon_cols = gold(animaker.read_pal(game.read(base + '/i.pal')))
    files[own + '/i.pal'] = animaker.write_pal(icon_cols)
    for d in ('NE', 'SE', 'SW', 'NW'):
        ani = game.read('%s/%s/%s.ani' % (base, d, d))
        files['%s/%s/%s.ani' % (own, d, d)] = renamed_ani(ani, name)
        for frame in animaker.parse_ani(ani)[1]:
            art = animaker.parse_frames(game.read('%s/%s/%s' % (base, d, frame)))
            art['pal'] = own + '/i.pal'
            files['%s/%s/%s' % (own, d, frame)] = animaker.write_frames(art)

    files['scenery/other/' + name + '.ucs'] = UCS.format(title=title, name=name).replace('\n', '\r\n').encode('latin1')

    out_dir = os.path.join(req['modsFolder'], m['folder'])
    os.makedirs(out_dir, exist_ok=True)
    with zipfile.ZipFile(os.path.join(out_dir, 'content.ztd'), 'w', zipfile.ZIP_DEFLATED) as z:
        for path, data in files.items():
            z.writestr(path, data)
    for path, data in hd.items():
        p = os.path.join(out_dir, *path.split('/'))
        os.makedirs(os.path.dirname(p), exist_ok=True)
        with open(p, 'wb') as f:
            f.write(data)

    sh = os.path.join(out_dir, 'shaders')
    os.makedirs(sh, exist_ok=True)
    with open(os.path.join(sh, 'shine.json'), 'w', encoding='utf-8') as f:
        json.dump({'name': title + ' Shine',
                   'about': 'A slow golden shine on the lit side of the map art, seen zoomed in. '
                            "The starter kit's example art shader.",
                   'stage': 'art', 'shader': 'shine.frag', 'on': False,
                   'settings': [{'name': 'shine', 'label': 'Shine', 'min': 0, 'max': 1, 'value': 0.5,
                                 'about': 'How golden the lit side gets.'}]}, f, indent=2)
        f.write('\n')
    with open(os.path.join(sh, 'shine.frag'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(FRAG.replace("{title}", title))
    with open(os.path.join(out_dir, 'README.md'), 'w', encoding='utf-8', newline='\n') as f:
        f.write(README.format(title=title, name=name))

    info = {'id': m.get('id') or '', 'name': title, 'author': m.get('author', ''), 'version': '1.0',
            'about': "A starter kit: a new gold rock in the Habitat panel's Rocks (its .ucs, art, icons and HD art), "
                     'an art shader and a setting of its own. See README.md.',
            'replaces': [],
            'settings': [{'name': 'objectsOnly', 'label': 'Shine on objects only',
                          'about': 'The Shine shader gilds only objects and buildings, not animals, '
                                   'people or fences.', 'type': 'toggle', 'value': True}],
            # (the Mod Menu's picture: the Large Rock in its own palette, and
            # the original's when the mod is off)
            'preview': (base + '/idle/SE').lower(), 'palette': (own + '/' + name + '.pal').lower(),
            'originalPalette': (base + '/' + BASE + '.pal').lower()}
    with open(os.path.join(out_dir, 'mod.json'), 'w', encoding='utf-8') as f:
        json.dump(info, f, indent=2)
        f.write('\n')
    return {'ok': True, 'mod': out_dir, 'object': name, 'files': sorted(files) + sorted(hd)}


def main():
    req = json.loads(sys.stdin.read())
    try:
        res = make(req)
    except Exception as e:
        res = {'ok': False, 'error': '%s: %s' % (type(e).__name__, e)}
    print(json.dumps(res))


if __name__ == '__main__':
    main()
