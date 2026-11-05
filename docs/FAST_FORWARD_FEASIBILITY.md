# Fast-forward backend and performance - 4 November 2026

The shared timing backend drives the normal game loop through the optional
HUD speed button and the existing developer `speed` command. It supports 1x,
2x and 5x without speeding up camera controls. These are local development
results, not release approval.

## Timing

`GameSpeedClock.hpp` preserves the original single variable-delta update at
1x. Accelerated simulation uses steps no larger than 1/60 second. World,
calendar, finances, goals, research and monthly charges receive the same game
time. Camera input receives real time once per displayed frame. Rendering and
UI stay outside the simulation substeps.

Pause and speed changes clear pending accelerated work. Zoo activation resets
the speed to 1x. Native-dialog dismissal, layout transitions and embedded-view
suspension reset pending/frame timing so they do not cause a catch-up burst.
This is source integration plus scheduler/native-world regression coverage;
the isolated checks do not exercise every interactive main-loop action.

The scheduler checks an 8 ms work budget between simulation ticks, permits at
most 64 ticks per frame, accepts at most 250 ms of elapsed time per accelerated
frame and retains at most two game seconds of backlog. One expensive tick can
exceed the budget. Overload can reduce achieved speed; discarded game time is
reported by `speedstatus` rather than hidden. It is not a guarantee that every
zoo or computer will sustain 5x.

## CPU reuse and GPU composition

- Shared home/show tank portals reuse their topology lookup. Wall edits and
  exhibit rebuilds invalidate it; water, height and classification checks stay
  live. This removes repeated full-wall searches in marine AI and rendering.
- Ground repaint candidates are deduplicated and tank depths are indexed by
  screen column, retaining the original membership/depth predicate.
- Large tanks compose their interior once into a GPU render target, then copy
  it through the existing depth strips. Ground repaint and tank rims retain
  their original draw order. The production path performs no CPU readback.

The GPU path applies to tanks of at least 64 tiles with at least eight strips.
Layers are frame-local, shared by that frame's strip callbacks, and repainted
every frame. This is not a persistent texture pool or a cache of animal pixels.
Logical pixel storage is capped at 8 MiB per layer and 64 MiB total; driver
allocation overhead is additional. Unsupported targets/blending/transforms
fall back to direct rendering. Zoom above 1x deliberately uses direct rendering:
an earlier image test found amplified edge differences through the zoom-in
upscaler. The CPU improvements still apply there.

The engine already used accelerated OpenGL. The measured device was an AMD
Radeon RX 6600. This change reduces repeated CPU/draw submission work and uses
GPU composition; it does not move animal AI or pathfinding onto the GPU.

## Matched short benchmark

Both runs used the same executable, scheduler, native fixture and 1280x720
renderer, with the independent HD batch running. The reference disables the
new fence reuse and GPU composition. Each mode has 24 warm-up frames and 120
measured frames. These are progressing native scenes without the production
HUD/presentation path, not whole-game FPS or minimum-machine certification.
Modes encounter different AI/animation phases; do not treat the day/night
difference as an isolated lighting cost.

| Requested rate | Reference daylight FPS | Optimized daylight FPS | Reference night FPS | Optimized night FPS |
| --- | ---: | ---: | ---: | ---: |
| 1x | 28.7 | 49.1 | 26.1 | 41.5 |
| 2x | 25.9 | 50.0 | 17.3 | 42.4 |
| 5x | 23.1 | 48.6 | 18.2 | 31.3 |

At requested 5x, the reference achieved about 3.37x daylight and 2.63x night,
with discarded catch-up time. The optimized run achieved 4.997x / 4.996x,
discarded no time, and ended below one pending tick. Reference capacity guards
failed three checks; optimized checks passed 66/66. The reference failures are
retained as evidence, not removed from the results.

Graph and machine-readable summary:
`E:/zt1-shots/fast-forward-gpu-2026-11-04/performance.png`,
`performance.svg` and `performance-summary.json`. Raw runs are
`reference-current/` and `optimized-current/` in the same directory.

## Validation

- The final shared-clock 360-game-second test passed 57 checks at 1x, 2x and
  5x. All three completed an automatic dolphin show and produced identical
  final animal, staff, guest, show and basic-book state. Their state hash also
  matches the earlier pre-optimization full-show baseline.
- The final renderer comparison passed 537 checks across four rotations,
  three zooms, lighting off/noon/sunset/night, and full/centred viewports.
  CPU reuse was pixel-exact. Active GPU composition differed by at most two
  channel levels in that fixture, within the three-level test tolerance;
  zoom-in fallback was exact. GPU counters prove the new path was exercised.
- Existing show-start and 30/30 management checks pass, including linked home
  tanks, platform relocation, feeding restrictions, stand prices, cancellation
  and a bounded 1,000-guest audience decision pass. The initial management
  wrapper crashed because its blank map lacked ff01's guest entrance. The
  wrapper now supplies that entrance and the intended 128x128 map; production
  guest admission was not changed to conceal the fixture error.
- A separate 5x sample with 302 guests present passed 36 checks and maintained
  4.997x daylight / 5.000x night without discarded time. Mean FPS was 22.8 /
  28.9, with p95 frame times 68.2 / 52.8 ms. Its lower FPS and differing phases
  show why a maintained simulation rate alone is not a smoothness guarantee.
- Clock/options regressions passed 41 + 114 checks. Existing finance checks
  passed 28/28, and the lighting suite reported zero failures. These check
  their respective systems, not every production main-loop transition.

The stock speed fixture includes four zebras, two ankylosaurs, two dolphins,
four marine specialists, two maintenance workers, a keeper, a scientist,
30 guests, linked tanks, pumps, plants/rocks, a grandstand and shops. Needs
remain active. Show research is unlocked only in the disposable fixture.
The state test uses an ideal 60 Hz reference at 1x; actual production 1x still
uses its original variable step. Basic books cover calendar and recurring
marketing costs, not every production accounting/research completion path.

## Player-facing control

The approved smaller control is attached to Play/Pause inside a shared olive
rim. One, two and three fixed-size triangles indicate 1x, 2x and 5x. Clicking
cycles those rates without changing pause. The normal component imports the
selected artwork directly, including its face, rim and symbols. Native red
Play and hover faces come from the player's game and are masked once and
cached, rather than creating textures every frame.

`Features::fastForward` (`fastforward`) defaults off. Game Options > Gameplay
labels it **Game speed**. Switching off resets normal speed and
restores the original HUD; activating another zoo starts at 1x. The calendar
and optional 24-hour clock use their existing font, baseline and recessed art.
With the clock visible, the added width comes from the Zoo Status meter's
empty space, retaining the other controls to its right.

The final x64 build passed 29 control/artwork checks and 172 options checks, including
real positioned clicks, the production speed callback, pause preservation,
modal input blocking, native hover clearing, large cash values, native-bar
pixel restoration, repeated toggles, widescreen and 2x UI scale. The clock
regression passed 41 checks plus the same 172 options checks. The timing/show
suite passed 57 checks at 1x/2x/5x. The first recreated artwork differed from
the approved design despite passing behavior checks. It was replaced by the
selected component's own colour pixels. The imported 2x frame preserves
4,252 opaque source pixels exactly; an external-reference render comparison
checked 1,271 opaque pixels in the actual engine capture, differing by at
most one channel level. A fresh visual review compared the selected button
and engine capture together. Physical pointer feel remains untested.

Receipts and actual-engine comparison images are in
`E:/zt1-shots/fast-forward-gpu-2026-11-04/runtime-speed-exact/`; earlier behavior
and scale captures are in `runtime-speed-ui/`. The timing rerun is
in `runtime-speed-backend/` and clock regression in `runtime-clock-regression/`
under that same root. The tests use private settings and muted audio. No save
format changes were made. Full main-loop accounting across month boundaries,
audio cadence, every animated asset, very large real saves and sustained
measurements without HD generation remain broader acceptance work. Test audio
was muted; no claim is made about sound pitch or effect cadence.

## Reproduce

Build the normal x64 Release target. Run `--panel-shots <output>` with
`SDL_AUDIODRIVER=dummy`, `ZT_FLAT_LAB=1`, `ZT_SPEED_CHECKS=1` and private
`ZT1_CONFIG_PATH` / `ZT1_MODS_CONFIG` files. Keep output under `E:/zt1-shots`.

- `ZT_SPEED_SECONDS`: simulation duration; 360 for complete-show checks.
- `ZT_SPEED_SHOW_SOAK=1`: three 60 Hz speed cases, no render benchmark.
- `ZT_SPEED_HZ`, `ZT_SPEED_RATE`: restrict the cadence/rate matrix.
- `ZT_SPEED_GUESTS`: admission count, 0..1000; default 30.
- `ZT_FENCE_REFERENCE=1`: original CPU work and direct composition.
- `ZT_GPU_TANK=0|1`: compositor override; transform guards still apply.
- `ZT_FENCE_COMPARE=1`, `ZT_SPEED_PIXELS_AT=180`: renderer comparisons.
- `ZT_SPEED_MANAGEMENT=1`: existing isolated show-management suite.
- `ZT_SPEED_HUD_CHECKS=1`: actual HUD, pointer input and restoration checks;
  combine with `ZT_FAST_FORWARD_CHECKS=1` for settings persistence checks in
  the private `ZT1_CONFIG_PATH`. No shared Lab or save files are changed.
- `ZT_SPEED_APPROVED_REFERENCE=<PNG>`: optional external approved component
  fixture for the 800x600 normal-state rendered artwork comparison.

Earlier feasibility receipts remain under
`E:/zt1-shots/fast-forward-2026-11-03/`. Its 180-second run had premature
show timeouts and capacity failures; the later 360-second follow-up resolved
the timeout question. Current receipts are under
`E:/zt1-shots/fast-forward-gpu-2026-11-04/`. Nothing committed or released.

## Sustained half-zoom follow-up

A 300-guest zoo at half zoom exposed a different bottleneck from the short
30-guest fixed-camera samples above. The native-size image rendered before
shrinking exceeded the 8 MiB per-tank cache cap. The fallback repeated tank
contents and floor-shadow geometry for each depth strip. The cap is now
32 MiB per tank, within the existing 64 MiB combined budget.

Matched 100-second daytime samples improved from 3.28 to 15.43 FPS.
The profiler's 240-frame samples put tank contents at 223.25 ms before and
10.90 ms after. Expanded renderer comparisons passed 729 checks across
four zooms, four rotations, lighting off/noon/sunset/night and both viewports.
The largest GPU colour difference was two levels.

The longer reruns averaged 29.29 FPS with lighting off, and 15.20 / 15.97 /
15.76 FPS with lighting at 1x / 2x / 5x. Achieved rates were 1.000x / 1.998x /
4.990x. Those lighting cases ran for 12 / 6 / 5 real minutes and each covered
at least one natural cycle. Day/night still has a substantial rendering cost
and full-cycle 1% lows remain around 5.6 to 5.8 FPS. Population evolves in
these runs, so results at different speeds are not isolated overhead tests.

Raw before/after telemetry, graphs and measurement details are in
`E:/zt1-shots/sustained-game-benchmark-2026-11-04/tank-cache-fix/`.
The parent directory retains the original eight-case long benchmark.
