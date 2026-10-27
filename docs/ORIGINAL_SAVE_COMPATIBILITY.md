# Original save compatibility - 27-28 October 2026

The goal is two-way exchange with Zoo Tycoon. Original saves can already be
imported, but this is still a partial state conversion. The engine does not
currently export an original-format save. Do not interpret a successful
parse or a renamed extension as full compatibility.

## What the actual original game accepts

Tests used the Lab's Play Original and copies of `lab-sweep-marine3.zoo`.
The executable identifies itself as Marine Mania / Dinosaur Digs version
12.04.00.0009 and reads save versions through 106.

| File contents | Test name | Original game result |
| --- | --- | --- |
| Native engine `S1TZ`, version 11 | `native-renamed.zoo` | Rejected: "Not a Microsoft Zoo Tycoon saved game." |
| Original `TZFB`, version 106 | `original-renamed.zt1save` | Loaded: 125 by 125 map, two animals, three staff, five guests, cash 121622.43, 20 February 2001. |
| Same original save plus a 29-byte probe block | `original-extra-tail.zt1save` | Loaded with the same starting cash, date and live entity counts. |
| Original game's own resave of that last file | `original-resaved.zoo` | Valid `TZFB` version 106; the appended probe was discarded. |

The file picker can accept a different extension when its full name is
selected or entered. The loader still requires an original `TZFB` body.
The engine's load dialog now also offers **All files** and identifies the
actual format from the file contents.

The trailer test is a container experiment on an unchanged original save.
It is not an export of an edited engine zoo and must not be described as one.
The original cannot preserve engine-only trailer data when it saves again.

## Import changes in this development build

- Restore original guests instead of discarding the decoded records. Keep
  their ids, kinds, names, positions, clothing colours and five status values.
  Rebuild their tasks using the engine AI; do not charge admission again.
- Preserve the original young animal subtype instead of adopting every
  animal as an adult.
- Clear stale reader state when a later load fails, so a previously loaded
  zoo cannot masquerade as the failed file's game state.
- Retain saved fence wear.
- Restore original tank filters as live, serviceable equipment, with their
  original name, grid position and health, rather than decorative objects.
- Make newly converted native saves independent of the original file's
  absolute path. Their complete world snapshot loads over a stock bootstrap;
  saved dimensions, zoo boundaries and the guest entrance are restored.
- Preserve original identities, animal lifetime/status and saved timers,
  guest inventory/favourite species/arrival and need timers, and staff
  appearance, duties and mapped assignments. Runtime behavior/task stacks
  are rebuilt rather than resumed byte-for-byte.
- Restore mapped research funding/categories/progress, awards and marketing.
- Import monthly finance history using the original Finance panel's row
  mapping and expense signs. Preserve original annual/lifetime tables in
  native saves without inventing older monthly rows.
- Write native format **12** to retain this additional state through a full
  file round trip. Older native versions retain conditional read paths;
  older released engines cannot read new version-12 saves.

These changes use the layouts checked in the original executable:
`ZTGuest::load` at `0x455fa2`, `ZTAnimal::load` at `0x455533`,
`ZTTankFilter::load` at `0x45d468` and its state selection at `0x45d234`.
Fence condition is checked against `ZTFence::load` at `0x44ff2d` and its
refund calculation at `0x4f945b` (life at `+0x160`). The format gate is
`loadZoo` at `0x453000`. No original executable file was modified.

## Verification

All game interaction, source edits and builds used the Lab MCP. The current
build passes **1,131/1,131 original-save checks**:

| Original save | Imported guests | Checks |
| --- | --- | --- |
| `lab-sweep-marine3` | 5 | 229/229 |
| `lab-sweep-marine2` | 37 | 549/549 |
| `lab-sweep-marine` | 0 | 180/180 |
| `lab-show-zoo` | 0 | 173/173 |

The guest and animal comparisons use each real save's decoded records.
The damaged-fence and worn-filter checks use controlled record fixtures
inside the Lab, not a claim that all four saves contain worn pumps. The
native world-snapshot check deliberately loads the saved geometry over a
different-size 32 by 32 bootstrap. An imported visitor also resumed walking
on a valid route when simulation time advanced.

The retained parser sweep reads 108 available `.zoo` files: 105 map sections read in
the original's order, 99 complete manager sections, and all **106050 entity
payloads** decode to their exact stored sizes. Three old scenario copies
use versions this original executable itself rejects. Six patched scenario
copies still stop at their thoughts section; their terrain/entities parse,
but that is not a full game-state pass.

Normal Play opened the renamed original through its ordinary Load dialog,
saved the imported zoo in native version 11, then loaded that native copy.
The Guest List still showed all five visitors. A second native save,
`portable-import.zt1save`, records `freeform/ff01.scn` as its bootstrap.
With the copied original source temporarily moved away, that file still
loaded the correct 125 by 125 zoo, 9735 inside-zoo tiles, entrance at 12,73,
five guests and the saved cash/date. The copied source was then restored.

The building/gate/diver regression also passes **51/51**, building/DRT
**44/44** and the behavior suite **38/38**. The full v12 file checks also
compare research and monthly/annual finance state. Native reports and
screenshots are kept in `E:/zt1-shots/non-scenario-audit-2026-10-28/`.
The earlier version-11 portable test described above remains historical
evidence; it is separate from the new full v12 round trips.

The original marine save, the player's `Something.zt1save`, normal mod
switches and `zoo.ini` retain their initial SHA-256 values. Play and Test Zoo
are left paused on the imported fixture. These source fixtures already have
unhappy animals; their welfare/adoption warnings also occurred in the
original and are not evidence of a new import loss.

## What remains incomplete

| State | Current coverage or gap |
| --- | --- |
| Terrain, paths, scenery, buildings and fences | Existing map restoration; wear now retained. Validate less common object kinds and mod definitions separately. |
| Habitats and tanks | Names, bounds, tank heights, fill, salt and purity are restored. Original pre-excavation ground is not available as the engine's full ground-restore snapshot. |
| Animals | Identity, lifetime and mapped life/status/need timers, sex, crate/egg/escape state and young subtype. Original behavior stacks and every disease/breeding transition are not restored exactly. |
| Staff | Identity, appearance, duties and mapped assignments/status restored. Original task stacks and exact in-progress job continuation remain incomplete. |
| Guests | Identity/appearance/needs, inventory, favourite species, arrival, mapped timers and leaving state restored. Original thoughts, visited-exhibit interpretation, routes and occupied building/show seats remain incomplete. |
| Money and date | Cash, admission, rating, date and rolling monthly finance rows restored. Annual/lifetime tables are retained in native saves; older monthly detail is not reconstructed or presented as restored. |
| Shows | Original records are consumed by the reader, but saved scripts/schedules, active performances and spectators are not restored into the new show controller. |
| Research, goals, awards and marketing | Mapped research funding/categories/progress, awards and marketing restored. Full scenario goals/events remain unfinished. |
| View | Existing saved-view handling; non-default original zoom needs more work. |
| Original-format writing | Not implemented. Renaming a native save cannot replace it. |

The new reader retains native versions 1–11 and writes version **12**.
Back up saves before acceptance testing: a version-12 save is not a rollback
file for v2026.10.20, and renaming it cannot make it one.

## Route to two-way compatibility

1. Finish the missing import state and keep unknown original records/fields
   available for lossless round-trip work. Use original save fixtures from
   base Zoo Tycoon, Dinosaur Digs and Marine Mania, plus saves containing
   active jobs, shows, research, damaged equipment and mod content.
2. Implement an explicit original-format writer for the **current** engine
   world: header/version, habitats/shows, tile grid, sized entity records,
   view/game manager, finance/date, thoughts/events, research, awards,
   marketing and clocks. Reusing an old original snapshot would silently
   lose the player's later edits and is not a valid export.
3. Validate supported features and required mod assets before export. New
   engine features without an original-game equivalent need a specific
   conversion or an honest unsupported-feature report.
4. Verify every exported fixture in the unmodified original via the Lab,
   save it there, then import that result back into the engine and compare
   the world and manager state.
5. A single-file variant could add a versioned, bounded, checksummed native
   extension after the original-compatible body. The tested executable
   ignores an extra tail, but older executables still need verification.
   Native-only data must remain optional because an original-game resave
   discards it. A separate native save should remain available.

Evidence files and native screenshots are retained in
`E:/zt1-shots/save-compatibility-2026-10-27/`. Original save files are not
overwritten. No release or deployment is part of these tests.
