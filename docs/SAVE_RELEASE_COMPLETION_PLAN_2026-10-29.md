# Save and release completion plan - 29 October 2026

Status: execution authorized by the owner; tests and progress are recorded in
[the execution log](SAVE_RELEASE_EXECUTION_2026-10-29.md). Final publication
still requires the owner's acceptance of the tested packaged candidate.

The shared-save core is implemented. The initial evidence was 1,421/1,421 save
checks across six fixtures, plus the activity and marine show suites. The local
Lab/Play executables match; the release executable and packages are older.
Passing those checks does not establish every missing-mod, failure, active-job
or upgrade case below.

Work in the order below. All game controls, C++ edits, builds and original-game
investigation stay in the Lab MCP. Preserve the current performance work, source
saves, mod selections and private research. Use copies and record actual results.

## 1. Protect saves and the currently open zoo

Priority: release blocker.

The native loader currently admits an animal/staff record only when its type
resolves, and admits a placed object only when its art resolves. This creates a
potential silent-loss path with disabled, missing or changed mod definitions.
The in-game mod switch protects content already in a running zoo; it does not
by itself demonstrate safe loading of another modded save.

The normal Load path also starts the saved map and replaces the screen before
the full native state has finished loading. Failures after that point need to
leave the current zoo intact.

Work:

- Reproduce missing/disabled/renamed definitions for animals, staff, objects,
  fences, paths, filters, food and a custom map, in shared and native saves.
- Preflight required resources before replacing the open zoo. Report missing
  content clearly and respect the existing mod switches; never enable every mod.
- Parse and validate before committing world, books, research, goals and view.
  Failed loads must not replace the current zoo with an empty or partial world.
- Exercise malformed state, unsupported versions, truncated files, failed writes
  and destination replacement. Keep an existing destination on save failure.

Done when: missing content cannot silently disappear, a failed load leaves the
open zoo and mod selection unchanged, and re-enabling the required content loads
the original file intact. Corruption/write-failure checks must verify read-back,
not merely that an error was displayed.

Code: `src/SaveGame.cpp` (`getWorld`, `load`, `encodeOriginal`), `src/main.cpp`
(`gameLoad`, `applyModSelection`) and resource/mod resolution.

## 2. Close the remaining mapped-state gaps

Priority: finish the supported save scope before freezing the candidate.

Work:

- View: convert the current camera centre, rotation and zoom into the original
  view fields. The current writer retains the source original view, and original
  import only handles part of its saved-view combinations.
- Tank ground: verify floor/water levels across repeated exchanges, then demolish
  an imported tank. Preserve exact native pre-tank ground. For original-only
  imports, establish which underlying ground can actually be recovered and use
  a documented fallback for information the original file does not contain.
- Equipment/managers: map remaining supported filter servicing, wear, cleaning
  and accounting timers. Check research progress, recurring costs, finance year
  transitions and manager references after edits and original resaves.
- Guests: finish the original thoughts and visited-record interpretation where
  representable; verify occupied buildings/seats and references to removed or
  engine-only objects. Native guest thoughts are already serialized; the gap is
  original interchange, not adding another native thoughts field.
- Audit animal life/breeding/disease transitions and staff assignments at load
  boundaries. Record unsupported fields explicitly instead of equating retained
  raw bytes with restored behavior.

Done when: a field matrix identifies what is restored, safely reconstructed,
preserved only as bytes or unavailable. Supported fields have before/after
comparisons in both directions. Any remaining limitation is explicitly scoped;
do not call the exchange lossless.

Code: `src/SaveGame.cpp`, `src/ZooReader.cpp`, `src/World.cpp`,
`src/WorldRenderer.cpp`, `src/Fences.cpp`, and the relevant actor controllers.

## 3. Verify saves during real activity

Priority: release blocker for supported gameplay.

Save and reload while two dolphins are travelling to a show, performing, and
returning; while guests occupy stands/shops; while a specialist climbs a ladder
or feeds; while maintenance services a pump; during egg hatching, sleeping,
illness and dinosaur capture. Test interruption, removal of an assigned target,
and several linked home tanks.

Native/shared engine loads should resume their supported state. Original
interchange may safely rebuild an incompatible task, but must clear stale
ownership and avoid duplicate charges, duplicate performers, stuck capsules,
floating people, lost actors or unfinished reservations.

Done when: each actor resumes or safely replans, shows finish and return animals
home, counts/identities remain correct, and a subsequent save/reload still works.
Run a final healthy marine zoo for at least 20 minutes of actual unpaused time,
logging modal/pause interruptions separately. Repeat the original runtime test
on a healthy exhibit; the Jan 1-6 welfare-interrupted run is not that soak.

## 4. Expand the compatibility matrix

Priority: release blocker for advertised combinations.

The custom-animal case is verified; it does not cover every mod category.

- Test a small theme pack containing custom animal/staff/building/filter/food,
  path/fence/ground, show content and a custom map; include stock replacements.
- Check the same shared file with its mods on in the engine and its compatible
  stock world in the original. Preserve all engine-only content in the unchanged
  shared file. Missing mods must follow phase 1, not silently reduce the engine zoo.
- Check custom show tricks/species against original definitions, so a stock
  identifier cannot conceal a trick or required item unavailable in the original.
- Exercise real native files from the previous public release and retained older
  fixtures. Conditional read branches alone are not regression evidence.
- Repeat edited-world exchange for land, marine/show, dinosaur and blank/custom
  maps. Compare geometry, actors, money, research and scripts after original
  resave and engine reimport, including several successive cycles.
- Test each original executable/expansion combination that will be advertised.
  Base/Dinosaur Digs map tests under Marine Mania do not prove acceptance by a
  base-only executable. Restrict claims if the matching executable is unavailable.

Done when: each advertised combination has a recorded runtime pass, supported
counts/state remain stable, and intentional stock substitutions are documented.

## 5. Finish the camera-performance check

Priority: existing release blocker until resolved or explicitly accepted by the owner.

Use the same busy marine and land scenes, camera routes, rotations, zooms and
guest populations. Measure running frame times while panning, placing animals,
plants/rocks, shops, animated attractions and path/fence previews. Profile the
remaining repeatable marine long frames; fix the measured cause and compare
against the reference path without losing rendering correctness.

Done when: frame-time reports include median, p95/p99 and worst stalls, the
repeatable regression is resolved, and terrain/sprite/tank pixel checks pass.
Any accepted residual stall must remain in the known issues; paused FPS or a
single average is insufficient evidence.

## 6. Freeze and rebuild the complete candidate

Priority: release blocker; after code changes and targeted tests finish.

- Record the exact reviewed source/diff and build configuration. Do not mix old
  packages with the new Lab executable or present a development hash as a final
  release hash.
- Correct stale updater/handoff documentation: current updater guides still
  reference the older candidate and native version 12. Keep canonical notes,
  packaged notes, Help and compatibility claims consistent.
- Through the Lab build workflow, rebuild installed game, portable game and Lab
  together at the selected candidate version. Keep builds local and unpublished.
- Verify both pinned-key manifests, names, versions, size/hashes, matching bundled
  engines, portable download support, bundled backends and offline Help (now 21
  articles). Do not distribute original assets, personal saves, selections or keys.
- Rerun the supported save/show/activity/behavior/building/performance checks on
  the actual versioned executables being packaged.

Done when: one inventory maps source identity to executables, packages, manifests
and reports; inspection shows the current save feature is really in every package.
Any later code change invalidates affected package tests and requires a rebuild.

## 7. Exercise installation and updater handoff in isolation

Priority: release blocker.

Use a separate Windows test account or machine. A second installation folder
under the current account is not sufficient isolation because AppIds and profile
data are shared. Do not use Docker, WSL containers or Hyper-V containers.

Test clean install, upgrade from the previous public build, retained saves/mods/
settings/research, game data discovery, and portable first-run behavior. For Lab,
test installed-only mode and a linked checkout; a checkout must not hide missing
bundled files. Confirm mouse controls, minimize/restore and embedded Play.

Use local candidate feeds to perform an actual download, installer launch,
upgrade and `/RELAUNCH` or `/RELAUNCHLAB=1` return. Keep public feeds unchanged.
The earlier intercepted installer spawn and signature checks do not satisfy this.

Done when: both products upgrade and relaunch correctly with user data retained,
and corrupt or changed installers remain rejected. If the isolated environment
is unavailable, record the gate as pending rather than testing on the owner's
live install without agreement.

## 8. Owner acceptance and publication gate

Provide the exact packaged Lab tested above plus a short checklist: marine
shows/returns, all four views, normal placement/demolition, mod switches, original
import, shared/native Save and original resave/reimport on copies. Retain old
backups because version-14 saves are not rollback files for older engines.

Done when: the owner accepts the concrete candidate. Then obtain the requested
authorization for any final source push/mirror and publication, verify GitHub and
CodeRook read-back against the tested identity, and publish only those artifacts.
No release tag or live feed change while an earlier blocker remains open.

## Compatibility work beyond this release scope

Full scenario goals/events and the six patched scenario thoughts failures remain
separate unfinished work. Exact byte-for-byte original behavior/task-stack
continuation and every undocumented private field are also a larger parity effort.
Phase 3's safe recovery is still required; these are not excuses for stuck actors
or lost save content.

The original executable discarding an appended engine snapshot on its own resave
is verified container behavior. The supported workflow keeps the engine copy and
resaves separately in the original. Do not promise that original resaves retain
engine extras, or introduce an external sidecar and call it the same single-file
design.
