# ZT1 Toon Shaders - and how to make a shader mod

This mod gives the map's animals, people, fences and objects a toon (cel) look,
and adds an optional colour grade for the whole picture. It also serves as the
example for writing your own shaders.

Switch the mod on in **Game Options › Mods & Updates**. Then open **Game Options › Shaders** to turn each shader on or off and adjust its settings with sliders. Changes show at once; art shaders show on the map when you zoom in.

## Making a shader mod

A shader mod is a normal mod folder (in the mods folder) with a `mod.json` and
a `shaders/` folder. Each shader is two files:

- `shaders/<id>.json` - what it is:
  ```json
  {
    "name": "My Shader",
    "about": "One line for the Shaders page.",
    "stage": "art",
    "shader": "<id>.frag",
    "on": true,
    "settings": [
      {"name": "strength", "label": "Strength", "min": 0, "max": 1, "value": 0.5,
       "about": "What this slider does."}
    ]
  }
  ```
  Each setting becomes a `uniform float` with that name, and a slider on the
  Shaders page. Up to 12 sliders are shown.
- `shaders/<id>.frag` - a GLSL 1.20 fragment shader (`#version 120`).

### Stages

**`art`** runs once on every frame of the map's art (animals, people, fences, objects) when that frame is made for the zoomed-in map. The frame arrives at 4x size: the HD art mod's frame if one is on, otherwise the original upscaled. The result is kept, so heavy shaders are fine. Inputs:

| uniform | what |
|---|---|
| `sampler2D uColor` | the frame's colour, straight alpha |
| `sampler2D uNormal` | its surface, worked out by the engine from the sprite: `rgb` = normal (x right, y up, z out of the screen; 0.5 = 0), `a` = height (0 at the outline, 1 at its thickest). The ground shadow is not part of the body. |
| `vec2 uSize` | the frame's size in pixels |
| `float uKind` | 0 animal or person, 1 fence (thin lines), 2 object or building |
| `float uTime` | seconds |

Write the new colour (straight alpha) to `gl_FragColor`. To read a pixel at its own place, use:

```glsl
vec2 p = floor(gl_FragCoord.xy);
vec4 c = texture2D(uColor, (p + 0.5) / uSize);
```

Neighbours are `p + vec2(dx, dy)`.

**`screen`** runs on the whole finished picture, every frame (menus included). Inputs are `uColor` (the frame, read at `gl_FragCoord.xy / uSize`), `uSize` (the window) and `uTime`. Keep it light, because it runs every frame.

Several shaders can be on at once. Art shaders run in name order, each one on the last one's result.

### Mod settings

A shader's own `settings` show on the Shaders page. A mod can also have settings of its own, in its `mod.json`. They show on the mod's own page: Game Options > Mods & Updates, then the **Settings** button beside the mod.

```json
"settings": [
  { "name": "peopleOnly", "label": "Animals and people only",
    "about": "Shown under the page when the mouse is on it.",
    "type": "toggle", "value": false }
]
```

`type` is one of:

- `slider`, with `min`, `max` and `value`.
- `toggle`, a tick box. `value` is true or false, which reaches the shader as 1 or 0.
- `choice`, with `"choices": ["Soft", "Sharp"]`. `value` is one of the choices, which reaches the shader as its place in the list.

Values are kept in `zoo.ini` under `[Mod:<folder>]`. Each one reaches the mod's shaders as a `uniform float` of its name; if a shader has its own setting with the same name, the shader's setting wins. This mod's `peopleOnly` is an example.

### Testing

The game's dev console has a `shader` command. On its own it lists every shader with its settings and whether it compiled. It can also change things:

- `shader <mod/id> on|off` switches a shader.
- `shader <mod/id> <setting> <value>` sets a value.

A shader that fails to compile shows "(didn't compile)" on the Shaders page, and the console prints the compiler's error.

## How the toon shader works

`toon.frag` uses the standard toon (cel) model from 3D toon shaders:

1. **Flat colour:** the sprite's painted grain is smoothed while edges are kept (a bilateral blur), and its painted light and shade are partly evened out (*Flat Colour*).
2. **Self shadow:** the light against the surface (N·L) is cut at *Shadow Start*, with a hard or soft edge set by *Shadow Edge*.
3. **Shadow colour:** the shade is tinted (*Shade Tint*) and darkened (*Shade Dark*), not just made darker.
4. **Gloss:** a sharp highlight on the lit side.
5. **Rim light:** a lit edge where the surface turns away from the viewer.
6. **Ink outlines:** around the silhouette, and inside where the surface's depth or direction steps, like screen-space depth and normal outlines (*Outline Width*, *Outline Dark*).

*Keep Painting* brings back some of the original art's own shading.

MIT licence, like the engine.
