# Comparing against the original game

Tools used to verify map decoding and terrain drawing against the original
Zoo Tycoon (see `docs/MAP_TILE_FORMAT.md` for what they established).

## Running the original

- Installed at `C:\Program Files (x86)\Microsoft Games\Zoo Tycoon`.
- The CD check wants the Marine Mania disc: mount `MARINA.iso` and point
  `HKLM\SOFTWARE\WOW6432Node\Microsoft\Microsoft Games\Zoo Tycoon\1.0\CDPath`
  at its drive letter.
- Windowed mode (`fullscreen=0` in `zoo.ini`) crashes on this PC; fullscreen
  800x600 works and screen capture still sees it.
- Answer the DirectPlay "Windows Features" prompt yourself (Skip is fine).

## Scripts

| Script | What it does |
|--------|--------------|
| `zt_server.ps1` | Long-running helper: finds the game window (class `Zoo`), clicks, screenshots its 800x600 frame. Driven over stdin by `capture_orig.py`. |
| `capture_orig.py [stems...]` | Start on the Freeform map list. For each map: select, Play, wait for the HUD, capture default zoom + zoomed-out start view + a 3x3 grid of minimap-click views into `orig/`, then return to the list. ~1 min per map; don't touch the mouse. |
| `compare.py <ours_dir> <out_dir> [stems...]` | Finds each original view inside our full-map render (masked NCC, HUD masked) and writes side-by-side sheets and scores. |
| `minimap_check.py <stem>` | Projects a map's terrain like the minimap (normal vs transposed). Note: on medium/large maps the original minimap is a scrolling window, not the whole map. |
| `fit_light.py` | Regresses the original's per-pixel brightness against our normals. Needs unlit (`shadingMode` 1) and normal-map (`shadingMode` 2) renders. |

## Our renders

From `build/Release`:

```bash
zt1-engine.exe --render-maps <outDir> 16 32 0 <stem,stem|all> [viewRotation] [shadingMode]
```

`16` = pixels per height unit, `32` = tile width (the original's zoomed-out
size). Freeform maps whose `.scn` name differs from the `.zoo` render under
the `.scn` name (`ff01` = default, `ff02` = medium, `ff03` = large).
