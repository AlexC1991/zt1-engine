# Optional day and night lighting

This is an addition to the original game. `Features::dayNightLighting` is off by default and is shown in Game Options > Graphics. The visual day lasts twelve minutes, derives from the existing saved world clock, and pauses with the zoo. It does not change the calendar, animal needs, opening hours or save format.

Game Options > Gameplay > Display game time independently enables the 24-hour
HH:MM clock beside the existing month/year. `Features::showGameTime` is off by
default and persists as `[Extras] gametime`; Graphics > Day/night cycle uses
`[Extras] daynight`. Both changes apply immediately. Either may be enabled on
its own: the clock runs in constant daylight, or the night cycle runs without
a clock. Off/off restores the original date display and daylight.

The native recessed field extends along the original bar, retaining its full
rounded border, height and text baseline. At narrow sizes the money field uses
the remaining space within its original right edge. The round pause/play
control retains its size and place. With lighting on the clock uses the same
prepared hour as the sky and lamps, including developer previews. With lighting
off it derives time directly from the saved world clock, so it cannot freeze
on an old rendered hour. Both pause with the zoo. No calendar or save format
changes are needed. The separate floating clock above the map has been removed.

## Geometry and materials

`LightingAssets.cpp` holds the original eight reviewed material profiles. `LightingCatalogue.cpp` adds the remaining 89 light-bearing definitions from the inventory, using 2,803 authored regions across four views. `LightingWater.cpp` adds 16 source-free water/steam profiles; `LightingEffects.cpp` gates four shared effect profiles across the twelve native actor definitions. The static material set contains 113 definitions, including the intentionally dark broken lamp. Regions are anchored native sprite coordinates for NE, SE, SW and NW. Steady bulbs, signs and windows use a stable reference frame. Fire and water retain animation. Steam and the fountain jet are not solid shadow casters.

`LightingModel.cpp` intersects the four native views to reconstruct a solid volume. The carousel has authored open space and supports; lamp posts use a small authored mesh. Scenery keeps a fine solid for local-light rays and a lower-detail surface mesh for lighting. Original artwork and reconstructed meshes are not shipped: the player's local game resources supply them.

`LightingActors.cpp` shares geometry between colour variants, keeps at most twelve key poses per animation, builds at most two new pose models in a frame, and limits its cache to 256 models. A nearby cached pose can be used while a new pose is built. Animation time, reverse playback and snapped facing match the drawn actor.

## Sun and receivers

`LightingSun.cpp` rasterises world-space sun depth. `LightingShadows.cpp` keeps two nearby sun times, interpolates between their results, caches static receiver samples, and restores only regions touched by moving actors. Terrain, slopes, cliffs, reconstructed scenery and animated bodies participate. Native marine floor-shadow art is disabled while this option is on.

Receiver ground geometry uses quarter-tile subdivisions. Sun depth is at most 1024 pixels across; the colour lightmap is at most 640. Coarse-model self-occlusion is excluded by owner identity so voxel edges do not produce false blotches across smooth original artwork. Reconstructed normals provide directional surface shading.

## Native shadow pixels and body pigment

The original art format uses explicit shadow frames and a reserved palette entry. `Animation` retains compact runs of those source indices. It does not decide that every RGB-black pixel is a shadow. Original animal definitions with `cUsesRealShadows` or `cHasShadowImages` have separate shadow artwork and retain their black body pigment. This distinction is required by the orca. The untouched source textures remain available when the option is off and for UI artwork.

## Current bounds and catalogue work

Normal play caps physical point lights at 96, emissive regions at 160, scenery surface faces at 60,000, nearby animated actors at 160 and actor surface faces at 16,000. The check harness can raise the physical-light cap to measure source scaling. These bounds make stress results bounded; they do not prove unrestricted whole-catalogue coverage. A successful automatic mesh reconstruction is a geometry check, not visual acceptance of every animation or material. `LightingStructures.cpp` adds native fence and gate state silhouettes, transparent tank framing, ladders/platforms/portals, filter bodies, the DRT base and the existing raised-path slab/support/rail geometry. Faces and actors beyond the stated limits can lose shadow detail; this optional feature is a bounded approximation, not unlimited full-scene ray tracing.

## Local checks

All runs must be muted and write to E:, as required by AGENTS.md.

```powershell
$env:SDL_AUDIODRIVER='dummy'
$env:ZT_LIGHTING_CHECKS='1'
$env:ZT_LIGHTING_REVIEW='1'
$env:ZT_SHADOW_CHECKS='1'
./build-x64/Release/zt1-engine.exe --panel-shots E:/zt1-shots/lighting-check
```

`ZT_LIGHTING_BENCH` selects `empty`, `preview`, `small`, `populated`, `busy`, `lamps32`, `lamps96`, `lamps192` or `audit`. `ZT_BENCH_RES` selects output height. `ZT_BENCH_SIM=1` adds live world simulation. The harness writes raw frame timings, main-thread CPU times, phase totals, process private memory, persistent lighting-buffer payload and lighting texture payload. It disables vertical sync for measurement; uncapped FPS is not a promise of capped gameplay performance.

## Measured prototype baseline (2 November 2026)

Local evidence is in `E:/zt1-shots/day-night-v4-2026-11-02/index.html`, with PNG/SVG graphs, raw frame CSVs, per-run binary hashes and the hardware inventory. The tested Release executable is `8bc07baa4bfcf5b9a358599fcc28a20ae073232c46b3f2bd93fea32974133d40`. There were 53 lighting checks, 15 shadow checks, fourteen timed benchmark cases and one catalogue audit. Three populated live-simulation runs completed and exited successfully.

On a Ryzen 3 3300X / RX 6600 at 1280 x 720, the populated panning fixture (144 props, 18 animals, 6 staff, 80 guests; AI paused, poses animated) averaged 237 FPS off, 51 in daylight, 38 at sunset and 73 at night. Its sunset median/p95 were 24.72/39.68 ms. Each mode contains 360 measured frames after warm-up. These are synthetic scene observations, not a whole-game guarantee. The CPU shadow cost and periodic sun-depth rebuilds still need optimisation for a crowded 60 FPS target.

Retained lighting buffer payload was approximately 52-59 MiB across the four main scenes; lighting texture payload was 3-71 MiB, excluding driver padding. First-enable frames ranged from 105 ms for the eight-item preview to 1072 ms for the busy fixture. Cache prewarming/staged work is needed to remove that hitch.

The catalogue had 676 entries, without downloaded mods. The geometry audit built 586 nonempty models from 598 sampled items/type poses; twelve flat food assets need decal treatment. Sampled model payload was 54.2 MiB. Keeping twelve similarly sized poses for every sampled actor type would project to approximately 87 MiB of models, excluding atlases, projections, receivers, textures and other animation states. This arithmetic projection does not override the runtime cache budget or constitute visual approval of every asset.

Benchmarking also exposed a pre-existing shutdown lifetime error: the renderer window was destroyed before ResourceManager's cached textures. The window owner is now declared before ResourceManager while retaining its original construction timing, so resources are destroyed before their renderer.

## Distribution boundary and planning estimates (2 November 2026)

The owner requires modified/replacement assets to remain in a separate mod project/package, outside the MIT engine source and engine/Lab installers. A graphical renderer and its cache can be part of the optional engine feature. No additional asset-lighting expansion is to proceed before the size, effort and distribution approach are understood.

The current prototype rewrites no original PNG, ANI, palette, OBJ or game archive. Its eight item profiles are C++ lighting/stencil parameters in LightingAssets.cpp. Cleaned textures, emissive overlays and reconstructed meshes are generated locally from the player's installed resources. They are not distributed asset files. Generic rendering, local reconstruction, sun/clock handling and caching therefore belong to the engine side of this boundary. Any later hand-reworked sprite frames, replacement meshes, baked emission/normal maps or altered animal/object content belong in the separate optional mod. Original-game art and locally derived copies must remain out of the engine distribution; packaging a mod does not change the project's rule against redistributing the original files/art.

Measured download-size evidence: the tested prototype executable is 8,501,760 bytes, versus 8,252,928 bytes for the installed 1 November build: +248,832 bytes (243 KiB). This is a comparison against an earlier build, including diagnostic/check code; it is not an isolated feature-only binary delta. The authored eight-item definition source is 9,011 bytes. Previous 54-87 MiB model estimates are local generated payload, not shipped download size. At that assessment point, an on-disk lighting cache had not yet been implemented; the optimisation pass below adds it.

Planning budgets, not measured package sizes or delivery commitments:

- Renderer/cache implementation: allow approximately 2-4 engineering days, including profiling and regression checks; an additional GPU implementation may change this estimate.
- A future data-only profile pack: budget 1-5 MB without duplicated art, subject to an actual serialization/package sample. Current profiles are compiled C++ parameters, so a separate profile pack would also need a loader.
- Full catalogue lighting review: allow approximately 2-4 working weeks for profile mapping and visual checks across views/animation states, without redrawing all artwork. Reassess after a representative batch of simple objects, buildings and animated rides.
- Replacement-art pack: no defensible download-size or completion estimate before choosing native/HD resolution, affected items, views and frame counts and exporting/compressing a representative sample. It is not included in the profile-review estimate.

These estimates do not mean all 676 catalogue entries need emitted light or modified art. Most plants and rocks should use the generic caster/receiver path. The active request to optimise the renderer/cache remains separate from the held asset expansion. Electrical-shock light and animal-light research are pending this distribution assessment; neither was added during this assessment.

## Approved work order (2 November 2026)

The focus is now the optional native-art shadow and light mapping pass across terrain/paths, objects, buildings, fences, staff, guests and animals. Coverage means casting and receiving where physically appropriate; an entry does not become an emitter merely because it exists. Keep the existing water treatment subtle. Map native lamp glass, signs, windows, ride bulbs and existing special effects, including guest/dinosaur fence shocks. Animal emitters require a species-specific evidence record. Distinguish bioluminescence from fluorescence and reflected colour; a fluorescent fossil under a laboratory laser does not establish a glowing living dinosaur.

The owner approved continuing this graphical approach without redrawing/replacing native artwork. It supersedes the temporary asset-expansion hold in the preceding assessment. Optimisation and cache work come first, followed by reviewed batches with a coverage ledger. The current eight authored profiles remain the only visually approved item mappings until further batches pass review. Terrain, fences, tank structures, crowded actors and other native art still need full coverage/quality checks; reconstruction success alone is not visual approval.

FreeRoam is a separate future graphical milestone. Resume the previously planned HD pack after FreeRoam is finished, using these mappings/shadow details as its foundation. HD/replacement assets and additional designed features such as tank lighting belong in a separate mod project/package with their own distribution metadata. No HD artwork or added tank fixtures are part of the current native-art pass. Do not start FreeRoam or the HD pack merely to complete this task.

## First optimisation pass (2 November 2026)

This pass keeps the accepted native-art appearance. Persistent submission buffers and cached projected bounds avoid repeated allocation and bounds work. The sun rasteriser uses scanline increments; back-facing geometry still casts, but does not need unused receiver samples. Flat terrain builds its regular quarter-tile receiver grid without hashing shared vertices. Slopes and cliffs retain the general path. Four-tile spatial indices shortlist local-light sources and occluders, followed by the same exact voxel obstruction test.

`LightingCache.cpp` stores compressed local models under `Paths::dataDir()/cache/lighting-v1` (or the test-only `ZT_LIGHTING_CACHE_DIR` override). Keys include the actual four-view silhouettes/anchors, footprint, pose, authored geometry profile and reconstruction algorithm version. Shape changes choose a new entry; RGB-only changes can share geometry. Files use bounded decompression, a checksum, structural validation and atomic replacement. Cache eviction periodically enforces a 256 MiB / 2048-file budget over its own named entries. Cached shapes contain locally derived geometry; do not include them in the source repository, release assets or mod uploads. A miss/corrupt entry rebuilds from the player's local resources. This cache currently covers geometry; it does not cache all irradiance or stage all first-enable work.

The tested Release executable is `ff4879a515b0df257728c71a62f8c1d00cf1032feaa7f987222b4170962e9305`. Evidence and graphs: `E:/zt1-shots/day-night-v5-2026-11-02/final/index.html`. 56 lighting checks and 21 shadow/cache checks passed, including native source-change invalidation, persistent geometry reuse and spatial-index results versus exhaustive voxel rays. A deliberately damaged on-disk entry was rejected and rebuilt byte-identically. Eight selected captures were pixel-identical to the earlier build.

On the same Ryzen 3 3300X / RX 6600, 720p populated sunset panning improved from 26.59 to 21.27 ms mean frame time (about 38 to 47 uncapped FPS). The small scene improved from 11.76 to 9.46 ms; the busy scene from 30.38 to 26.64 ms. These compare recorded synthetic-scene runs, not interleaved randomized trials. Shadow caps are unchanged. Populated first-enable time was 393 ms with a cold cache and 295 ms with saved geometry, versus the prior 640 ms; the eight-object cached preview was 49 ms. Retained populated buffer payload increased from approximately 58 to 69 MiB because drawing buffers now stay allocated. Staging cold work, further sun sampling optimisation and local priority selection remain necessary for a crowded 60 FPS target.

`DAY_NIGHT_COVERAGE.csv` now classifies the 676 catalogue entries plus 26 runtime definitions. `DAY_NIGHT_COMPONENTS.csv` separately records child parts and shared runtime rules; see the subsequent audit section for scope and remaining visual checks. Ground/slope/cliff tiles, water surfaces, tank walls/platforms, elevated paths and all four guest types also require dedicated coverage review. `DAY_NIGHT_ANIMAL_EVIDENCE.md` records the initial species evidence and native shock-animation hooks. At the v5 optimisation checkpoint, the authored eight-item preview was the reviewed light-mapping set; the full native mapping implementation below follows that checkpoint.


## Asset classification before further authoring (2 November 2026)

The owner requested an item-by-item light-versus-shadow plan before expanding the authored mappings. `DAY_NIGHT_ASSET_PLAN.md` explains that plan; `DAY_NIGHT_COVERAGE.csv` is the 702-definition ledger and `DAY_NIGHT_COMPONENTS.csv` records native eggs, containers, dust and shared tank/deck/effect/UI rules. The read-only diagnostic `ZT_LIGHTING_BENCH=coverage` exports the current catalogue, metadata, native art views and fence runtime flags. Run it with muted audio and an E: output folder. It changes no lighting profiles, source art or game saves.

The audit assigns 502 definitions to ordinary shadows, 94 to light-bearing designs, 12 to brief actor-effect masks, 35 to special materials, 35 to surface receivers, 21 to species-evidence holds, one buoy fitting to a material-identity hold, and two to exclusions. These are definitions, not simultaneous lights. At that audit checkpoint, the eight existing preview profiles were the only authored mappings. The read-only audit itself enabled no extra emitter; the subsequent implementation is recorded below.

Native evidence includes 13,185 exported views, four-view building/scenery/fence inspection sheets, overview surveys and full camera/shock exports. This is not visual acceptance of every sex/age/animation frame. Exact masks, per-view alignment, closed/off/broken gating, height/clipping and crowded panning remain the acceptance work for implementation batches. Local review gallery: `E:/zt1-shots/day-night-asset-audit-2026-11-02/index.html`. No original or derived art from that folder belongs in the source distribution.


## Full native mapping implementation (2 November 2026)

The 702-definition ledger is now implemented by treatment: ordinary objects/animals/people cast and receive; ground/paths receive; reviewed lamps, building windows, signs, native bulbs, flame/lava and holiday effects have authored emission regions. The 21 biological evidence holds and the unresolved buoy fitting intentionally remain non-emitting. A floating buoy **fence** has ordinary native geometry; it is a different asset from the buoy fitting held for emission evidence.

Native sprite files are unchanged. All new maps are renderer coordinates/material parameters; no new raster art or replacement mesh is distributed. Local four-view silhouette reconstruction and temporary emission textures use the player's own installation. This work belongs to the optional engine graphics feature. A future replacement-art/HD package remains a separate mod. FreeRoam is not part of this change.

New catalogue regions share one cropped atlas per asset/frame/view. Windows, signs and ride bulbs use a steady reference pose, so moving staff and ride parts do not drive their brightness. Native fire-only sources require visible hot pixels. Source-free ponds/fountains/waterfalls receive a subtle water response; tar and ice do not become blue lamps. Steam and jet regions are excluded from solid occlusion without deleting adjacent stone/metal pixels. Tiny bulb strings share bounded local light sources.

The electrical layer follows the currently playing native test_fence/fencezap pose; camera flashes use the native exposure frame. Ordinary idle/walk poses do not emit. White claws/horns are screened against the pre-contact native pose so the dark gaps between electrical flashes do not turn into body lights. At most eight brief effect sources supplement the 96 static lights. No animal AI/contact probability, fence power rule, show animation gate or natural biological-emission behavior was changed.

Native food stages, dung and litter use their existing artwork for small contact silhouettes when a four-view volume cannot form. Native litter has only northern artwork; the existing item drawing path now uses that supplied frame when its requested southern frame is absent. The original art remains intact. Tank toys include their lower native layer at the actual waterline, and tank-floor objects participate in the caster/receiver pass.

Static models build at most two new types per frame, with a three-millisecond admission budget after the first type. One model build itself cannot be interrupted; this is not a hard frame-time guarantee. Animated models retain the separate two-build/256-pose bound. Static local-light samples are cached by each source's nearby geometry and each receiver's relevant sources. Placement preserves distant samples rather than invalidating every light. These in-memory caches add retained payload; process memory, texture memory and frame timing are reported separately.

Merged emission atlases clip against foreground sprite pixels. Their visible spans are cached using relative placement, native art, pose and view, so a foreground object hides the windows it actually covers instead of switching off a whole building atlas. Halo drawing is omitted on clipped fragments to avoid bright borders over the occluder.

Diagnostic commands: `ZT_LIGHTING_BENCH=materials` exports the 113 static native material profiles in four views. `ZT_LIGHTING_BENCH=details` checks structural state changes, exact obstruction rays, local placement reuse versus a fresh complete irradiance rebuild, native flashes and small item silhouettes. `ZT_LIGHTING_CHECKS=1 ZT_SHADOW_CHECKS=1` runs both suites; SHADOW_CHECKS alone does not select that branch. `ZT_LIGHTING_BENCH=audit` reconstructs representative scenery, male/female/young animal bodies, native eggs/crates, staff and guest art. `ZT_LIGHTING_VIDEO=catalogue` with LIGHTING_CHECKS records the 210-second expanded review. Native metadata contains a young tiger submerged-animation reference absent from the stock archives; the representative young-body audit uses its available native land walk. That audit does not certify every declared animation reference as present.

Local evidence, measurements and review video: `E:/zt1-shots/day-night-v6-2026-11-02/index.html`. This folder includes native-art captures and must stay outside source/release packaging. Material sheets inspect four views; pose/state acceptance remains sampled. Automatic nonempty geometry is a reconstruction check, not visual approval of every original sprite frame.


### Measured v6 review build

Tested x64 Release executable SHA-256: `f26f3664a2707c6bc589d5dcd8b29f9cf91cb1d676a904cb1de63148ef5e2179`. The final build passed 56 lighting checks, 21 shadow/cache checks and 43 detail checks, plus the 113-profile four-view material pass. The representative geometry audit has 1,128 nonempty rows out of 1,140; twelve definition-only food items use the runtime food-stage renderer. The 210-second 1280x720 review video contains 5,040 frames at 24 FPS and decodes without error. Its recording closes the options panel before the cycle begins, leaving the scene and native clock unobstructed.

On the Ryzen 3 3300X / RX 6600 at 1280x720, with v-sync off, AI paused, animated poses and a panning camera: the populated fixture averaged 23.83 ms at sunset (42 FPS), and 12.27 ms at night. The busy fixture averaged 28.43 ms at sunset (35 FPS). These are synthetic scene measurements; crowded sunset is still below a steady 60 FPS.

In the placement fixture, the slowest night frame fell from 122.5 ms before neighbourhood reuse to 35.3 ms in the review build. Sunset placement maxima were 208.2 and 108.5 ms respectively. Day/sunset placement still has visible rebuild spikes; the cache reduces them but does not eliminate them. These are successive runs rather than randomized trials.

Populated retained lighting buffer payload: 73.1 MiB; lighting texture payload: 32.8 MiB. These exclude original art, other game systems and driver padding. The report lists process growth separately. First enable with the saved geometry cache took 167 ms; further types/poses warm in later frames. No replacement raster assets are included in the source or build.


## Native beacon follow-up and height-aware shadow sampling

The DRT base now has the native red roof beacon and four blue pad guide lenses in every view. `LightingAviation.cpp` corrects their regions and preserves the native nine-frame lamp sequence. Night fixtures remain powered when the aircraft leaves; the base body and its geometry still use their real occupied/unoccupied state. The same staff frame helpers serve body drawing and lighting, including reversed shutdown playback. No source sprite changes are involved.

Dinosaur Cinema marquee bulbs and Kids Arcade roof-rail bulbs have two separate auxiliary material profiles. They run the native active lamp sequence at night without forcing the building, guests or mascot into an active pose. Steady lettering and interiors are separate from flashing bulbs. The arcade screen follows actual occupancy; cabinet paint and the dinosaur remain unlit. Bulb stencils reject static pale masonry crossing the rail runs.

Eight shared powered-aircraft material profiles add small airframe position markers and a pulsing body beacon for warmup, fly, level and fire. These are optional cosmetic additions, not a claim of recovered native navigation lamps. Tail paint, rotor-tip paint and cockpit reflections are excluded. Parked aircraft and daylight suppress these emitters. They do not allocate camera/shock point-light pools or invalidate static irradiance on every flash. The ordinary light/emission/cache budgets remain in force.

The native inspection exported 11,552 frames locally. Forty-two targeted checks cover all 388 powered aircraft poses across four views, pad playback, altitude/power gating, reverse shutdown, unoccupied fixtures and exact return to the normal rendering when switched off. The broader measured build passed 56 lighting, 43 detail and 25 shadow/cache checks, plus all 113 catalogue material profiles. Twenty hill/actor shadow captures match the prior v6 images pixel for pixel. This is sampled catalogue acceptance, not certification of every original animation in the archives.

`SunDepth` records the highest actor vertex in each coarse projected cell. A receiver above that height cannot receive that actor's shadow and can reuse cached static samples. Lower receivers retain full current-pose sampling; a flying actor still shades a roof below it. Independent depth checks compare skipped receivers with exhaustive sampling. The check-only `ZT_BENCH_FULL_ACTOR_SAMPLING=1` together with `ZT_LIGHTING_BENCH` retains the previous 2D shortlist for a same-executable performance control.

Local video, graphs and raw evidence: `E:/zt1-shots/day-night-v7-beacons-2026-11-02/index.html`. The 80-second, 1280x720, 24 FPS recording contains 1,920 frames and decodes successfully. It shows four camera views and controlled parked/startup/climb/flight/fire/return/descent/shutdown poses, with the midnight clock visible. AI is paused for this lighting review; it does not prove autonomous pursuit/task selection.

### Measured v7 build

Measured x64 Release SHA-256: `bf345641e340e0e2836e290a39b6ff8dcf00322f0a73fac95cec5f5647548c26`. The recording build differs only in the check-mode caption font and reruns the 42 targeted checks; its hash is in the recording folder and summary JSON.

On the Ryzen 3 3300X / RX 6600, OpenGL at 1280x720, v-sync off, AI paused and animated poses: populated panning averaged 16.38 ms by day, 21.33 ms at sunset and 10.95 ms at night (61, 47 and 91 FPS). Busy panning averaged 17.75, 23.14 and 14.59 ms (56, 43 and 69 FPS). Each mode has 360 measured frames after warm-up. The same-build 2D-shortlist control averaged 17.00/21.18 ms for populated day/sunset and 19.24/23.56 ms for busy day/sunset. Daytime shows a modest reduction; sunset differences are small and do not establish a reliable gain. Sequential runs are not randomized trials. Crowded sunset remains below a steady 60 FPS.

Placement maximum frames were 105.5 ms by day, 87.8 ms at sunset and 25.9 ms at night. Input mutation/reindex work occurs immediately before the timed draw; renderer rebuild work is included. Placement and first-enable hitches remain. Populated retained lighting buffers were 71.8 MiB and lighting texture payload 32.8 MiB, excluding original art, other systems and driver padding. First enable with the generated cache was 170 ms, followed by staged warm-up.

No replacement raster assets or meshes are distributed. Functional mapping parameters remain part of the optional engine graphics feature; replacement HD art remains a separate future mod. No commit, upload or release was made for this review.


## Actual changed-block tracking and evidence review

`LightingSun.cpp` now tracks four-pixel blocks only when a current actor wins a depth texel over the sealed static field. The actor bounding rectangle still controls restoration and the height bound remains conservative. `actorNear` checks every block touched by the complete 2x2 PCF footprint, including neighbours at a block boundary. An untouched block reuses the static result; shapes, atlas resolution, shadow bias and receiver geometry are unchanged. Two maximum-size atlases add 128 KiB of tracking bytes. Masks are cleared each actor pass, so old positions do not leave invalidation trails.

The attempted shared-corner cache was removed. Its exact owner/normal/position keys only reused roughly 3-4% of static queries in these meshes and did not reliably improve the busy fixture. Its source and diagnostic measurements remain in the local v8 folder, not in the final runtime.

### v9 validation and measured cost

Tested x64 Release SHA-256: `efe8f57279e65d1a2c97f563bcf8094b967ab3b94c2cb830d33a089be6bdf992`. All 56 lighting, 43 detail, 27 shadow/cache and 42 beacon checks passed, together with 113 catalogue material profiles. The new analytic test probes fractional sample borders, oblique sun directions, different receiver heights, a caster hidden by a static roof and clearing the prior actor mask. Twenty hill/animated-body shadow captures match v7 pixel for pixel. The 80-second four-view lighting reel was regenerated with this same executable and decoded successfully.

The same executable was measured with coarse bounds before and after the changed-block run. The populated sunset surface-projection phase was 8.23 ms in the first control, 6.32 ms with changed blocks and 7.48 ms in the later control. The busy values were 6.52, 5.38 and 5.82 ms respectively. This isolates a useful reduction in that phase, not an equal percentage gain for the whole engine. Off and night timings drifted between the sequential controls, so no fixed overall FPS improvement is claimed.

At 1280x720 on the Ryzen 3 3300X / RX 6600, v-sync off, AI paused with animated poses and a panning camera: populated mean day/sunset/night was 16.65/20.84/11.38 ms (60/48/88 FPS); busy was 18.51/24.33/14.88 ms (54/41/67 FPS). There are 360 measured frames per mode. A separate populated run with AI/needs active completed both passes while panning, averaging 17.11/21.23/11.20 ms (58/47/89 FPS). Crowded sunset remains below a steady 60 FPS. This short fixture run does not replace profiling large player saves.

Placement maxima were 152.2 ms by day, 72.5 ms at sunset and 28.7 ms at night; the remaining rebuild spikes are not hidden by the mean. Populated retained lighting buffers were 71.9 MiB, textures 32.8 MiB, and first enable with the saved geometry cache was 130 ms. These measurements exclude driver padding and separate the feature's retained payload from process-wide native-resource loading.

Local current report, graphs, video, raw frames and control runs: `E:/zt1-shots/day-night-v9-fine-shadows-2026-11-02/index.html`. The report includes the 23-entry emission decision review. See `DAY_NIGHT_EMISSION_DECISIONS.csv` and `DAY_NIGHT_ANIMAL_EVIDENCE.md` for native identity checks, primary sources, unresolved species/fixture evidence and why no new natural emitter was enabled. No replacement art, animal husbandry rule or save format changed. No commit, upload or release was made.


## Projecting fixtures and excited materials

The lighthouse now has a soft rotating cone from its native lantern height. Its 10-second sweep, 24-tile range, exposure and cone angle are optional graphical design parameters. A 128-direction static obstruction envelope is cached per source and scene. The cone rotates over cached ground/face irradiance; rotating its direction does not rebuild static rays. The optional atmospheric haze stops at the cached terrain/solid boundary. Four projector slots share the normal 96-light budget. Static terrain and reconstructed solid objects obstruct the beam. Moving actors receive directional illumination, but do not rebuild this static local-light obstruction envelope.

Mapped catalogue building window/sign centroids locate sources on outward-facing reconstructed facade surfaces. Existing individually authored sources retain their fixture positions and colours. Windows illuminate outward instead of radiating from a generic building centre. Repeated fixture/mesh combinations reuse facade lookup results.

Green moray tissue has excitation-dependent green fluorescence. Sea Anemone and Divercate Tree Coral have explicit fluorescent reference materials because the native common labels do not establish exact species. These are RGB graphical approximations, not measured spectra or autonomous bioluminescence. Incoming blue-weighted light, source direction and obstruction gate the response; red light and darkness do not energize it. Coral skips its own solid when testing the incoming ray. Native body alpha/pigment and retained shadow exclusions constrain the material. The animal response is drawn inside the existing native tank-clipping pass, not over the tank afterwards. Up to 64 visible cues are retained, with an artwork lookup to skip unrelated sprites. See DAY_NIGHT_ANIMAL_EVIDENCE.md and the decision CSV for primary sources and species limits.

### v10 verification and cost

Tested x64 Release SHA-256: `4b1072e2a445708ef6e812d3664a43d6ea08c9c5ee43a11baab476e5421a8698`. All 16 projection/fluorescence, 42 beacon, 43 detail, 56 lighting and 27 shadow/cache checks passed, plus 113 catalogue material profiles. Exact and accelerated tank clipping agree pixel for pixel in four views with fluorescent marine bodies. Disabling lighting restores exact original pixels. Twenty hill/animated-shadow captures match the prior accepted build. The 48-second 1280x720 video contains 1,152 frames at 24 FPS and decodes without errors. Beam/pose times are driven for this paused lighting fixture; the midnight clock is a preview, not an autonomous task test.

At 1280x720 on the Ryzen 3 3300X / RX 6600, OpenGL and v-sync off, camera panning with animated poses: populated day/sunset/night averaged 18.27/22.89/13.27 ms (55/44/75 FPS); busy averaged 21.30/26.50/16.96 ms (47/38/59 FPS). Each mode has 360 measured frames. A populated live-simulation run averaged 18.05/21.77/13.03 ms (55/46/77 FPS). Crowded sunset remains below 60 FPS. These sequential fixture measurements do not establish a fixed improvement over older builds; the populated fixture now includes 90 lights rather than 75 due to outward window groups.

The separate lighthouse/tank fixture averaged 9.52 ms with lighting off and 10.37 ms with the night effects, over 120 frames per mode. This measures the whole optional lighting effect in that scene, not isolated beam-shader time. Placement maxima were 130.1/70.1/32.9 ms by day/sunset/night, so rebuild spikes remain. Populated retained lighting buffers were 74.1 MiB and lighting textures 32.8 MiB, excluding native art, unrelated systems and driver padding. Initial activation took 153 ms, followed by staged warm-up; the fresh per-fixture disk cache recorded native geometry misses rather than a warm-cache startup.

Local current video, graph, four-view comparisons, raw frame samples and check logs: `E:/zt1-shots/day-night-v10-projection-2026-11-02/index.html`. Source sprites, animal needs and save formats remain unchanged. No replacement art is distributed. No commit, push or release was made.


## Building light and shadow polish

The moving lighthouse beam exposed constant per-face illumination, discontinuous roof shading and receiver models being skipped entirely by local-light obstruction tests. Projectors now retain four corner energies per receiving face and apply their current angular direction at each vertex. Raster interpolation carries the beam across the face. Steady fixtures keep one cached RGB value per face: extending all of their rays to four corners caused a 473 ms placement frame and was removed. The corrected placement run is reported below rather than hiding that regression.

Local lighting offsets an owned receiver slightly along its normal rather than ignoring its whole solid. Roofs and overhangs can therefore obstruct another part of the same receiving building. Ground samples retain their actual height so the bias does not detach contact shadows. The originating fixture's own volume remains excluded. Projectors use four small aperture samples for obstruction softness and a 48-cell ground grid rather than the ordinary 20-cell grid. These are cached samples, not a full-scene blur; rotating the source still reuses them. The four-projector and 96-total-light bounds remain.

Reconstructed lighting normals use a continuous Gaussian density gradient at the actual face position instead of integer-cell box differences. Solid cells, face coordinates and outward winding do not change. Cache algorithm key 2 prevents reuse of old shading normals while retaining the existing validated local cache format and pruning limits. An independent cache comparison found 374 sampled native model volumes and surface coordinates identical before and after. No art, sprite, source texture or distributed replacement mesh was added.

### v11 validation and cost

Tested x64 Release SHA-256: `6ccd224b69c5437e96aa74a70293b7668bffa64a52fdf255a3c99733e9792a17`. 190 checks passed: 22 projection/receiver/cache, 42 beacon, 43 detail, 56 lighting and 27 shadow/cache, plus 113 catalogue material profiles. The added checks prove receiver self-obstruction, a clear facade without self-acne, variable projector corner samples, the 48-cell grid and source-array restoration after inserting/removing five lighthouses under the four-source cap. Four-view exact marine clipping and exact feature-off restoration still pass. Existing sun shadow tests remain green; the source geometry is unchanged, while the refined lighting normals intentionally change shading.

At 1280x720 on the Ryzen 3 3300X / RX 6600, OpenGL and v-sync off with a panning camera: populated day/sunset/night averaged 15.88/19.74/11.30 ms (63/51/89 FPS); busy averaged 17.78/22.30/14.41 ms (56/45/69 FPS). Each mode contains 360 measured frames. Populated with AI/needs active averaged 17.74/21.60/11.72 ms (56/46/85 FPS). These sequential synthetic runs are not randomized trials, a claimed fixed optimization or certification of a large player save. Crowded sunset remains below a steady 60 FPS.

Placement maxima were 162.1/80.7/34.5 ms by day/sunset/night. The shader/renderer rebuild is timed, while mutation/reindex occurs immediately beforehand. Rebuild spikes remain. Populated retained lighting buffers were 72.7 MiB and lighting texture payload 32.8 MiB, excluding native art, unrelated systems and driver padding. Initial activation was 134 ms with 119 model-cache hits and 0 misses across the run; do not compare this startup directly to the earlier cold-cache measurement.

The 48-second 1280x720 four-view recording contains 1,152 frames at 24 FPS and decodes cleanly. The fixture is paused with controlled beam/pose times; the midnight clock is a preview. Local before/revised building crops, video, timings, graph, hashes and checks: `E:/zt1-shots/day-night-v11-surfaces-2026-11-02/index.html`.

Some fine silhouette stepping and original painted shading remain in the four-view visual-hull approximation. This pass reduces illumination banding and missing self-obstruction; it is not perfect per-pixel geometry for every native building or every animation pose. No gameplay, save format or source art changed. No commit, push or release was made.


## Receiver visibility: buried tanks and building outlines - 3 November 2026

The reported faults were receiving-surface visibility leaks, not primarily shadow softness: buried tank frames were being drawn over foreground terrain, and the reconstructed building hull was painting an enlarged silhouette over the native artwork. The prior normal/corner interpolation work did not solve these faults. Previous energy/cache checks also did not verify those visible pixels.

`LightingVisibility.cpp` now evaluates receiving vertices against the orthographic camera ray through terrain. `LightingScene.cpp` clips receiving polygons at that visibility boundary. Above-ground clearance uses the same height-distance scale as buried clearance; a constant positive sentinel incorrectly left a short underground strip. Tests now include 0.5, 1 and 2 units below the rail in all four views. Buried structures remain in physical light/shadow geometry; only their hidden receiver paint is excluded.

Placed-object and actor receiving surfaces also use cached coverage from the current native art, pose, anchor and mirror. Retained native shadow runs are excluded. Alpha-masked geometry cannot draw the reconstructed hull as an extra outline outside its sprite. No replacement art is generated for distribution. Native alpha masks are runtime-only GPU data.

Four logical 2048-pixel pages share one 4096-pixel GPU texture where supported, preserving the global surface order without repeated texture switches. A bounded separate-page fallback supports smaller renderers. Unused logical pages can be reused; maximum coverage texture payload is 64 MiB. Fully visible faces already inside the native canvas skip polygon clipping. A same-executable forced-complete path verifies identical pixels in eight four-view captures. Native bodies still receive light; the fix is not omission of the surface pass.

### v12 verification

Tested x64 Release SHA-256: `c00613647d4542892b967aa1818bcc39f84147d6e019106899fa77d7a09a3c80`. All 196 checks passed: 28 projection/visibility, 42 beacon, 43 detail, 56 lighting and 27 shadow/cache, plus 113 catalogue material profiles. Native four-view tank clipping and exact feature-off restoration still pass.

The before-render altered 2754 of 25297 protected pixels surrounding the building. The corrected render alters 0. Another 32,549 native building pixels still change when receivers are enabled. All 40 accepted buried-post sample patches leave visible grass unchanged; exposed inner receivers remain visible. The report includes the actual before/after crops at the user's beam positions and raw probe coordinates/results. Pairing uses a direct check-only receiver toggle, avoiding Windows process-environment/CRT-environment mismatches during in-process captures.

At 1280x720 on the Ryzen 3 3300X / RX 6600, OpenGL, v-sync off, panning camera: populated day/sunset/night averaged 20.55/24.41/14.57 ms (49/41/69 FPS); busy averaged 25.04/29.72/20.79 ms (40/34/48 FPS). Each mode has 360 frames. The populated live-AI fixture averaged 22.70/27.70/17.08 ms (44/36/59 FPS). These sequential runs contain background-load variation and are not an isolated incremental cost measurement. Crowded scenes remain below a steady 60 FPS.

Placement maxima were 209.1/107.2/49.3 ms by day/sunset/night. Mutation/reindex occurs immediately before the measured draw. Rebuild hitches remain visible in the data. Populated retained lighting buffers were 73.8 MiB and total lighting texture payload, including coverage, 96.8 MiB. These exclude other game systems, native source art and driver padding.

Current local comparison, 48-second four-view video, graph, binary hash and raw checks: `E:/zt1-shots/day-night-v12-visibility-2026-11-02/index.html`. The 1280x720 recording contains 1,152 frames at 24 FPS and decodes without error. It is a paused controlled lighting fixture, not an autonomous AI-task recording. Representative checks do not certify every native animation or every zoo. No source art, gameplay or save format changed; no commit, push or release was made.


## Additional directional fixtures - 3 November 2026

The native candidate audit reviewed 112 lighting/vehicle items in four overview sheets, enlarged four-view details for uncertain fittings, and resource names/definitions across 172 installed archives. The street light is the additional confirmed directional family. Its source position (1.793633, 0.010352, 6.386406) is a least-squares fit of the four native lamp-head centroids, with less than one native pixel error in every view. A downward cone replaces its previous diffuse source. Power, warm colour, range and aperture are optional night-design parameters, not native photometry. A clear-air lamp has a downward illuminated pool; no lighthouse-style atmospheric shaft was added.

Fixed cones retain cached angular falloff and obstruction, use a 32-cell ground grid and the existing steady per-face RGB cache. Only cones with a nonzero sweep consume the four moving-projector slots, aperture rays and per-corner irradiance. Fixed and moving lights still share the total 96-source budget. The six-streetlight fixture confirms that fixed cones coexist beyond four sources, time changes reuse static irradiance and angular falloff is applied exactly once. The common terrain and native-art receiver clipping remains active.

Enlarged native inspection corrected an earlier error: the Animatronic Theater's purple-rimmed perimeter objects are plant pots with foliage. Their four erroneous Glass regions and mapped floor source are removed at definition preparation; facade windows remain. Parked vehicle headlight lenses did not establish a powered state, so no speculative beam was added. Ordinary lanterns, signal masks and decorative glow retain separate treatment. See DAY_NIGHT_PROJECTORS.csv for the audit decisions. The resource-name search does not certify every animation frame or mod.

Tested Release SHA-256: `d4e6ddb1eb0f5deea1b3fd9f702205b2ee25c218e5e35af633af3d52f80535f9`. All 205 checks passed (9 fixtures, 28 projection/visibility, 42 beacons, 43 details, 56 lighting, 27 shadows/cache), plus 113 catalogue material profiles. Four night captures were inspected: the sunken tank has no buried-frame x-ray and the theater has no extra receiver outline. Existing exact off-state and marine clipping comparisons pass. The 48-second four-view native video has 1,152 frames at 24 FPS and decodes successfully. It is a paused graphical fixture with the midnight preview clock, not an autonomous AI test.

At 1280x720, OpenGL, v-sync off on the Ryzen 3 3300X / RX 6600, panning populated day/sunset/night averaged 19.83/24.60/13.98 ms; busy averaged 21.79/26.30/17.27 ms. Each mode contains 360 frames. These sequential synthetic measurements cover the whole lighting system, not isolated streetlight overhead or a fixed improvement over earlier runs. Crowded sunset remains below 60 FPS.

Local evidence, graph, video, candidate sheets, archive search and raw results: `E:/zt1-shots/day-night-v13-projectors-2026-11-03/index.html`. No source art, gameplay or save format changed. No commit, push or release was made.
