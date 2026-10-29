# Save and release execution - 29 October 2026

The owner requested all eight phases and tests for each. Work is local until
the final candidate is accepted. This log records observed results, not intended
results. It supplements the completion plan and compatibility reference.

## Progress

| Phase | State | Evidence or remaining work |
| --- | --- | --- |
| 1. Protect saves/open zoo | Development checks pass | Separate-world normal Load; missing-resource rejection; bounded counts; rejected missing animal preserves current zoo. Missing custom-map dialog names the map; temporary profile re-enables and loads it, normal switches unchanged. Repeat on final packages. |
| 2. Mapped state | Development checks pass within documented scope | Camera turns/zooms, filter costs, original demolition fallback, guest thoughts/visits pass. Native v14 favourites survive catalogue reordering. Exact pre-tank slope/terrain survives native/shared files and demolition. |
| 3. Active-job saves and soak | Development checks pass | Full-file show/ladder/pump/shop/dinosaur/sleep/illness/hatch tests pass. Original healthy run exceeds 23 minutes; clean native run reaches 1,289.86 unpaused seconds, six completed shows and zero cancellations/skips. Final package rerun remains. |
| 4. Compatibility matrix | Development matrix passes within scope | Retained native 6/7/8/10/11/12/13 load. Theme pack loads in both games within the stock-projection limits. Custom show behavior stays native-only. Program accumulation fixed; 1,709 checks pass. Runtime scope is MM/DD 12.04.00.0009 only. |
| 5. Performance | Measurements complete; acceptance open | Latest valid running marine pans: 48-50 FPS with 1,000 guests, no frames over 33 ms in these short captures. Earlier intermittent stalls remain documented. Running land pan: 57.8 FPS. Final pixel/suite rerun follows. |
| 6. Packages | Verified locally | All three final packages rebuilt; matching engine, full hashes, signatures, Help and payload checks pass. Final packaged-engine checks are in report 203. |
| 7. Install/update | Full matrix waived by owner | Prior public release worked; installer/updater source has no changes. Existing package/download checks pass. Owner requested private CodeRook downloads for laptop gameplay testing; no new installer/relaunch runtime pass is claimed. |
| 8. Owner acceptance | Pending | Requires the tested packages and the owner's actual result. |

## Current evidence

`E:/zt1-shots/save-compatibility-2026-10-29/loadguard-checks-49.json`:
marine3 258/258, marine2 578/578, marine 209/209, show 202/202,
Dinosaur Digs 142/142, base 100/100. Includes rejected missing animals, staff,
objects, paths, fences, filters, food and elevated paths; invalid month counts;
truncated native state; and failed replacement preserving its destination.
Counts will change as more checks are added; these are not final package results.

Actual ordinary Play loads `dolphin-shared-17.zoo`, then rejects
`custom-native-33.zoo` with its temporary mod disabled. The existing world
remains visible and continues; `loadguard-reject.png` shows the missing
`ambermos.uca` message. Earlier screenshots where no dialog opened are invalid
missing-mod UI evidence and are retained as failed attempts.

The load-result UI calibration identified the planted wrong failure colour.
The independent reviewer then identified invisible main-menu failures and
unnamed original missing definitions; both were corrected and statically
rechecked. Main-menu runtime confirmation and extra resolutions remain pending.

Original Play loads `camera-turn-51.zoo` and resaves `camera-turn-og-53.zoo`
with rotation 3 and scroll -943,-1586 retained. `camera-half-52.zoo` loads at
zoom -2 and rotation 3; `camera-half-og-54.zoo` retains both. A 32-pixel scroll
change occurred with the cursor at the edge during unpaused load; subsequent
comparisons must move the cursor to the centre before waiting.

Original filter inspection: load 0x45d468 and save 0x615b31 store health,
last/current/lifetime costs (+0x168/16c/170), two dates and accounting period.
The decay/cleaning countdowns (+0x160/164) are not in the original save stream;
placement/load at 0x45d384 resets them. Engine snapshots already keep both.
Cleaning charges at 0x49bec7 update current and lifetime costs; monthly rollover
in 0x497e0f moves current to last. Mapping and tests are in progress.

## Follow-up results

`step2-regression-59.json` records 1,579/1,579 checks across the six fixtures:
275, 595, 226, 217, 154 and 112 respectively. Each includes twelve camera
export/import combinations. Later targeted tank-ground checks pass 279/279 on
marine3 and 221/221 on the show fixture (`activity-tank-70.json`).

The ordinary main-menu Load failure reports `ambermos.uca` in its native dialog;
`dialog inspect` confirms its title, message and OK button. After dismissing it,
another Load works. A real original resave caught its specialist partway up a
ladder; new-hire placement rules previously rejected that record. Import now
restarts such workers on accessible ground near their habitat's gate. Ordinary
Load of `camera-half-og-54.zoo` restores all actors with zero missing records
(`camera-half-engine-60.png`).

Original-only imports no longer invent a pre-tank ground snapshot from the
excavated floor. Demolition uses level surrounding ground and grass when the
original file has no underlying snapshot. Native snapshots keep their stored
ground data. Original filter countdowns are not serialized by zoo.exe; native
filter countdowns and last/current/lifetime costs survive full saves.

`active-show-fullfile-57.json`: 39/39 show-polish checks plus 18/18 starts,
including actual native/shared files while gathering, performing and returning.
`activity-tank-70.json`: 77/77 activity checks. It saves during each of the four
specialist climb stages and during real pump service; workers safely replan and
complete their work after reload. Test fixture corrections replaced fake Lab
animal counts with a real dolphin and avoided mistaking the immediate bank
restart for a completed visit. Earlier failed reports 63/66/67 remain as failed
attempts, not acceptance evidence.

The guest-thought manager was checked against original load `0x59435b`, record
load `0x594e63`, constructor `0x423043` and formatter `0x4edbfc`. Each current
record is five words: text id, speaker unit id, subject unit id and optional
habitat x/y coordinates. The final pair is not a timestamp. Follow-up inspection
of `0x410349`, `0x410bf9`, and the formatter corrected the earlier catalogue-index
interpretation. Plain thoughts and thoughts about a surviving named unit passed
across all six fixtures; habitat-name mapping is queued for the next build.
Deleted/omitted unit references
are removed from the compatible manager. `thoughts-regression-73.json` records
284/284, 636/636, 230/230, 221/221, 156/156 and 114/114 checks (1,641 total).

`parity-active-77.json` passes 57/57, including full native/shared files during
egg hatching, sleeping, illness and a shop visit. Hatching preserves one baby's
identity, sleeping restarts and finishes its behavior, sick animals remain sick
and accept treatment, and shop users safely replan without a duplicate charge.
The initial hatch probe used a restoring adoption that created an adult; it was
corrected to explicitly create an egg before starting the real hatch sequence.

Retained native version 11 and 12 files load. Version 11 imports exposed the
older generic `food` key; a targeted pre-v13 migration derives its chow from
the exhibit's real animals. Earlier diagnostic fixtures also used `grass` food
and `road` path placeholders; those known old keys migrate to `graschow` and
`path` only when their own resource does not exist. Version 7, 8 and 10 retained
files then load (`older-native-80.json`). These are old diagnostic files, not
proof of every save from every public version. Source snapshots are untouched.

`drt-active-82.json` passes 55/55 building/DRT checks. Native/shared snapshots
during spin-up, ascent, chase and firing safely park the helicopter and release
the target claim; a darted dinosaur stays asleep and a scientist contains it.
A second uninterrupted mission verifies descent, spin-down and landing, because
the interrupted mission intentionally restarts at the base.

The real-time engine soak started at 08:31:23 UTC on a stock two-dolphin zoo with
suitability 90; original Play starts conservatively at 08:32:43 after its initial
suitability award is dismissed. Normal needs/timers remain enabled. At engine
clock 331, the first continuous show is complete and the second is performing;
both dolphins retain health 400. Hunger/feeding and subsequent shows remain under
observation. No 20-minute pass is claimed yet.

Further original inspection: guest `+0x278` holds five recent viewed species
name IDs (`0x42e527`, `0x425b6d`), not building identities. `+0x260` contains up
to ten recent visited grid-coordinate pairs (`0x455fa2`, `0x42281c`, `0x4a840e`).
Import/export mapping corrections are open in the Lab editor for the next build;
the ongoing timed run uses the previous successfully built executable.

Original midpoint at 08:41:46 UTC (`healthy-marine-og-mid-96.zoo`) parses fully:
date February 16 2001, 3 dolphins (a calf was born), 4 specialists, 2 maintenance
workers, 2 pumps, one food pile and 36 guests. No welfare/pause popup observed;
the save/menu interval is excluded conservatively from the final timing. The
original has demonstrably advanced from January 1; a static screenshot alone
is not being used as timing evidence. The source shared file remains intact.

GitHub latest-release metadata was rechecked read-only: public release remains
`v2026.10.20`. Lab Sync fetch reports main matches its upstream, no conflicts,
and the current changes remain local. No push/mirror/publication was performed.

All C++ edits/builds and gameplay use Lab MCP. Private follow-up notes are not
read or packaged. No release, feed change or remote push has been made.

## Continued execution

The first engine soak reached clock 970.690 with five completed continuous
shows, zero cancellations/skips, both adults healthy at 400, and ordinary eating
and rest observed between performances. At about clock 949 a console command
intended as inspection added a food pile. This invalidates an uninterrupted
20-minute acceptance claim. The run is paused and preserved as
`soak-engine-interrupted-98.zoo`; the clean engine soak must be repeated.
The original run is unaffected and remains running from 08:32:43 UTC, with
brief hidden-tab/menu intervals excluded conservatively.

The next build includes corrected guest habitat-thought and recent-visit
mapping, plus stock-definition checks that keep unsupported mod show programs
out of the original-compatible stream. These changes are not passed evidence
until the new binary's checks finish. Original art lookup now excludes every
loaded mod, rather than accepting another enabled mod as a stock fallback.

`guest-habitat-regression-106.json` passes 1,695/1,695 across the six fixtures:
291, 675, 232, 223, 158 and 116. This includes habitat-name thoughts, recent
species/visit-list bounds and surviving manager references. `active-regression-107.json`
passes activity 77, parity 57, building/DRT 55, show polish 39 and marine
management 30, with 18 nested show-start checks in each show suite.

Original healthy run completed at 08:56:08 UTC, over 23 minutes after its initial
award was dismissed. Brief tab/menu intervals were allowed for conservatively.
Final `healthy-marine-og-final-105.zoo` parses April 29, three dolphins including
a calf, six workers, two filters, one fish pile and 34 guests. Ordinary engine
Load restores all actors with zero missed records; ordinary Save creates
`soak-original-reimport-110.zoo`, whose native section retains all three dolphins,
six workers, both filters and the two-trick show. These are actual Play controls.

The Moonstone Zoo Theme 1.2 temporary profile was exercised with its components
off and normal switches unchanged. `theme-food-shared-114.zoo` restores four
animals (including its dinosaur egg), two staff, the custom kiosk/scenery, both
filters, custom boundaries and paths in native state. However the original
rejects this file with its corrupt-save dialog (`theme-original-117.png`). The
parser accepting it is not a runtime pass. Investigation is focused on empty
original entity bodies and original/template identity matching. Keep phase 4
open and packages held until the corrected file is accepted by the original.

The Lab Food down command previously chose generic chow even for real animals
with a custom diet. It now calls the same food selection as staff; its new build
and custom-food check remain pending. The first compile exposed private access;
the read-only food query was moved to Staff's public interface before rebuilding.

The scenery failure was isolated with class-subset exports: scenery-only failed,
while the remaining actors/boundaries loaded. Zero-id map objects had matched
zero-body template declarations. Export now uses only real decoded source
identities in the same version and builds fresh bodies otherwise. Custom/legacy
`tankwall` boundaries use the stock `tankwal2` equivalent. The corrected theme
file `theme-corrected-126.zoo` loads in the original; its actual resave
`theme-original-resave-129.zoo` retains one stock dolphin, maintenance worker,
stock filter, 30 paths and 112 tank-wall records. Ordinary modded engine Play
loads the unchanged shared file with all custom content (`theme-engine-132.png`).
Custom food read-back confirms `fernfeast`, 1,000 units (`theme-customfood-118.zoo`).

Stock entrance pieces without a separate AI file are now admitted using their
stock animation resource. `healthy-native14-141.zoo` retains all three entrance
pieces and eight buildings. Unsupported custom dolphin flip behavior is omitted
from the stock program (`custom-show-unsafe-135.zoo`, original runtime accepts
the file). A stock TailWalk still exports with stock species id 5402 even when
the temporary mod renames the animal id to 30000. Original resave
`custom-show-stock-og-146.zoo` retains that program.

New native saves are version 14: favourite species is stored by definition file,
and recent seen exhibits persist. Reordered-catalogue regression passes. Read
paths for versions 1-13 remain. `native14-regression-139.json` passes 1,703/1,703;
`native14-active-140.json` passes activity 77, parity 57, DRT 55, show polish 39,
management 30 and nested starts 18. Terrain pixels pass 224. Retained native
6/7/8/10/11/12/13 files load (`native14-legacy-143.json`); versions 1-5 still lack
retained fixtures. Some old check labels still say v13, but file headers are v14.

Normal main-menu Load with the theme pack off reports `Missing map:
aquaticmodtest.scn. Enable its mod and try again.` Its native dialog's text and
OK button are verified in play.log; normal mod switches remain off.

A clean engine soak starts 12:21:12 UTC from `healthy-native14-141.zoo`, normal
needs/timers, speed 1. At clock 210 both adults remain at health 400 while
performing the first show. No food or need interventions have occurred. Do not
rebuild or restart until the run is paused and its final snapshot recorded.

## Final development checkpoint

The show-program table is now rebuilt from the current habitats. The ordinary
engine loads the second original resave, writes exchange-cycle3-engine-177.zoo,
and the actual original loads/resaves exchange-cycle3-original-181.zoo.
Both streams have exactly one species-5402 program with the two selected tricks.
Two dolphins, six workers, two filters, eight buildings, 235 ordinary boundaries,
160 tank-wall records and 2,245 paths survive. Ordinary engine Load/Save then
creates exchange-cycle3-reimport-182.zoo. The original's own date and finances
advance while running, so elapsed values are not compared as bit-identical.

final-save-regression-159.json passes all 1,709 checks (294, 678, 234, 225, 160,
118). Exact native pre-tank slope and terrain survive both .zt1save and shared
.zoo files and demolition. final-runtime-and-active-183.json records 224 terrain
pixel comparisons and activity 77/77, parity 57/57, show polish 39/39 and marine
management 30/30. Building/DRT independently reruns 55/55 on the same build.
Earlier exploratory suite calls on a non-flat marine fixture were rejected;
the relevant suites were then rerun on their required flat ff01 fixture.

The clean native run starts 12:21:12 UTC and pauses 12:42:50 UTC, with measured
unpaused simulation time 1,289.86 seconds. healthy-native-soak-158.json and
healthy-native-soak-final-158.zoo retain six completed shows, 27 performed tricks,
zero cancelled/skipped shows, two healthy adults and automatic pump servicing.
A seventh show is performing at the end. No food/needs/timer intervention occurs.
Two dolphins can overlap at a shared food pile; this visual limitation is now a
known issue, not a claim of complete animal separation.

Latest valid running marine camera captures 169/171/173/174 show 48-50 FPS and
no frames above 33 ms in each short 240-frame sample; the older intermittent
stalls remain disclosed. Land capture 176 is 57.8 FPS while panning past occupied
attractions, buildings, scenery and 48 animals. See the performance report.

The source is ready for a local package rebuild and checks of those exact
executables. Publication remains held. Original runtime scope remains MM/DD
12.04.00.0009; retained native versions 1-5 still lack real fixture evidence.
## Original tank-content export follow-up

The first full package build completes, but actual original Play of its normal
Save output packaged-shared-189.zoo raises the original's Large Poo warning:
stock poo lacks cUnderwater and blocks filling. These intermediate packages
are superseded and must not be distributed. The original resave is preserved
as packaged-original-resave-194.zoo, with the screenshot packaged-original-193.png.

The compatible projection now omits only generic dung below a filled tank's
water surface. Both native save formats keep it. Dry ground and raised banks
inside a tank still export. wet-dung-regression-196.json passes the projection
and full-file native/shared preservation cases. The initial test used 32-unit
coordinates instead of the original's 64-unit tiles; that rejected exploratory
result is preserved. A const-tile test-fixture compile error was corrected with
WorldMap's mutable accessor. The corrected build succeeds.

The same healthy soaked zoo is loaded and saved with ordinary engine controls
as wet-dung-fixed-shared-197.zoo. Final original runtime verification follows.
All three packages are rebuilding again from this fix; earlier package hashes
and packaged-engine-checks-187.json describe the superseded first build.
## Final candidate and owner-directed laptop test

The second package rebuild completes. Final engine 73011c28d0dfcbf7b9cbc866f058f49837a2d14f43a85ab844ac6ef8a638804c
matches the Lab tree and Test Zoo. final-packaged-engine-203.json reruns all 1,709
save checks, activity 77, parity 57, DRT/buildings 55, show polish 39, management
30, terrain 224 and the three wet/dry/bank dung checks. The new native/shared
files retain dung; the original stock projection omits submerged dung only.
The original filling warning is absent after correction. Original resave 201
reimports to final-engine-reimport-202.zoo with two dolphins, six workers and
zero missing actors. The original game itself adds an empty default show program
while running; the engine writer continues rebuilding only live schedules.
An original specialist access warning remains in screenshot 200; no claim is
made that original task/show AI is complete in this diagnostic layout.

All package SHA companions, pinned signatures, tamper rejection, portable CRC,
matching payloads and offline Help pass. The production Lab updater verifies the
full new download and rejects corrupted/changed installers with spawn intercepted.
The separate CLI feed verifier prints its successful validation before its Node
24 Windows child process aborts with UV_HANDLE_CLOSING; that harness exit is
preserved in local-feed-check-191.json and is not reported as a clean process pass.

The owner requests private CodeRook downloads for laptop testing. They questioned
repeating the installer/update matrix because the previous release worked.
Diff inspection finds no installer/updater source changes. Full isolated upgrade
and relaunch are waived, not passed. Owner gameplay acceptance still remains.
A private prerelease draft, test-2026.10.29-saves, is created on CodeRook save 22.
It is an artifact carrier for the local tested builds; its source snapshot is
explicitly labelled as the earlier checkpoint. The public latest is verified
as v2026.10.20. Upload/read-back is in progress; no public promotion or feeds change.
Private test upload complete: CodeRook draft test-2026.10.29-saves on save22,
channel test/prerelease. Twelve attachments include both installers, the portable
ZIP, checksum companions, source/artifact inventories, notes, instructions and
a stock dolphin show save. Every attachment is downloaded and hash-matched.
The large Lab installer exposes a CodeRook legacy attachment-route truncation:
154,025,984 bytes returned from the front of a 616,266,042-byte file. The object
route returns the complete file and matches SHA256 70619364cedf9dff65762ff94b6e8b35586a17e4110237b6fe44444e11b663fa.
DOWNLOADS.md and the draft notes provide that verified route with filename guidance.
Anonymous object access returns401 and anonymous attachment access404. Owner
access succeeds; the public latest remains v2026.10.20. Server scan status is
still checking, not reported clean. Remote draft notes and handoff are read back
exactly. Evidence: coderook-test-readback.json, coderook-full-download-verification.json,
coderook-download-probes.json and coderook-test-handoff-readback.json.

Owner laptop gameplay testing is now the pending next step. No public release,
GitHub source push, CodeRook source mirror or live feed change occurred.