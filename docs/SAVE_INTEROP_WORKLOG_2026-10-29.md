Historical initial checkpoint: native v13 and the counts below predate the final v14 work. Current results are in SAVE_RELEASE_EXECUTION_2026-10-29.md (1,709 checks, both healthy soaks, mod matrix and final runtime cycle).

# Save interoperability work log - 29 October 2026

Local development; release remains held. See [the compatibility reference](ORIGINAL_SAVE_COMPATIBILITY.md) for current user behavior, evidence and limits.

## Implemented and checked

- Native version 13 retains the validated original archive. Native versions 1-12 keep their conditional read paths.
- Shared `.zoo` saves contain a newly serialized **current compatible world**, then the engine snapshot and a versioned, bounded, checksummed 36-byte footer. The engine restores its snapshot; the original ignores the extension.
- Native `.zt1save` remains available in the ordinary Save dialog. Shared is the default. Actual format selection, extension changes, switching back and cancellation were exercised in Play.
- Atomic replacement protects the prior file on serialization/write failure. Missing/truncated trailers and CRC damage fail validation. Repeated saves do not recursively embed earlier native sections.
- The original writer handles live world entities, tiles, habitats/tanks, money/date/admission, mapped research, awards, marketing, show programs and finance changes including annual/lifetime totals and year-ring rollover.
- Original show scripts now import into the engine controller. Current native scripts, frequency, admission and visible tricks export to the original.
- Engine-only assets remain in native state. The stock projection omits unique mod entities, uses stock path/fence counterparts, and retains engine-only elevated paths only in native state.
- Fresh base-game and Dinosaur Digs maps exercise older finance layouts; tank-only checks skip explicitly when there is no tank.

## Runtime evidence

All files are under `E:/zt1-shots/save-compatibility-2026-10-29/`.

| File | Verified result |
| --- | --- |
| `marine3-shared-08.zoo` / `original-resaved-shared-08.zoo` | Added dolphin and Drink Stand load and survive a real original-game resave. |
| `show-export-13.zoo` | Original show panel identifies the tank and shows the orca script, Continuous and $5.50. |
| `dolphin-shared-17.zoo` / `dolphin-og-resave-18.zoo` | New Flip/TailWalk program, Frequent, $7.25, visible in original and ordinary engine Play; original resave retains both tricks. |
| `dino-new-31-shared.zoo` / `dino-og-resave-32.zoo` | Newly placed T. rex egg, scientist and reinforced enclosure survive original loading/resaving. |
| `custom-native-33.zoo` | Native read-back has Amber Mossbun and stock zebra; ordinary engine Play renders both with the private test profile. Original loads only stock zebra. Temporary profile ended. |
| `new-filter-37.zoo` / `filter-og-resave-38.zoo` | Real original ZTTankFilter created, facing tank; filter identifier and entity survive original resave. |
| `base-litter-39.zoo` / `base-litter-og-resave-40.zoo` | Original loads and resaves one exported litter pile. Ordinary engine Load reimports it with zero missing records. |
| `engine-reimport-litter-41.zoo` | Ordinary engine Save after that original resave creates a valid shared file; native read-back restores 75x75 world. |
| `blank-shared-46.zoo` / `blank-og-resave-48.zoo` | A wholly blank 75x75 world without an imported archive saves and loads in both formats. Native has zero objects/actors; original has ambient wildlife only and its resave parses. |
| `final-show-42.zoo` / `final-show-og-resave-47.zoo` | Original advances Jan 1 to Jan 6, feeds the orca and retains animal, trainer, tanks and script on resave. Ordinary engine Load reimports 2 habitats, 1 food pile, 1 animal, 1 staff and 7 guests with zero missing records. Welfare popup interrupts timing; not a completed long soak. |
| `engine-only-filter-28.zt1save` / `shared-filter-29.zoo` | Ordinary Save format choice creates the correct native/combined file content and extension. Cancel creates no output. |

The original's resaves strip engine-only data, as expected. A separate original-game copy is essential when preserving engine features.

## Regressions

Four real original saves: **1,199/1,199** (marine3 246; marine2 566; marine 197; show 190). Fresh Dinosaur Digs map **132/132**, base map **90/90**. Total save checks across those six fixtures: **1,421/1,421**.

Activity **56/56**; marine management **30/30** and show polish **27/27**, each latter report also includes **18/18** nested show starts. Parser sweep: 108 files; 105 map sections, 99 complete manager sections, 106,050/106,050 sized entity payloads. Three old versions are rejected by this original too; six patched scenarios stop at thoughts and are not full-state passes.

Native regressions cover world restoration over a 32x32 bootstrap, research/finance, archive retention, visible show scripts/schedules, engine-only elevated decks, corruption/truncation and stable repeated save size. Native map/bootstrap path conversion does not require the original source file at its old location.

Full current reports: `final-checks-43.json`, `final-show-checks-44.json` and `shared-file-validation-45.json` (nine shared files pass independent structure and CRC checks). Lab and Play executables have matching SHA-256 `605BA8BCDCB0A3EF27F1C7AD0C4CE26520F810867CCAF3037407A60092D4A1C5`. The ordinary release executable and previous packages have not been rebuilt from this source. Offline Help builds 21 guides and 9 video entries; the new save reference was opened and visually checked in the running Lab.

## Corrections found by real original tests

- Do not serialize transient ripple/wave objects as persistent scenery.
- Use original guest and food identifiers; restore live dung/litter without a decorative duplicate.
- Fence countdowns are milliseconds and need nonzero periods. Keep south/east owner-tile insets and owner floor Z.
- Original adjacent tanks own separate wall records. Duplicate the shared physical edge for the two owners and normalize a show boundary to original show walls. Native majority-edge show classification alone is insufficient.
- Trick byte two controls visibility, not trick kind. The original schedule hid exported tricks until corrected.
- A real filter is `filter/filter1/f`, from `filter01.cfg`; `filter/other/filter1` parses but is silently dropped by the original. Its facing must point toward its tank.
- Lab native format automation must commit the combo choice with CBN_SELENDOK. Merely selecting its index changes the displayed label without updating the dialog extension.
- No tank on a base/dinosaur map is an explicit skip, not a failed filter import. Older finance tables use admission income for this test because they have no show-income row.

Failed probes remain beside the corrected evidence. Earlier commands using `dialog PATH` are not valid load evidence; use `dialog text PATH`, Windows backslashes, read-back and waits. Earlier filter34/35 and show09-12 probes are failures, not successful runtime coverage.

## Reference and remaining work

RustyZoo reference pinned at `4250545173284cdd4963a47cde7fbf6edf7073b4` (GPL-3.0), stored outside the repo. No Rust source copied, vendored or linked. C++ layout work is independently checked against original executable loaders through the Lab.

Still partial: original behavior/task continuation, thoughts/visited/occupied guest state, active original performances, some private unit/manager timers, source view/non-default zoom conversion and full scenario events/goals. Wider mod and asset coverage remains necessary. No claim of perfect or lossless interchange.

The game/portable/Lab packages predate this save work and the camera follow-up. Rebuild and rerun matching-engine checks, complete isolated installer/update/relaunch tests, resolve or accept intermittent marine crowd stalls and obtain the owner's packaged-Lab acceptance before publication.

All C++ edits, builds, gameplay and original inspection used Lab MCP. Reports and Help were updated locally. Existing performance/user edits were retained. No commit, push, release, updater-feed change or public issue reply was made during this save work.
