# Original save compatibility - 29 October 2026

The local development build can import original saves and write the current
edited zoo as an original-compatible `.zoo`. The same file also contains the
engine's full native snapshot. This has been tested in the Lab's real original
game and ordinary engine Play; it is not yet a claim of complete save parity.
Publication remains held for the release gates below.

## Choose a save format

The ordinary **Save a zoo...** dialog offers:

| Choice | Contents | Intended use |
| --- | --- | --- |
| Shared zoo (`.zoo`, default) | Original TZFB world, followed by the native version-14 snapshot and a validated footer | Open the compatible world in the original, or restore the engine snapshot in ZT1 Engine. |
| Engine-only save (`.zt1save`) | Native S1TZ version 14 | Continue in ZT1 Engine without producing an original-compatible world. |

Selecting a format updates the default extension for a name without one.
**Load** accepts both formats, individual filters and **All files**. Contents
determine the loader: renaming a native file does not convert it.

**Keep a separate copy before saving in the original game.** The original
ignores the appended engine snapshot when loading, but discards it when saving.
That original resave can be imported again; engine-only content from the earlier
file cannot be recovered from the original resave.

New native saves use version **14**. Read paths for native versions 1-13 remain;
older released engines cannot read a new version-14 snapshot. Retain old backups.
Version 14 stores a guest's favourite species by definition file and keeps recent
viewed exhibits, so registry reordering does not change the saved preference.
Custom asset definitions and art still need their mods installed and enabled in
the engine: the snapshot stores zoo state, not a bundle of mod resources.

## What goes into the original-compatible world

The writer serializes the **current** terrain, paths, boundaries, habitats,
tanks, objects, animals, guests, staff, food, dung, litter and filters. It also
writes mapped money/date/admission, finance, research, awards, marketing and show
programs. Added content and later edits are not replaced by an old zoo snapshot.

Unique definitions supplied only by engine mods remain in the native section.
The compatible section omits their animals, staff, objects, filters and food.
Custom paths and fences use a stock equivalent; custom ground uses stock grass
where necessary. Generic engine dung on a submerged tank floor also remains
native-only: the original stock dung definition cannot be placed underwater
and otherwise prevents filling. Dung on dry ground or a tank's dry bank still
exports. A mod replacing a stock definition can use that definition's
stock counterpart in the original. Elevated engine paths remain native-only.
This is a reduced compatible world, not identical mod gameplay in both engines.
The Amber Mossbun plus stock zebra test verifies the basic unique-animal case;
arbitrary old fan mods and every theme-pack combination are not certified.

The receiving original must have the necessary stock assets/expansions. Runtime
tests here use Marine Mania / Dinosaur Digs **12.04.00.0009**, which reads save
versions through 106. They do not establish base-only executable acceptance of
Marine Mania content. Older archives needing tanks or shows are promoted to a
stock Marine Mania template; the original source archive remains in the native
snapshot. Fresh blank zoos use a stock template as well.

## State coverage and limits

| State | Current coverage and remaining limits |
| --- | --- |
| World and boundaries | Live tiles, paths, objects, fences and tank walls. Correct owner tiles, original condition periods and shared tank edges. Less common asset/geometry combinations need wider testing. |
| Animals | Identity, sex, young/egg/crate state, mapped lifetime/status and need timers. Original AI stacks and every in-progress disease/breeding transition are not resumed exactly. |
| Staff | Identity, appearance, duties and mapped assignments. Original route/task stacks rebuild rather than resume byte-for-byte. |
| Guests | Names, appearance, needs, inventory, favourite species, arrival, mapped timers and leaving state. Original plain, named-unit and habitat thoughts restore newest-first. Recent original visits use grid coordinates; recent species use stock name IDs. The engine's bounded recent-history sets do not preserve exact original visit ordering. Original occupied seats/buildings and in-flight routes rebuild. Native shop users safely replan; show spectators retain their controller state. |
| Money and date | Live cash/date/admission/rating; mapped monthly changes update annual and lifetime tables with the original row signs and calendar ring. Older monthly detail is not invented. Categories absent from an older format cannot be represented there. |
| Shows | Import scripts, species, frequency and admission into the engine controller; export current stock-compatible scripts and visible tricks for the original. Replaced species use the stock species ID. Programs with custom behavior or required-item definitions remain native-only. Active original performance progress and spectators rebuild rather than resume exactly. |
| Filters | Real serviceable equipment, position, facing, health and last/current/lifetime costs. Construction month is mapped. Native saves retain decay and cleaning countdowns; the original format does not serialize those countdowns and resets them on load. Exported pump survives an original resave. Exact private viewed date/construction day remain partial. |
| Managers | Mapped research, awards and marketing. Unknown manager/unit data is retained where possible; stale original event queues are cleared on live export. Full scenario events/goals remain unfinished. |
| View and ground | Current camera centre and all four rotations convert both ways at original half/normal/double zoom. Arbitrary native zoom is retained natively and quantized to an original-supported zoom for interchange. Native pre-tank ground snapshots remain exact; original-only imports have no pre-excavation snapshot, so demolition uses surrounding ground height and grass. Original tank floors and terrain are retained. |

The original archive is bounded and reparsed when read from native state. The
combined file has a versioned 36-byte `ZT1EXTRA` footer, native offset/length and
CRC32 for each stream. Missing/truncated trailers and damaged streams fail
validation instead of silently loading a reduced zoo. The archive excludes the
native extension, preventing recursive growth on repeated saves. Writes use a
temporary file and atomic replacement. These checks do not make unknown fields
fully understood or promise lossless original AI continuation.

## Verified through the Lab

All game controls, C++ edits, builds and original executable inspection used the
Lab MCP. Files below are copies under
`E:/zt1-shots/save-compatibility-2026-10-29/`; source saves were not overwritten.

| Test | Observed result |
| --- | --- |
| `marine3-shared-08.zoo` | Original loads a newly placed dolphin and Drink Stand: 3 animals, 11 buildings, 5 guests, 3 staff, 92 paths, 62 tank walls. Original resave retains the additions. |
| `show-export-13.zoo` | Original identifies the show tank and displays Wave to Crowd, Spray Water, Continuous and $5.50. |
| `dolphin-shared-17.zoo` | Engine and original display the new Flip/TailWalk program, Frequent and $7.25. `dolphin-og-resave-18.zoo` retains both tricks. |
| `dino-new-31-shared.zoo` | Newly placed T. rex egg, scientist and reinforced enclosure load in the original. `dino-og-resave-32.zoo` retains the young animal and scientist. |
| `custom-native-33.zoo` | Native snapshot restores Amber Mossbun and stock zebra; ordinary engine Play renders the custom animal with its temporary test mod. Original loads the same file with just the stock zebra. Normal mod switches are untouched. |
| `new-filter-37.zoo` | Original creates a real ZTTankFilter, not decoration. `filter-og-resave-38.zoo` retains `filter/filter1/f`; screenshot `original-filter-38.png` shows it facing the tank. |
| `base-litter-39.zoo` | Original loads exported litter. `base-litter-og-resave-40.zoo` retains it, and ordinary engine Play reimports one live litter pile with no missing records. |
| `blank-shared-46.zoo` | A wholly blank 75x75 Lab world saves without an imported archive. Native read-back retains zero placed objects/actors; the original loads the blank terrain with ambient wildlife. `blank-og-resave-48.zoo` parses completely. |
| Save dialog | Shared default, engine-only selection, switching back, extension changes and cancellation verified. `engine-only-filter-28.zt1save` is native; `shared-filter-29.zoo` contains both streams. |

The expanded save/import regression passes **1,431/1,431** across four real original saves:

| Fixture | Checks |
| --- | --- |
| `lab-sweep-marine3` | 294/294 |
| `lab-sweep-marine2` | 678/678 |
| `lab-sweep-marine` | 234/234 |
| `lab-show-zoo` | 225/225 |

Fresh Dinosaur Digs and base-game maps pass another **160/160** and **118/118**,
for **1,709/1,709** total (`final-save-regression-159.json`).
Tank-specific checks are explicitly skipped on maps without a tank. Financial
checks use admission income for older formats that lack the show-income row.
Each fixture also checks twelve view combinations, missing definitions, failed
replacement, malformed/truncated state, and native reload after tank demolition.
Normal Load validates a separate world and retains the current zoo on failure;
missing content is named. Original workers caught on ladders or inside exhibits
restart on accessible ground instead of being silently lost or rejecting the zoo.
Coverage includes full native files, a different-size bootstrap, show scripts,
current finance transactions/year rollover, engine-only decks, CRC failures,
truncated/missing footers and stable repeated saves.

The retained parser sweep covers 108 available files: 105 map sections, 99 full
manager sections and **106,050/106,050** exactly decoded entity payloads. Three
old scenario versions are rejected by this original executable too; six patched
scenario copies still stop at thoughts. This is parser coverage, not runtime
acceptance of every zoo. Activity checks pass 77/77, behavior/parity 57/57 and
building/DRT 55/55. Marine management previously passed 30/30; show polish passes
39/39 with 18/18 nested show-start checks. Full native/shared file reloads cover
show gathering/performance/return, four specialist climb stages, pump service,
hatching, sleep, illness, a shop visit, helicopter flight and darted-dinosaur
containment. Supported controllers resume; other tasks safely replan.

Retained native files from versions 7, 8, 10, 11 and 12 load. Some old Lab
fixtures used placeholder `road` paths and `grass` chow; known legacy keys map
to stock equivalents if their own definitions do not exist. The old original
import's generic `food` key is corrected using its exhibit's occupants. These
targeted migrations do not bypass missing custom definitions in current saves.

The exported marine fixture `final-show-42.zoo` ran in the original from January
1 to January 6; its specialist supplied food and `final-show-og-resave-47.zoo`
retains the orca, trainer, tanks and script. The fixture's welfare warning
interrupted the wall-clock interval; this is not a completed long soak.
Ordinary engine Load then restores two habitats, one food pile, the orca, its
trainer and seven guests from that original resave, with zero missing records.
`final-checks-43.json` records all six save suites and the repeated activity/show
checks. `shared-file-validation-45.json` records nine file structure/CRC checks.

## Reference and release gates

SlendyMilky's [follow-up](https://github.com/AlexC1991/zt1-engine/issues/5#issuecomment-6443994223)
pointed to RustyZoo's versioned reader/writer. The local reference is pinned to
`4250545173284cdd4963a47cde7fbf6edf7073b4`. Its files are GPL-3.0; no Rust source
was vendored, copied into or linked with the engine. Independent C++ serialization
was checked against the original loaders and actual original-game load/resave.
Preserving unknown bytes is distinct from decoding or restoring their meaning.

Before public release, rebuild the packages from this final source, repeat the
matching-engine checks, test installation/upgrade/updater relaunch in isolation,
and complete the owner's packaged Lab test. Include stock land, marine/show and
dinosaur zoos plus a modded zoo, new filenames, original resaves and backups.
Keep scenario parity, exact original AI continuation and wider
mod/asset coverage open. The older package inventory does not include this work.
