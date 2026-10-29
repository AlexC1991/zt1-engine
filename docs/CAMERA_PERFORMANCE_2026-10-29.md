# Camera and placement performance - 29 October 2026

Status: measured improvements in the local engine; intermittent marine crowd
stalls remain unresolved. The earlier packaged candidate does not contain these
changes. Nothing was published.

## What changed

- Bound terrain traversal to a conservative visible rectangle before projecting
  every map tile. Include the highest terrain and lowest pit/cliff floor and
  preserve the existing diagonal order and final per-tile visibility check.
- Remove off-screen sprite art and fully clipped custom tank strips before
  sorting/submission. Keep unknown bounds and custom work without an explicit
  off-screen clip. Use full animation canvases and a rounding margin.
- Fix Lab-held keys: synthetic SDL events do not update SDL_GetKeyboardState.
  Merge only the Lab's held keys with physical keyboard state; retain modal,
  console and text-edit input suppression. Physical controls are not intercepted.
- Extend F9 frame timing with camera movement, paused-frame counts, long-frame
  counts and costs for input, simulation, drawing and presentation. Lab-only F8
  compares the original traversal/submission path against the optimized path.

## Method and limits

All game controls, scene construction, source edits and builds used the Lab MCP.
The new land scene was built with actual Lab placement tools. Play then used its
normal catalogue, gender/adopt button, rotation controls, mouse previews, clicks,
path drag and native Save/Load dialogs. Diagnostic files are separate from the
owner's source saves. No downloaded mod selection was changed.

F9 records 240 complete visible frames at 1904x897, with the normal HUD and an
accelerated OpenGL renderer. Each pan is a bounded sequence of held arrow keys.
Camera counters prove actual motion; simulated mouse-edge movement is not
claimed as an edge-scroll test. All-frame paused counts distinguish rendering
tests from running simulation. Award/welfare pop-ups and concurrent owner camera
input interrupted some exploratory samples; those are not running evidence.

Frame rate is capped near 60. These short samples are observations on this PC,
not a universal frame-rate claim. The crowded scene deliberately stresses drawing
and does not represent a fully suitable habitat. Welfare warnings were dismissed;
captures stopped by a warning are excluded from running results.

## Workload results

The medium grass stress zoo has four exhibits, 48 zebras/elephants/giraffes/tigers,
four keepers, 35 placed buildings/shelters, four additional decorative buildings,
48 trees/rocks, perimeter fences and connected paths. It starts with 328 placed
objects including the stock map. A separate diagnostic starts 400 real guest
instances beside shops; their normal AI chooses buildings and records purchases.

| Capture | Simulation | Observed FPS | p95 / p99 / maximum frame, ms |
| --- | --- | ---: | --- |
| Mixed land scene, wide-view pan, original path | running | 56.7 | 19.351 / 21.597 / 39.155 |
| Mixed land scene, wide-view pan, optimized | running | 57.0 | 18.911 / 21.736 / 26.598 |
| Busy shop view, 408 guests / 48 animals, stationary | running | 58.4 | 18.258 / 18.380 / 18.964 |
| Busy shop view, 409 guests / 48 animals, pan | running | 58.2 | 18.217 / 18.669 / 20.941 |
| Attractions and carousel, 401 guests / 48 animals, pan | running | 57.1 | 18.428 / 28.435 / 43.430 |
| Close zoom 1.46, 419 guests / 48 animals, pan | running | 58.0 | 18.096 / 18.587 / 18.870 |
| Tree preview, 416 guests / 48 animals | running | 58.6 | 18.352 / 18.671 / 18.824 |
| Rocks-category preview, 418 guests / 48 animals | running | 58.3 | 18.082 / 19.003 / 28.217 |
| Path drag and commit, 425 guests / 48 animals | running | 58.1 | 18.103 / 18.589 / 18.975 |
| Zebra preview and mouse movement | paused rendering | 55.8 | 19.891 / 30.955 / 55.298 |
| Gift stand preview and mouse movement | paused rendering | 57.4 | 18.763 / 21.300 / 22.368 |
| Fancy burger stand preview | paused rendering | 57.9 | 18.516 / 18.963 / 19.382 |
| Carousel preview at zoom 1.54 | paused rendering | 58.4 | 18.035 / 18.322 / 18.552 |

The zebra placement increased the live animal count from 48 to 49. Gift stand,
fancy burger stand, rotated carousel, tree and rocks-category placements were
also clicked in Play. The carousel preview was tested on clear and obstructed
ground. Native saves retain the diagnostic zoo and placement examples.

Shop read-back verifies real visits, not just occupied-looking sprites: Burger
Stand 1 had 7 visits/$49 income, Drink Stand 1 6/$20, Ice Cream Stand 1 3/$18,
Hot Dog Stand 1 4/$24 and Frozen Yogurt Stand 1 6/$36. Added attraction paths let
guests use the carousel; its occupied animation is visible in the captured Play
frames and Carousel 2 shows $320 income in owner-heavy-paused.png. The committed
path drag is also visible there. Bulk guest placement is diagnostic setup, not
a public-player workflow.

## Rendering checks

- Activity checks: 56/56, including building/gate/diver animations and the existing
  exact whale-shark wall-clip comparisons. Sprite pruning also matches reference
  pixels over 80 combinations of zoom, rotation and screen-edge position with
  real buildings, used/background layers, clipped halves and custom passes.
- Terrain: 224 pixel comparisons on the actual renderer. Four zooms, four
  rotations, map corners, raised cliffs, deep pits, paths and partial ground
  redraws match the complete traversal.
- Populated Play scene: eight reference/candidate screenshot pairs (four camera
  directions at two zooms) have zero differing pixels in the visible world crop
  x=200..1903, y=70..789. HUD/banners are excluded.
- Marine management: 30/30. Complete programs and returns for the six stock show
  species: 27/27.

The earlier performance checkpoint executable SHA-256 is
421DEF487AE03B748BE729B2B52D6B078D65F10A27E5CCA88E74B2599CB28C34.

## Evidence and remaining work

Files are under E:/zt1-shots/camera-performance-2026-10-29/. Useful logs are
heavy-land-ab.log, zebra-placement.log, shop-placement.log, carousel-preview.log,
busy-shops-running.log, attractions-clean-running.log, busy-close-pan.log,
scenery-placement.log and path-drag.log. heavy-pixel-comparison.json records the
eight screenshot comparisons; final-regressions.txt contains the complete check
reports. busy-final-paths.zt1save is the latest stress scene.

Earlier marine panning tests still show occasional long frames, including running
crowd captures above 50 ms. Terrain traversal costs fell in comparable samples,
but that does not establish that all stalls are fixed. Keep that investigation
and owner acceptance open. Rebuild and re-verify game/portable/Lab packages after
the final engine work, then complete isolated install/update/relaunch tests.

## Latest save-build measurements

The final development build was measured again with the Test Zoo blank and
paused in the background. Each valid capture has 240 frames and zero paused
frames; camera counters confirm movement. Reports are under
E:/zt1-shots/save-compatibility-2026-10-29/.

| Running workload | Mean / median ms | p95 / p99 / max ms | Camera moved frames |
| --- | --- | --- | ---: |
| 1,000 guests, 3 dolphins, zoom 1.80 (169) | 19.875 / 19.630 | 23.313 / 24.420 / 26.445 | 221 |
| Same scene repeated (171) | 20.405 / 20.224 | 23.796 / 24.933 / 29.180 | 220 |
| Rotated marine view, zoom 1.80 (173) | 20.061 / 19.997 | 23.149 / 24.998 / 26.327 | 239 |
| Rotated wider view, zoom 1.34 (174) | 20.164 / 20.144 | 23.321 / 25.328 / 28.509 | 239 |
| Busy land/attractions, 378 guests, 48 animals, zoom 1.46 (176) | 17.293 / 17.300 | 18.248 / 18.798 / 22.723 | 218 |

These samples contain no frames above 33 ms. They do not erase earlier observed
stalls or guarantee 60 FPS: the dense marine view is about 49-50 FPS. In capture
171, average rendering takes 17.430 ms, world update 1.639 ms, fence collection
3.320 ms, sprite submission 3.640 ms and tank composition 4.559 ms. These scopes
overlap and must not be added together. No further rendering changes were made
from these short observations. Award-interrupted captures 162/163/166/170 are
paused rendering evidence and excluded from the running results.

Final development reruns pass terrain 224/224, activity 77/77, parity 57/57,
building/DRT 55/55, show polish 39/39 and marine management 30/30. Packaged
executable checks and owner acceptance of residual performance remain open.
