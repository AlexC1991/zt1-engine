# Release candidate 2026.10.29

Status: local candidate, not approved for publication. This supersedes the old
2026.10.28 package inventory. Both public update channels still serve 2026.10.20
as checked on 29 October. Nothing in this document authorizes publication.

## Scope

Marine shows, tank rendering and management fixes, automatic specialist work,
attendance/performance, mod studio/help and the original-save import improvements
since v2026.10.20. The draft notes are in
engine-build-resources/NEXT_RELEASE_NOTES.md. Full scenario parity and original
format export remain separate unfinished features. Original saved show scripts
and active performances are not imported into the new show controller. Native
saves use format 12; keep old copies for rollback.

## Current verification

Gameplay source edits, builds and game controls use the Lab MCP. Packaging
scripts/docs use file tools where the Lab editor is read-only. Source saves remain
untouched. Reports: E:/zt1-shots/release-candidate-2026-10-29/.

| Check | Development build result |
| --- | --- |
| Original imports and native v12 round trip, four real original fixtures | 1,131/1,131 |
| Marine management | 30/30 plus 18/18 show starts |
| Complete show programs and returns, all six stock show species | 27/27 plus 18/18 show starts |
| Buildings, gates, ladder directions and whale-shark clipping | 54/54 |
| Buildings and dinosaur capture | 44/44 |
| Behavior parity | 43/43 |
| Lab process, reply and temporary mod profiles | 21/21 |

The six show species are orca, dolphin, great white shark, jellyfish, sea lion
and sea otter. Each complete program uses two adults and checks their return
home. Object tricks, seats, portals, interruptions and saves also have dedicated
checks. This is targeted coverage, not every possible trick/geometry/mod.

## Packaging and remaining gates

Build installed game, portable game and Lab together as version 2026.10.29.
The Lab recipe is engRelease with version, official:true and lab:true. Verify
the full hashes, both signed manifests, matching bundled engine, offline Help,
backends and absence of original assets, personal saves and private keys.
Do not change pinned update keys. CodeRook's signed game feed is zt1-update.yml.

- [x] Rebuilt candidate packages and full payload/signature read-back.
- [x] Regression checks on the versioned engine matching the packages.
- [x] Native zoo soak, all four views and normal Save/reload read-back.
- [x] Signed Lab updater download, corruption rejection and pre-install recheck on loopback; install spawn intercepted.
- [ ] Isolated game and Lab clean installation and upgrade with data retained.
- [ ] Actual Windows updater installer/relaunch handoff in isolation.
- [x] Source checkpoint prepared from the tested tree; see the local source inventory for its commit and mirror result.
- [ ] Owner's final packaged Lab acceptance.

Do not mark an isolated check passed based solely on signatures or source review.
No live updater feed, release tag or public download changes before acceptance.

Both game and Lab are installed on this PC. Their stable AppIds, shared profile
data and installer relaunch targets make a second directory alone insufficient
isolation. Actual installation/upgrade remains pending a separate Windows test
profile or the owner's accepted candidate installation; it was not run against
the live installs. Local signed download checks do not substitute for this gate.

## Final local artifacts and evidence

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
Report any failure before publication; retain the pre-update save.

Known issues: overcrowded exhibits may repeat animations or twitch while turning;
full original-save parity/export and scenarios remain incomplete. The original
Smilodon fplaywithsoud opcode and prototype Amberhorn containment remain known
limitations. Never frequency is implemented, not an outstanding feature.
