# Release candidate 2026.10.28

Release authorization: the owner approved publishing this alpha on 28 October
with overcrowding/repeated animations/direction twitching documented as a
known issue. Rebuilt packages must contain the current steering changes.
Earlier candidate gates below remain a record of tests and limitations;
the owner's explicit publication instruction supersedes their approval hold.

Previous candidate status: BLOCKED — user reported tank steering jitter. The existing dist
installers predate the steering fix and must not be shipped. The updated
Lab engine is a test build pending the user's verification; rebuild all
release packages after acceptance and replace their inventories/signatures.

Do not create a public release, promote a
CodeRook version, change the live update feeds or push a release tag until
the acceptance checks below pass and the user approves shipping.

## Scope and baseline

The last public release is [v2026.10.20](https://github.com/AlexC1991/zt1-engine/releases/tag/v2026.10.20).
The candidate contains the current workspace, including work not yet
committed. Its parent is `61b4f988`. The complete draft release notes are
`engine-build-resources/NEXT_RELEASE_NOTES.md`; they must become the approved
tag/release notes only after testing. Read the known limitations there.

This is a date version with three numeric components, `2026.10.28`. If more
changes are needed after testing, rebuild and replace the candidate hashes.
Never silently replace an already published installer under the same version.

## Expected local artifacts

All are under `dist/`; the candidate inventory records actual size and SHA-256.

Packaging completed successfully in the Lab (789.3 seconds). All three
checksum companions and both signed manifests passed full-file read-back.
The Lab production validator rejected altered app/version/size/hash/signature
fields. Packed runtime modules match source; all 20 Help guides and 101
referenced media entries are present. The portable ZIP's CRC and x64 engine
match passed. Original saves and normal settings retain their initial hashes.
See `dist/candidate-2026.10.28-inventory.json` and
`dist/candidate-2026.10.28-portable-check.json`.

| Gate | Status |
| --- | --- |
| Local build, packaging, signatures and payload checks | PASS |
| Native checks rerun on the candidate executable | PASS: 1,131 import + 38 behavior + 44 polish + 51 activity |
| User Lab acceptance session | PENDING |
| Isolated clean install, upgrade and actual updater relaunch | PENDING |
| Source checkpoint/tag matching the accepted artifacts | PENDING |
| Approval and public publication | PENDING; do not ship |

| Artifact | Purpose |
| --- | --- |
| `ZT1-Engine-Setup-2026.10.28.exe` and `.sha256` | Per-user installed game, official updater enabled when running installed |
| `ZT1-Game-Build-2026.10.28-win64.zip` and `.sha256` | Portable game; imports the owner's original assets, no self-update |
| `ZT1-Lab-Setup-2026.10.28.exe` and `.sha256` | Lab, matching engine, hook, Python, Ghidra/JDK and notices |
| `latest.yml` | Signed ECDSA game feed; do not put it on a live channel yet |
| `ZT1-Lab-update.json` | Signed Ed25519 Lab manifest; do not publish yet |

The release recipe runs inside the Lab via its `engRelease` action with
`{"version":"2026.10.28","official":true,"lab":true}`. It builds and packs;
it does not upload. `--official` is necessary for an installed game to test
its updater. The Lab installer requires its backend and licence notices.
Neither package includes the original game's assets or a Ghidra project
containing zoo.exe. The Lab's shipped mods.cfg starts with loading off.

## Checks already run during development

| Check | Result and scope |
| --- | --- |
| Original import and full native-v12 round trip | 1,131/1,131 across marine3, marine2, marine and show-zoo fixtures |
| Behavior integration (`paritycheck`) | 38/38, including capture, eggs, tours, guest needs, walrus routing and park-duration thoughts |
| Activity (`activitycheck`) | 51/51; 116 drawable buildings, 26 gates, portal, male/female specialist ladder visits on all sides |
| Building/DRT (`polishcheck`) | 44/44 |
| Lab reply, process and mod-profile tests | 21/21 |
| Species scan | 119 definitions; original `fplaywithsoud` typo remains reported |
| Native embedding | Minimize, restore, resize to 1100x760 and restoration of prior bounds/maximized state |

Evidence: `E:/zt1-shots/non-scenario-audit-2026-10-28/`, plus the linked
building, show, mod-switch and save-compatibility audit documents. These are
targeted checks, not certification of every original save or custom mod.

## Your Lab acceptance session

Use copies of saves and a separate test zoo. Record the candidate hash,
original game edition, selected mods and a screenshot for each failure.
Save to a new filename containing `RC-2026.10.28`; keep pre-update originals.
New native saves are version 12 and cannot be reopened by older engines.

| Order | Do this in the Lab | Pass condition |
| --- | --- | --- |
| 1 | Open the candidate Lab, verify version and Help/F1, then test minimize/restore/resize, tabs and Play controls with the mouse. | UI stays clickable with Play stopped and running. Current tutorials and videos open offline. No missing-module or blank-pane startup. |
| 2 | Play Engine: make stock Basic Grass with mods off. Open Game Options while running, close it, then repeat while manually paused. | Menu pauses; closing restores the previous pause state. No unexpected mod items. |
| 3 | Download/import one tutorial mod, then enable it deliberately. Test main-menu and in-game switches and all pagination arrows. | Download starts off; optional replacements toggle live; counts/cash/pause remain. Pages and Required explanations do not overlap. |
| 4 | Open a bundled custom map and place a custom animal. Attempt to disable their providers, return to main menu, then try again. End a temporary Lab test too. | Required content cannot disappear from an active zoo; message explains why. Main menu releases requirements; temporary testing leaves normal selections unchanged. |
| 5 | Flat Test Zoo: run `polishcheck`, `activitycheck`, `paritycheck`, loading flat ff01 again before each. Then use `polishdemo` / `activitydemo`. | All checks pass. Watch several minutes: stores get customers, smoke/doors work, gates hold endpoints, specialist enters and exits facing correctly. Static-only idle art is expected. |
| 6 | Build a marine home tank and linked show tank, portal, grandstand and specialist. Add compatible animals, depth and trick props; edit and run a show. Save/reload during and after it. | Gate layers correctly, tricks/spectators/admissions progress, animals return home, no double admission. Drain/cancel recovers safely. |
| 7 | Try whale shark and other large swimmers near corners; walrus with connected land and sunken water; normal land species and dinosaurs in groups. | No persistent wall penetration, stuck turn or piling. Animals can reach suitable land/water, food and carers. Disliked plants/rocks affect suitability. |
| 8 | Break/wear a filter; watch repair, idle/decayed/broken animation and sound. Escape a dinosaur near the DRT/scientist and a zoo animal near a keeper. Hatch a custom egg. | Correct carer/job handles each case; DRT base stays on grid through all four views; capture returns safely and hatching completes. |
| 9 | Load copies of your original `.zoo` and older `.zt1save`. Compare cash/date, finance rows, entity counts, research, staff, inventory and map in Play Original where possible. Save native v12 under a NEW name and reload. | No crash or unexpected content. Known conversion gaps are documented; newly lost supported state blocks release. Original file hashes do not change. |
| 10 | Run a busy zoo for 20–30 minutes; rotate/zoom, pause, switch windows, load another zoo, return to menu and Continue. | No hang, crash, runaway sound, duplicate guests/admission or unexplained save loss. |

## Installer and update acceptance — required before shipping

Test on a spare Windows profile/VM or backed-up test installation, without
replacing the developer's live setup. No Docker is required. A source checkout
can mask missing installed files: include a Lab start with no checkout and
without a system Python/JDK. Then test a Lab explicitly linked to a checkout.

1. Fresh-install game and Lab; select the owner's Zoo Tycoon folder. Verify
   matching versions, Play Engine, Play Original, Test Zoo, decompilation,
   Mods and offline Help. Bundled engine must match the candidate engine.
2. Upgrade test copies of v2026.10.20 with the candidate installers. Stable
   installer AppIds and paths must preserve saves, settings, downloads,
   mod selection, install keys, Lab root selection and private research.
3. Extract the portable ZIP into a new folder, run start.bat/import and load
   a copy of a save. It must report no automatic updates and need no bundled
   original assets. Do not use the working checkout as the portable test.
4. Start again from separate v2026.10.20 test copies for update testing:
   an already-installed 2026.10.28 correctly will not offer itself as newer.
   Serve signed candidate feeds on loopback only. Use the game updater's
   `ZT1_UPDATE_FEED` test override and the Lab's `ZT1_LAB_UPDATE_FEED` plus
   `ZT1_LAB_UPDATE_TEST=1`; see updater source for the exact override format.
   Lab test mode verifies/downloads but deliberately cannot install. Verify
   good signatures, exact size/hash and rejection of corrupted bytes,
   wrong app/version/name and invalid signatures before manual install tests.
5. On the isolated test install, verify the game's main-menu Update now,
   Skip and Later behavior and the Lab's Restart to update. Game installer
   uses `/RELAUNCH`; Lab uses `/RELAUNCHLAB=1`. Confirm only the intended app
   restarts, old child engines exit, input works and user data is unchanged.

Local signature checks do not prove Windows installer handoff. Do not mark
the update/upgrade rows passed until that actual handoff has been observed.

## CodeRook and publication gate

The failed [Mirror run 40311012096](https://github.com/AlexC1991/zt1-engine/actions/runs/40311012096)
was rejected because retained protected files were excluded by .gitignore.
Commit `61b4f988` added `--allow-ignored` and exported `git archive HEAD`;
[run 40311557413 succeeded](https://github.com/AlexC1991/zt1-engine/actions/runs/40311557413).
The retention helper keeps owner-only files outside the public deletion
baseline. The local release uploader also stops on a failed save before
selecting a version or attaching downloads.

Public GitHub CI only builds the engine and mirrors main. The official
release tooling and installer scripts are intentionally private; the local
ignored `release.yml` is historical and is not an installed GitHub workflow.
Pushing a commit is not a release. After acceptance: commit the exact tested
source, review its diff and private tooling, create the annotated date tag
with approved notes, then run the private release publisher using the exact
tested artifacts. Do not rebuild silently after approval. If `--skip-build`
is used, first compare the candidate inventory and ensure the tag describes
the source used to build it.

Both products are one CodeRook `zt1-engine` project; mods remain separate.
Attach all installers, ZIP, checksums and signed manifests before naming and
making the CodeRook version public. Attach the game's signed latest.yml as
`zt1-update.yml` too: CodeRook generates its own unsigned latest.yml.
Run `verify_release.js <version>` after publication to read back both live
channels. Game: CodeRook signed feed first, GitHub fallback. Lab: CodeRook
manifest first, GitHub fallback. Never change the pinned public keys or
publish an unsigned feed to repair an update failure. Both current pinned
keys were compared with v2026.10.20 and are unchanged.

Release blockers: any crash/hang, broken input, new save corruption/loss,
unexpected default mods, missing installed resources, failed signature/hash,
broken upgrade/relaunch, or an unreviewed artifact/source mismatch. Fix the
failure, rebuild, replace the inventory and rerun affected checks. Approval
to ship is still pending.
