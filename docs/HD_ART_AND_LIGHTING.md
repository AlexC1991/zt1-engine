# HD art with day/night lighting

The Lab's HD pipeline produces 4x two-dimensional animation frames. It does not generate new 3D meshes, new animation poses or a PBR material set. The existing reconstructed geometry continues to drive shadows and light obstruction.

## What the mod contains

- Colour frames in `hd/<native animation folder>/<direction>_<frame>.png`. The original outline is restored after upscaling, and back-projection retains native texture detail.
- Separate emission layers in `hd/_lighting/<profile fingerprint>/<profile>/<view>/<native animation folder>/<direction>_<frame>.png`.
- `mod.json` and `lighting.json`, with generation status and layer layout.

The emission layers use the same reviewed material regions, pixel masks, native frame gates and mirrored views as normal day/night lighting. Colour detail is borrowed from the finished HD frame inside those approved regions; bright colour elsewhere never discovers a new lamp. Static windows keep their reference pose while animated fixtures use their own frames. Sunlight, nearby-light illumination, cast shadows and fluorescence activation are not baked into the colour image. They stay dynamic.

At zoomed-in scales, the renderer loads the matching baked layer from the same enabled pack as the colour frame. It verifies integer dimensions and crops transparent canvas from the GPU texture while preserving native placement coordinates. The material fingerprint changes when its authored region, tint or mask settings change. Missing or mismatched layers fall back to the existing native material pass. Zooming out uses native materials. The pack stays an ordinary optional mod; generation does not enable it or change the user's normal mod selection.

## Sharing it: one mod per game, packed

`re/lab/hd_split.py` sorts the finished pack by game: Zoo Tycoon, Marine Mania and Dinosaur Digs. An art folder goes with its item's `[Member]` tag (`dinosaur`, `aqua`), as the content filter reads it, or else with the game whose archives first had it. `re/lab/hd_pack.py` then builds one mod per game. It packs each art folder into `hd/<top>/<name>.ztd` (stored, since the PNGs are compressed already), with the frames and that art's current light maps, and leaves out light maps an older build made. It lists the archives as `"parts"` of at most 3.5 GB. The engine reads packed folders as it reads loose files (`HdArt`). The Lab uploads each part as its own download.

The mod's description says why it carries the AI tag: the frames are the game's own art enlarged by Real-ESRGAN, an open-source upscaler run locally, not made by an image generator.

## In the game: loading and memory

The engine treats the pack as something to stream, not to hold. HD frames are used only while the map is zoomed in, one direction of one animation at a time.

- **Background loading** (`HdArt::requestFrames`): a direction's frames are read and decoded on worker threads, most recently wanted first. The original frames are drawn until the HD frames arrive, so zooming in never stops the game.
- **Two levels:** up to about 2.1x zoom, frames are halved from the pack's 4x (a quarter of the memory). From 2.4x, the full 4x frames are used. Between those zooms, the level stays as it was.
- **A budget:** zoomed-in textures are kept up to 1.5 GB. Beyond that, those not drawn for longest are freed (`Animation::trimZoomedIn`).
- **Compressed on the GPU** (`GpuCompressed`, `Bc7`): each HD frame is encoded to BC7 as its texture is made, one byte a pixel instead of four. The GPU reads BC7 as it draws. SDL 2 can't make compressed textures, so SDL makes an ordinary one and its storage is swapped: on OpenGL through `SDL_GL_BindTexture`, on Direct3D 11 through SDL's own texture record, checked against the texture first. If any check fails, the ordinary texture is kept. Frames whose size isn't a multiple of 4 are padded with their edge pixels; `ArtScaler::contentRect` keeps draws to the art itself. How is the player's choice: every HD art mod that is switched on gets a **Graphics card memory** setting on its Settings page (Game Options > Mods & Updates), added by the game, not by the mod (`Mods::kHdCompression`, kept in zoo.ini `[Mod:<folder>]`). It offers Full size, Compressed (fast, the default: mode 6 only) and Compressed (best quality). Changing it on one HD art mod changes it on all of them, and the zoomed-in pictures are remade at once. With best quality, the encoder tries each block in the modes that suit it and keeps the one that decodes closest; a block that mode 6 already gets within about 2 levels a channel is left at that. Solid blocks use mode 6 (one line of colours), or modes 1 and 3 (two groups of pixels on lines of their own, which suits busy colour such as leaves). Blocks with soft or clear pixels use mode 6, mode 5 (colour and alpha on separate steps) or mode 7 (two groups with alpha). For the two-group modes, the two most promising of the 64 patterns are tried. Against the uncompressed frames in the lit scene this measures about 52 dB, with 0.15% of pixels more than 8 levels off; mode 6 alone gave 44 dB and 1.1%. The game's own art is never compressed.
- **Source frames packed** (`Animation::packAll`): the original frames are kept packed and unpacked when needed, up to 512 MB.

The panel walk-through checks the encoder, that the GPU draws a compressed texture exactly as it decodes, and that random blocks of every mode written (the two-group ones in all 64 patterns) decode on the GPU exactly as `Bc7::decodeBlock` decodes them ("compressed HD textures"). A wrong pattern or anchor table cannot pass.

## Run and resume

`re/lab/hd_batch.py` is the resumable worker. The local `hd_art_batch` Lab script launches it using the Lab toolkit and reports completed batches in Test Zoo. The older `hd_art` single-prefix example remains available.

The worker uses a fixed generation executable, private resource/mod settings, a bounded frame chunk, a Vulkan Real-ESRGAN upscaler and four CPU finishing workers. Its inventory is resolved by the engine in native resource order with normal user mods switched off in that private session. It includes non-UI animation resources; UI layouts, sound and entirely new model detail are outside this pipeline.

Progress is also appended to `worker.log` in the run's work folder. A closed
or disconnected Lab console no longer turns a saved checkpoint into a failed
job merely because printing progress raises a broken-pipe/invalid-handle error.

A chunk is exported, upscaled, checked for exact 4x dimensions, finalized, given its material layers, then checkpointed before its scratch files are removed. Completed filenames are skipped on resume. Failure keeps unfinished scratch. A lock prevents a second worker from writing the same run. Child jobs are tied to the worker through a Windows job object. The batch stops before another chunk if scratch space falls below 12 GiB or system free space below 4 GiB. It does not upload or publish anything.

To pause after the active chunk, create `STOP` in the run's work folder. Remove it and run the same Lab script to resume. Do not remove a lock while its recorded process is still running. A forced termination can leave a stale lock; verify the recorded PID before removing it. The Lab and PC must remain running for the overnight job.

## Update only the lighting maps

Run the worker with `--materials-only`, a fresh work folder, the existing pack as `--output`, and an executable containing the revised mappings. This exports native material seeds, pairs them with the existing finished colour frames and regenerates the separate layers. It never invokes the upscaler. Old fingerprinted layers are ignored by the updated renderer. This mode was exercised on the native four-view streetlight sample.

## Local run, 3 November 2026

- Lab script: `re/lab/scripts/hd_art_batch.cpp`.
- Batch state: `E:/zt1-shots/hd-lighting-batch-2026-11-03/full-work/status.json`.
- Pack: `E:/zt1-hd-packs/ZT1-HD-Lighting-Preview`.
- Mod-library junction: `B:/zt1-mods/ZT1-HD-Lighting-Preview`; it remains disabled.
- Current generation tool snapshot: `E:/zt1-shots/hd-lighting-batch-2026-11-03/tool-v2/zt1-engine.exe`.
- Queue: 1,108 resolved animation groups. Ten to eleven hours is a planning estimate, not a measured full-library completion time.

The streetlight sample completed the real export/upscale/finish/layer pipeline, then passed five renderer checks: the private mod is present, all four mapped views load the baked layer, texture resolution and placement match, zooming out restores native patches, and lighting off restores the same HD daylight pixels. Six worker checks cover material footprint, transparency, approved emission, invalid PNG rejection and regeneration without the upscaler. Full-queue generation subsequently completed as recorded below; these sample renderer checks do not certify every generated frame visually.

Generated colour frames, raster masks and original-art comparisons stay outside this source repository. They are derived from the owner's installed game. No original art or generated pack is part of the MIT source or the engine release.


## Integration review and resource cleanup - 3 November 2026

An isolated copy of the five completed fixture groups is used for destructive graphics probes; the live generation pack is never modified by those tests. The review covers streetlight and lighthouse idle art, restaurant idle/used art, carousel idle/used art and helipad idle/used art in all four native views. Every available pose in those eight sequences is checked for active/inactive material gating, 4x texture dimensions and native placement bounds. First/middle/last alpha samples compare the baked layer with the native approved material intersected with the finished HD frame's alpha.

The review also temporarily withholds and replaces a map with a wrong-sized image, restores it, switches the optional pack off/on in a private mod configuration, checks exact rendered restoration, resets palette references, and captures the restaurant, carousel and real helicopter team's base in four views. These are controlled paused graphical states; they do not certify autonomous staff jobs or the unfinished full pack.

The first broader run passed its image checks but exited with access violation 0xC0000005 after returning. The HD recolouring cache retained unique ownership of a registered animation while ResourceManager cleanup also deleted it. The palette-reference cache is now released before resource inventory cleanup, while the resource manager and renderer still exist; the resource pointer is cleared on destruction. This covers both shutdown and the shared inventory-reset path. The same review then exited cleanly. The final rerun receipts and regressions are in the local review report.

The Lab monitor can attach to the existing worker through its recorded process handle without starting a second batch. It shows verified frames, percentage of the measured 1,030,934-frame inventory, emission layers, completed chunks, current upscale progress and elapsed hours. Reloading the monitor leaves the generation process and active GPU chunk running. Source/reference taxonomy uncertainty in DAY_NIGHT_EMISSION_DECISIONS.csv remains explicit; unsupported emitters are not added to complete a checklist.

Evidence: `E:/zt1-shots/hd-lighting-review-2026-11-03/index.html`. Do not use these concurrent-generation runs as FPS benchmarks. Full-pack visual acceptance and performance measurements remain pending generation completion.

Final integration build: `24f724d557e5af20233bcf540113d095dcf4e96be75cea701a8e4a2e0fc31266`. Both full HD reviews exited 0, with 43 checks and 300 pose/view combinations each. All 205 existing lighting regressions and 113 catalogue material profiles passed on that same executable. The live batch remains on its fixed earlier generator; it does not load optional HD replacements, so the recolouring shutdown fix does not require interrupting generation.

## Windows progress-file recovery - 4 November 2026

The full batch stopped at 939,187 completed frames when Windows temporarily
refused the atomic replacement of `status.json` with `status.json.tmp`.
`atomic_json` now retries PermissionError for up to three seconds, retaining
the previous valid progress file between attempts. Persistent errors still
stop the job; they are not reported as a successful checkpoint.

A native Win32 file-lock probe held the destination open without delete
sharing, released it after 200 ms, and confirmed that the retry wrote valid
new JSON. The batch resumed through the existing Lab `hd_art_batch` script,
using its unchanged fixed generation executable and completed checkpoints.
It subsequently passed 952,857 verified frames and progressed into guest
art. This is a recovery receipt, not full-pack completion. Live status stays
in the work folder above. Evidence for the lock probe is
`E:/zt1-shots/fast-forward-gpu-2026-11-04/locked-status-check.json`.

## Generation completed - 4 November 2026

The durable batch status reports `state: complete`, 1,030,934 verified colour
frames, 8,517 material maps and 1,525 completed batches. The worker log ends
with the same counts and a completion message, and the Lab's completion check
passed. Its float-valued watch rounds the frame count; the JSON checkpoint is
the authoritative integer count.

The pack remains a disabled local preview at the path above. Completion means
the export/upscale/finish queue and its frame checks finished, not that every
asset has had a visual review or that the pack is ready to distribute.
