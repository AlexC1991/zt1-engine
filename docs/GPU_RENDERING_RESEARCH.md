# GPU rendering research - 5 November 2026

The next useful GPU work is the shadow-receiver and lighting calculation path.
The engine already uses accelerated OpenGL to draw sprites, composite tanks and
run FSR. Much of the lighting data sent to that renderer is still calculated
on the CPU. This document records the original profiling and proposed stages.

The concrete pass design, source-function migration, resource budget and
completion gates are in [GPU lighting implementation map](GPU_LIGHTING_IMPLEMENTATION_MAP.md).
That map also records the experimental GPU passes and initial comparison
results. The CPU-atlas bridge described below is an intermediate stage.

## Hardware verification

An isolated native OpenGL probe on the owner's PC reported the AMD Radeon
RX 6600 and OpenGL 4.6 compatibility context, matching the earlier engine log.
A GLSL 4.30 compute shader wrote the expected value into a storage buffer,
with no GL error. A pair of 64-bit GPU timestamp queries returned successfully.
The probe detected framebuffer, multiple-output, integer-clear, instancing,
persistent-buffer, synchronization and compute entry points. The maximum
texture dimension reported was 16,384.

These checks establish local API availability. They do not measure the speed
of a game lighting pass or establish support on other players' machines.
The tiny probe's GPU duration is a capability receipt only.

Probe source, build and results are in
`E:/zt1-shots/gpu-research-2026-11-05/`. The result is
`gpu-capabilities.txt` and the executable exits after the check.

## What the existing profile tells us

The post-tank-cache 240-frame daytime sample measured approximately 70.14 ms
per frame. Selected scopes are below. These are elapsed CPU-side scopes,
including submission and any driver waiting. They are not hardware GPU times.
Nested rows overlap and must not be added together.

| Scope | Mean ms | Existing work |
| --- | ---: | --- |
| World draw | 67.61 | Enclosing render scope |
| Simulation | 0.93 | Existing game update |
| Lighting shadows | 16.79 | Includes preparation, actors, sun maps and outside-ground shadows |
| Lighting actors | 3.51 | Pose models and transformed receiver geometry |
| Lighting sun | 2.74 | CPU shadow-atlas rasterization and samples |
| Lighting ground | 7.56 | Outside-ground receiver sampling and triangle construction |
| Surface project | 5.46 | Surface light/shadow colours, clipping and vertex construction |
| Fence collect | 10.29 | Tank strips and ordered draw preparation |
| Tank contents | 10.90 | Includes tank-floor shadows, objects, animals and staff |

The ground, surface-project and sun scopes cover about 15.8 ms of elapsed
work, with additional floor-shadow work inside tank contents not separated
by this profile. They are useful candidates to measure and migrate. This
number is a work budget, not a promised saving. GPU costs, transfers and
synchronization have to be measured as well.

Receipt
`E:/zt1-shots/sustained-game-benchmark-2026-11-04/day-profile-candidate.log`.
The later full-cycle runs are under `tank-cache-fix/` in the same parent.

## Implementation order

### 1. Measure GPU time and resource churn

Add optional asynchronous GPU timestamp markers around the actual production
frame. Start with a whole-frame pair, then diagnostic markers for tanks,
shadow maps, receivers and lighting composition. Read available results a few
frames later from a bounded query ring. Leave unavailable results pending.
Record query support, shader support, target allocations and cache fallback
reasons alongside CPU elapsed scopes and present intervals.

SDL queues drawing commands. Markers must account for that queue, and the
direct-GL boundary must flush submitted SDL work and restore the GL state SDL
expects. Detailed pass flushes can change batching, so measure instrumentation
overhead and keep sustained comparison runs separate from diagnostic captures.
Existing `GpuFsr::artPass` provides a local example of the interop boundary.
[SDL interop documentation](https://wiki.libsdl.org/SDL2/SDL_RenderFlush),
[Khronos timer queries](https://wikis.khronos.org/opengl/Query_Object).

### 2. Move receiver lighting into shaders

Keep the current CPU-built sun atlases initially. Upload depth, owner IDs and
the actor-change information, retaining them on the GPU between changes.
Transfer changed atlas regions through bounded staging storage. Include
transfer and synchronization costs when comparing this stage with the CPU path.
Upload ground and static receiver meshes once. Let the GPU calculate current
sun interpolation, shadow samples, surface colour and beam direction.
Update actor transforms and pose references rather than rebuilding every
vertex position and colour on the CPU.

Target `LightingShadows.cpp::drawShadows`,
`LightingScene.cpp::drawSurfaceLighting` and the transformed receiver data in
`LightingActors.cpp`. Build this as a GL 3.3-capable rendering path inside the
existing day/night setting, with capability checks and the CPU reference path.

The existing look requires deliberate preservation. `SunDepth::shadow` uses
four weighted taps, a slope-sensitive bias, a soft depth ramp and receiver
owner exclusion. Built-in depth comparison filtering has different semantics.
The first shader should reproduce the current formula. Evaluate at the same
receiver vertices, including current byte-colour truncation and interpolation.
A switch to per-pixel lighting would change the appearance and belongs in a
separate visual comparison. [Khronos depth sampling](https://wikis.khronos.org/opengl/Sampler_Object).

Keep current tank-floor regions, terrain clearance, sprite masks and drawing
order. Retain the CPU path for clipped faces until their interpolation can be
reproduced. A whole-screen darkening pass would miss the existing receiver
rules that prevent buried tank walls and building outlines showing through.

### 3. Generate sun atlases on the GPU

After shader receivers pass comparisons, replace `SunDepth::triangle` and
the CPU loops in `DayNightLighting::updateSun` with offscreen GPU passes over
the existing reconstructed meshes. Preserve the paired sun directions and
static/dynamic separation. Cache static geometry and update moving actors.
Use a depth/height representation and integer owner attachment that preserve
the reference comparison. Validate coverage at triangle edges and equal-depth
ownership explicitly. [Khronos framebuffers](https://wikis.khronos.org/opengl/Framebuffer).

This raster path uses meshes the engine already has. Compute shaders are a
later option for large visibility or light-grid jobs when measured workload
justifies them. Dispatching work also requires the correct visibility barriers
for the following consumer. The capability probe demonstrated compute support
on the local card, not a game-performance benefit.

### 4. Retain and stream GPU resources

`Fences::collect` currently creates frame-local `GpuTankLayer` objects. Each
allocates a target on first use and destroys it when its references expire.
Measure these allocations, then prototype a renderer-owned pool under the
existing memory budget. Reuse target storage while clearing and repainting
animated contents each frame. Handle resize, renderer shutdown and device
reset explicitly.

Keep static mesh buffers resident. Stream changing actor data through several
buffer regions, reusing a region after its fence completes. Persistent mapping
is an optional GL 4.4/extension path. Older supported paths can use buffer
orphaning or multiple buffers. This reduces avoidable synchronization while
retaining correct data ownership. [Khronos buffer streaming](https://wikis.khronos.org/opengl/Buffer_Object_Streaming).

### 5. Batch compatible sprite and fence work

Cache static fence/tank preparation by topology, terrain and camera orientation.
Use GPU transforms and instanced draws for compatible consecutive commands.
Texture atlases or arrays can reduce changes between textures, subject to
palette, tint, animation and HD-art requirements.

Keep the existing painter order and tank depth sections. Transparent water,
animals, gates and platforms cannot be globally reordered by texture. The
measured fence-collection scope includes CPU preparation that requires its own
cache improvements, so increasing GPU draw throughput alone will not remove
that scope. SDL already accepts triangle lists, while persistent mesh and
custom instancing support needs a direct rendering component.
[SDL geometry interface](https://wiki.libsdl.org/SDL2/SDL_RenderGeometryRaw).

## Renderer and tooling choices

The current GL compatibility context can host a separately capability-checked
modern shader component. Keep its state boundary explicit and let the existing
renderer handle unsupported devices. The local hardware probe supports trying
this approach first.

SDL3's GPU API provides Vulkan, Direct3D 12 and Metal backends. It is a possible
longer-term renderer project involving shader formats, resource ownership and
integration work. Merely selecting another API does not move the current CPU
lighting loops. [SDL GPU API](https://wiki.libsdl.org/SDL3/CategoryGPU).

AMD's Radeon GPU Profiler supports explicit graphics APIs and selected compute
APIs, and its manual excludes OpenGL. Use in-engine GL timestamps for the
present renderer. [AMD profiler manual](https://gpuopen.com/manuals/rgp_manual/).

In the measured 1x sample simulation took under one millisecond, while drawing
took roughly 68 ms. That evidence prioritizes the rendering stages above.
Reconsider simulation offload only against a workload where update costs
dominate and the transfer/readback costs are included.

## Acceptance for each stage

- Compare against the CPU renderer with the existing image checks, expanded
  to cover owner IDs, receiver clipping, sunrise/sunset, tank corners, pumps,
  platforms, elevated paths, animals in motion and HD artwork.
- Exercise four rotations, half/normal/zoomed-in views, resizing, feature
  toggles and unsupported-capability fallback. Day/night off must restore the
  original daylight output.
- Record CPU and GPU timings separately, target allocations, uploaded bytes,
  resident memory, average FPS, 1% lows and achieved 1x/2x/5x simulation speed.
- Use the same initial zoo and camera route. Include stationary and panning
  sections, cold activation and warm steady-state samples. Keep the evolving
  census in the report.
- Repeat the relevant sustained full-cycle tests after image comparisons pass.
  Accept a stage for measured frame-time improvement and preserved appearance.
