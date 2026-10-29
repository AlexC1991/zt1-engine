# Release candidate 2026.10.29

Historical candidate record: the owner approved shipping the current changes as
2026.10.30, including the later loading-order fix, with no further testing.
See [release notes](RELEASE_NOTES_2026-10-30.md). The inventory below describes
the earlier 2026.10.29 candidate, not the rebuilt 2026.10.30 packages.

Status: local candidate, not approved for publication. This supersedes the old
2026.10.28 package inventory. Both public update channels still serve 2026.10.20
as checked on 29 October. Nothing in this document authorizes publication.

30 October owner follow-up: an intermittent first-load white-window freeze and
closure worked on retry. A startup research synchronization fix and stage timing
logs are tested locally, but are not in the packages below. See
[save-load follow-up](SAVE_LOAD_FOLLOWUP_2026-10-30.md) for evidence and limits;
the reported closure is not established as resolved.

All three packages were rebuilt on 30 October (Sydney) as candidate 2026.10.29.
They include native v14/shared saves and the final submerged-dung projection
fix. The exact packaged engine passes the save, activity, show and rendering
checks below. A private CodeRook test draft carries the laptop downloads;
the public release and update feeds remain v2026.10.20.
Dense marine performance and original task/show parity still have documented
limits. See [camera evidence](CAMERA_PERFORMANCE_2026-10-29.md) and the
[owner test checklist](RELEASE_TEST_CHECKLIST_2026-10-29.md).

## Final package inventory

Engine SHA-256:
73011c28d0dfcbf7b9cbc866f058f49837a2d14f43a85ab844ac6ef8a638804c.
The bundled Lab engine and the Lab Test Zoo executable match this exactly.

| Artifact | Bytes | SHA-256 |
| --- | ---: | --- |
| ZT1-Engine-Setup-2026.10.29.exe | 4952947 | 5dfdc723ee1235c9653208b736eb3cad13b07b0d9818656e7fb6efa01c44d22b |
| ZT1-Game-Build-2026.10.29-win64.zip | 4022552 | 6662f3fe96d9e0a4a0d1ff15b98464ec035ab267ae573c6a9e6532b50a73aa23 |
| ZT1-Lab-Setup-2026.10.29.exe | 616266042 | 70619364cedf9dff65762ff94b6e8b35586a17e4110237b6fe44444e11b663fa |

dist/candidate-2026.10.29-inventory.json records all artifact and manifest hashes.
Both pinned signatures, tamper rejection, portable ZIP CRC and payload checks
pass. Lab carries 21 offline Help articles, nine videos and all 101 referenced
media entries. No original game assets, personal saves, projects or private keys
are in the packages. The private test draft's sample zoo is a separate attachment.
The production Lab updater downloads the new installer on loopback and rejects
changed/signature-invalid bytes; its installer spawn is intercepted.

The owner chose laptop gameplay testing instead of repeating the full installer
and updater matrix, citing the working previous release. No installer/updater
source changes were found. Actual Windows installation/relaunch is not claimed
as newly verified. The final original exchange removes the tank-filling dung
warning and reimports two dolphins and six workers with zero missing actors.
An original-game specialist access warning in the diagnostic tank layout remains
under investigation; original show/task parity is partial.

## Scope

Marine shows, tank rendering and management fixes, automatic specialist work,
attendance/performance, mod studio/help and the original-save import improvements
since v2026.10.20. The draft notes are in
engine-build-resources/NEXT_RELEASE_NOTES.md. Shared saves now write the current
original-compatible world plus the engine snapshot in one `.zoo`. Original show
scripts, species, frequency and admission import/export; active performance and
spectator continuation remain partial. Native snapshots use format 14; keep old
copies for rollback. The original's resave discards engine-only data. Full
scenario parity and exact original AI/guest continuation remain incomplete.
Original view conversion covers all four turns at half/normal/double zoom.
See [save compatibility](ORIGINAL_SAVE_COMPATIBILITY.md).

## Current verification

Gameplay source edits, builds and game controls use the Lab MCP. Packaging
scripts/docs use file tools where the Lab editor is read-only. Source saves remain
untouched. Reports: E:/zt1-shots/release-candidate-2026-10-29/.

| Check | Development build result |
| --- | --- |
| Original import, native v14/shared round trips, four real original fixtures | 1,431/1,431 |
| Fresh base-game and Dinosaur Digs save checks | 278/278; tank-only cases explicitly skipped where absent |
| Real original load/resave | Added dolphin/shop, edited show scripts, T. rex egg/scientist, tank filter and litter retained |
| Engine-only content in the same shared zoo | Custom animal restored in engine; original loads stock projection; elevated deck covered by regression |
| Ordinary Save dialog | Shared default, engine-only choice, extension changes, switch back and cancellation pass |
| Marine management | 30/30 plus 18/18 show starts |
| Complete show programs and returns, all six stock show species | 39/39 plus 18/18 show starts |
| Buildings, gates, ladder directions, whale-shark clipping and sprite visibility | 77/77 |
| Bounded terrain traversal against reference pixels | 224/224 |
| Populated world screenshots, four rotations at two zooms | 8/8 identical world crops |
| Buildings and dinosaur capture | 55/55 |
| Behavior parity | 57/57 |
| Lab process, reply and temporary mod profiles | 21/21 |

The six show species are orca, dolphin, great white shark, jellyfish, sea lion
and sea otter. Each complete program uses two adults and checks their return
home. Object tricks, seats, portals, interruptions and saves also have dedicated
checks. This is targeted coverage, not every possible trick/geometry/mod.

## Packaging and remaining gates

The ordered implementation and acceptance plan is
[save and release completion](SAVE_RELEASE_COMPLETION_PLAN_2026-10-29.md).
It starts with missing-content and failed-load protection before packaging.

Build installed game, portable game and Lab together as version 2026.10.29.
The Lab recipe is engRelease with version, official:true and lab:true. Verify
the full hashes, both signed manifests, matching bundled engine, offline Help,
backends and absence of original assets, personal saves and private keys.
Do not change pinned update keys. CodeRook's signed game feed is zt1-update.yml.

- [x] Rebuild all three packages after the final save fix; full hashes, payloads and both pinned-key signatures pass.
- [x] Final packaged engine: 1,709 save checks, 77 activity, 57 parity, 55 buildings/DRT, 39 show polish, 30 management, 224 terrain pixels and submerged/dry/bank dung checks pass.
- [x] Clean 20-minute native zoo soak and full-file reload: 1,289.86 unpaused seconds, six completed shows, zero cancelled/skipped, two healthy adults and automatic pump service. Original run also exceeds 23 minutes and is reimported. Final packaged-engine checks remain separate.
- [x] Signed Lab updater download, corruption rejection and pre-install recheck on loopback; install spawn intercepted.
- [ ] Full isolated installation matrix not rerun; owner chose laptop gameplay testing based on the working previous release and unchanged installer/updater code.
- [ ] Actual Windows updater handoff not rerun; waived by the owner for this unchanged updater. Do not describe it as a new runtime pass.
- [ ] Checkpoint and mirror the final camera-performance/save tree when authorized. The previous tested tree was checkpointed.
- [ ] Repeat shared/native saves and original load/resave using the final packaged engine; retain the engine copy when resaving in the original.
- [ ] Owner checks stock land, marine/show, dinosaur and custom-mod saves from copies, including both Save choices and required mod switches.
- [x] Dense land exhibits, occupied shops/attractions and animal/building/scenery/path placement workload checks.
- [ ] Investigate or explicitly accept the remaining intermittent marine panning stalls.
- [ ] Owner's laptop acceptance: private CodeRook draft test-2026.10.29-saves contains the verified downloads, instructions and show zoo. Use its direct Lab object link to avoid the legacy attachment-route truncation.

Do not mark an isolated check passed based solely on signatures or source review.
No live updater feed, release tag or public download changes before acceptance.

Both game and Lab are installed on this PC. Their stable AppIds, shared profile
data and installer relaunch targets make a second directory alone insufficient
isolation. Actual installation/upgrade remains pending a separate Windows test
profile or the owner's accepted candidate installation; it was not run against
the live installs. Local signed download checks do not substitute for this gate.

## Previous local artifacts and evidence (stale)

These hashes describe the earlier candidate only. They are not the current save
build and must not be used as evidence that shared saves are in a package.

Engine SHA-256: 08c84d01245da5b1b212cb5a07f144051f6fe094dd45d9a211de2053caf198da.

| Package | Bytes | SHA-256 |
| --- | ---: | --- |
| ZT1-Engine-Setup-2026.10.29.exe | 4881375 | 99be5d7bb876d576f88322275e9aa2e705a1198b44d35de436ebc8b45875d840 |
| ZT1-Game-Build-2026.10.29-win64.zip | 3910535 | ae5494b8b44c61c12732d974e4a3fc9c14d8c934417446ae3e85758ba122bcf3 |
| ZT1-Lab-Setup-2026.10.29.exe | 616098583 | 1444966e889d5b47564a75bd95274c50848cf0d11cddff00871b1328e4e02727 |

dist/candidate-2026.10.29-inventory.json records full artifact/manifest read-back.
Pinned-key signatures pass; modified app/version/name/size/hash/signatures fail.
The Lab updater downloads and hashes the real installer from loopback, refuses
corrupted bytes and rechecks a changed file at install time. Its spawn was
intercepted: this verifies command arguments, not an actual Windows handoff.
All 20 offline Help guides and 101 media references are present. Bundled Python
imports capstone, pefile and unicorn; the bundled JDK starts. Payloads contain
the matching engine/backends and no original assets, saves, projects or keys.

Package testing found and fixed stale README version text and a missing portable
mod-download client. The ZIP now includes the matching modrepo.dll, and start.py
creates a distinct per-install key while preserving an existing one. The key is
generated locally, not distributed. The ZIP CRC, paths and absence checks pass.

The native soak was a 25.7-minute wall-clock session, with short award/menu/save
and inspector interruptions recorded separately. Even allowing five minutes
for those interruptions leaves over twenty minutes of running time. All four
camera views were used. May advanced to August; six additional scheduled shows
completed (3 to 9), with no skipped tricks or extra cancellations. Final native
read-back has all three healthy dolphins home, 675 home food units and no animals
or food in the show tank. The original diagnostic/source saves were not overwritten.
Two unpaused candidate captures measured 46.8 and 42.1 FPS; later captures varied
with show phase and background packaging. Do not use paused captures as running
performance evidence.

## Owner test before shipping

Open the packaged Lab and check mouse controls, minimize/restore, Help and Play.
Use a copy of your marine zoo: allow the automatic dolphin show to start, check
both adults, specialist platform height, seats, jumps, toys and return home.
Rotate all four views and try large swimmers by the platform/filter corners.
Check feeding in the home tank and no ordinary food in the show tank. Move a
platform, link another home tank, change show admission, then save to a new
filename and reload. Test an older original .zoo and a native save with mods off.
Save a shared `.zoo`, open a separate copy in the original, resave to another name,
then import that original resave in the engine. Test Engine-only `.zt1save` too.
In a modded zoo, check the original's reduced stock world and the custom animal
restored from the unchanged shared file in the engine with its required mod.
Report any failure before publication; retain the pre-update save.

Known issues: overcrowded exhibits may repeat animations or twitch while turning;
exact original save-state continuation, view conversion and scenarios remain incomplete. The original
Smilodon fplaywithsoud opcode and prototype Amberhorn containment remain known
limitations. Never frequency is implemented, not an outstanding feature.
