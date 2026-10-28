# Marine shows: original-game research and checks

## Completed feeding, interrupted shows and frame profiling — 29 October

The idle dolphin in the user's show tank was an orphaned performer. Cancellation
discarded its home assignment and boxed it at its current location; normal
rehome processing then reopened the capsule in the show tank. Cancellation now
retains each performer's home and portal, releases the specialist and audience,
and runs a return-only phase without requiring a specialist to stand on the
platform. The return state survives saving. A removed/unusable portal leaves a
closed capsule until it is placed in a home tank, preventing the capsule loop.
Old saves with one unambiguous linked home recover unreserved show occupants
through that portal. Multiple possible homes are not guessed.

The earlier food guard prevented new specialist drops but left old piles in
saves. Show classification/restore now removes those piles, and ZooItems update
also rejects show-tank food. Specialist hunger-only visits no longer target show
tanks; healing, cleaning and show rewards remain separate. An extended native
save test exposed a second feeding failure: floor plants around the home ladder
blocked the diver's whole water column. Visiting specialists now pass above
ordinary tank scenery while buildings, filters, bases and land obstacles remain
blocked. Automatic feeding with plants surrounding a ladder is covered by a
full visit test, not just a forced food commit. In the user's zoo after 360 game
seconds the specialist had replenished the home tank (750 food units remained),
the dolphin was healthy with hunger 40, and show-tank food was zero.

Full-game profiling now records 240 visible frames with F9 in Play, including
normal drawing, HUD and frame presentation. It logs the active renderer, guest
count, resolution, zoom and pause state. On this PC the renderer reported
OpenGL, accelerated=1, targetTextures=1 and vsync=1. The installed GPU is an AMD
Radeon RX 6600; Windows' per-process 3D counters recorded 6.1–9.0% activity in
one sample. Simulation and draw preparation still run on the CPU.

In 1904x897 normal Play at 1x, loading the same 102-guest source save measured
28.1 FPS before these changes (35.537 ms mean, 55.101 ms p95). Later captures
measured 52.7 FPS (18.965 ms mean, 22.689 ms p95) and 41.7 FPS (23.978 ms mean,
39.863 ms p95) on the final build. These are short live captures with changing
animation/show phases and machine load, not a deterministic benchmark or a
universal frame-rate guarantee. A separate running 1,000-guest crowd beside
the stands measured 33.7 FPS (29.626 ms mean, 38.695 ms p95). Two earlier crowd
captures affected by award dialogs/pausing were discarded as simulation results.

The largest improvements remove duplicate work: animal draw lists and their
tank mask geometry are prepared once within a world draw, sprites wholly inside
the same mask draw once, and boundary sprites retain exact pixel-column clipping
over only their visible bounds. An empty animal pass exits immediately. Show
readiness and failed scheduled starts are polled once per second, with immediate
rechecking on a frequency change. Attendance readiness is evaluated at its
decision cadence instead of every frame. The first capture's tank-content
render cost fell from 10.106 ms to 1.181 ms; show update fell from 4.524 ms to
0.034 ms. Later live numbers vary with the scene and load.

The real whale-shark artwork matches the exact per-pixel clipping reference at
three renderer scales and all four rotations. Renderer clip/scale restoration,
ladder directions, platform staff, twelve seats, dolphin/orca shows, home food,
show food, cancelled returns, restore and capsule fallback all pass. Latest
counts: management 30/30, show-start 18/18, show-polish 19/19, activity 54/54,
parity 43/43. Native replay completed two further shows with no skipped tricks;
afterward all three dolphins were home and show food remained zero.

Final engine SHA-256:
18A6E311D13BCA7CA7C27881692FB6E67DB620394C48373D8D878C166781DA60.
Reports and raw profile logs are under
E:/zt1-shots/marine-regressions-2026-10-28/; see marine-finished-checks.json.
marine-finished-test.zt1save is a separate native copy verified after saving:
three dolphins in home Tank 1, no animals or food in show Tank 2, and its schedule
retained. The user's source save was not overwritten. Play is paused on that
normal-size zoo. Changes are local; no release, upload or commit was made.

## Tank management follow-up — 29 October

Tank platforms now retain their chosen position while its outside bank is clear.
Connecting a neighbouring tank or covering the approach with a building repairs
the platform onto a clear external wall. Shared tank boundaries, zoo boundaries,
land water and static object/filter footprints are excluded. If no bank is free,
no invalid replacement is created. Habitat > Fences > the gate button also moves
a tank platform manually and rejects obstructed banks. A running show cancels
and resets its schedule if its platform moves; its specialist is released at
the new bank. A working diver abandons the old ladder and replans its next duty.

Tank demolition now tests the opaque pixels of the current object layers at
their rendered height. Mouse-down consumes the same target as the highlight;
old platform/gate hover highlights are cleared in bulldozer mode. In normal Play,
clicking the dolphin hoop's visible rim removed the hoop and retained both tanks.
Clicking transparent space inside a hoop can select the wall behind it.

Marine scenery marked cDeadOnLand is refused on dry land or in an empty tank,
including its full footprint. Sea Grass was refused on grass with the original
water-terrain message and bought in the filled home tank through normal Play.
This refusal is the requested engine policy: the original assets also provide
dead-on-land artwork, so this is not a claim that stock placement always refuses it.
Ordinary specialist food drops are suppressed in show tanks, including a queued
food job; home-tank feeding and scripted show treats remain available.

Grandstands link to the show tank four tiles along their facing, rather than
choosing an arbitrary nearby show. Show admission and all linked stand prices
stay synchronized in both directions; another show's prices remain independent.
The building panel identifies the linked show or asks the player to face one.
Normal Play verified Grandstand 1 changed from $5.50 to $6.00, then Grandstand 6
displayed $6.00 and the same linked Tank 2. Links rebuild from placement on load;
there is no new native save format. Two adults from separate home tanks completed
one shared show and returned to their respective homes in the regression fixture.

Original evidence read through the Lab MCP: 0x59e349 resolves stand facing and
the tile four steps ahead; 0x59e306 maps it to a show; 0x4742ed propagates show
admission to linked stands via 0x474ef2. Guest choice at 0x440f0c ranks stands by
Manhattan distance plus random 0..14, uses maxDistToGoToShow and skips previously
seen shows through 0x5a2bcd. Its AI task decision path is visible at 0x5a2d00.

Audience work now checks urgent needs, existing tasks, distance, price, previous
attendance and seat reservations. Each show considers at most 24 guests and
attempts at most eight routes per second; it stops admitting late in a show.
Reservation counts and occupied seats are collected once per update instead of
repeated guest scans for every candidate. Stand/type data is cached and show
topology refreshes once per second. The 40-distance cap, interest weights and
admission window are engine tuning, not fully recovered original constants.
The 1,000-guest fixture took 0.7043 ms for 240 direct audience-controller updates
with 96 decisions and seven route attempts. This measures controller CPU only;
it does not establish total crowd rendering FPS or eliminate every crowd bottleneck.

Final local build SHA-256:
F378F7E4C1AB64B922E2C2DF332BC5C63C0AC196A98CD92F64745FED543FA1BB.
Checks: show management 22/22, show start 18/18, show polish 19/19, activity
53/53 and parity 43/43. Run showmanagementcheck in a fresh flat ff01 Test Zoo;
it constructs its own disposable fixtures. The twelve-seat rendering/restore
fixture reserves its audience explicitly, independently of voluntary attraction.
All engine edits, builds and gameplay used the Lab MCP. Evidence is
marine-management-final-checks.json in E:/zt1-shots/marine-regressions-2026-10-28/.
Normal Play is paused on management-native-test.zt1save, a separate diagnostic
copy with a relocated platform, $6 linked admission, a purchased Sea Grass and
the test hoop removed. The user's source save remains untouched. No publication.

## Regression follow-up — 28 October

Tank placement now previews floor scenery inside the underwater pass. Floating
show toys use the absolute water surface for both cursor projection and drawing;
their picking height matches their rendered height. Marine adoption and Move
Animal previews use the same initial depth as the placed animal. This removes
the dry-looking preview and the depth jump on purchase/drop. Preview art caches
are cleared when loading another zoo.

The part of a toy above the water now sorts with platforms and front walls;
its submerged artwork stays below the water. Normal Play catalogue selection,
cursor movement and clicks were used to place a dolphin ball and a hoop near
the show platform. The hoop was inspected in all four rotations. The user also
independently confirmed animal, rock and foliage previews render in the water.
Rotation exposed a separate sand strip across the home tank: one tank's
foreground ground repaint included its neighbour's floor. Other tank tiles are
now excluded; the strip disappears in the same native-save view.

Latest checks: **53/53 activity**, **18/18 show-start**, **19/19 show-polish**,
and **43/43 behavior parity**. Evidence is `marine-placement-final-checks.json`
and `platform-toy-final-side0.png` through `side3.png` in the directory below.
`placement-final-test.zt1save` is a separate diagnostic copy with the placed
hoop; the user's source save was not overwritten. The show-status footer was
also inspected in normal Play and fits beneath the trick lists.

The user's dolphin zoo exposed a task-mapping bug: roaming marine specialists
with no exhibit assignments were excluded from shows. Manual assignment was
an initial workaround, not the intended requirement. Readiness, starting and
active validation now accept available roaming specialists automatically,
while respecting explicit assignments to other exhibits. The original native
save starts its scheduled show without assigning staff or issuing `shows run`.
Both adult dolphins performed six tricks with zero skips and returned home.

The second adult was trapped because tank-floor rocks and plants blocked whole
water columns in the navigation index. Underwater swimmers can now route over
scenery; land animals, buildings and equipment retain their blocking rules.
A failed gathering timeout also leaves an animal already in its home tank
unboxed, rather than repeating the transport-capsule cycle in the same spot.

Airborne dolphin artwork was cropped by a canvas anchored to the first frame.
Animation decoding now includes every frame's signed extent and preserves a
common world anchor. A synthetic pixel-preservation regression and the actual
dolphin air-flip viewer cover this case. The show window now has a status footer,
and Never is preserved as a disabled schedule instead of becoming Infrequent.

The show follow-up also covers platform staff, grandstand occupants and
performance-only behavior. The specialist uses the ladder to reach the
platform and descends before being released. Seated guests use cached draw
slots relative to the stand's ground height and sort above its steps. Seats
are recalculated after loading an active show.

Standing specialists now sort immediately after their placed platform art,
outside the tank-content composite. This prevents the board from hiding the
body and leaving only the head visible. Gate highlighting preserves this
layer. The rebuilt Lab fixture was inspected in all four camera rotations:
the full body is visible and the feet sit on the deck. Earlier platform-pass
verification was show-start **16/16**, show-polish **18/18**, activity **52/52**; the report is
`show-platform-final-checks.json` in the directory below. Four screenshots
were displayed through the Lab MCP during verification.

At the user's request, performance artwork is now restricted to a reserved
animal actively performing inside its assigned show tank. This is stricter
than the stock ambient behavior: for example, the original orca `bSurftalk`
includes `surface_spit`. The boundary is derived from the species' declared
tricks, including object tricks; shared feeding, resting and locomotion
remain available. Stale performance queues are discarded outside shows.

`showpolishcheck` in a fresh flat ff01 Test Zoo runs the start checks and
exercises the show behavior boundary, twelve occupied seats, all camera
rotations, active-show state restore, a complete two-orca object-trick
program and a complete two-dolphin plain-trick program. `showpolishdemo`
leaves the fixture paused during performance for visual inspection.

The local build preserves DeepSeek's show-object name-id correction and its
movement rewrite. A snapshot of those changes is retained at
`E:/zt1-shots/marine-regressions-2026-10-28/deepseek-before.patch`.

- A specialist assigned to the show tank is now accepted consistently by
  readiness, starting and active-show validation. An otherwise empty show
  tank accepts a specialist only when its linked home tank has performers.
- Failed scheduled starts expose their actual refusal reason, including a
  busy specialist or an unreachable platform, instead of just waiting forever.
- The tank animal renderer preserves the surrounding device-pixel clip and
  zoom, clips submerged artwork against the current water geometry, and uses
  pixel-width strips. An empty mask intersection draws nothing. Surface/air
  layers remain available for heads, splashes and airborne tricks; large
  sprite strips are drawn once on that pass.
- Removed per-frame clipping/filter debug logs. Restored mixed-habitat shore
  access and water/land height transitions lost in the movement rewrite.

Through the Lab MCP: **16/16 show-start checks**, **52/52 activity/render-state
checks**, **44/44 building/DRT checks**, and **43/43 behavior checks** passed.
The continuous-mode fixture starts, performs, returns its orca home and
releases the specialist; both home-only and show-only assignments work.
The placed orca ball is recognized as an available trick object. This is
targeted regression coverage, not every species/trick combination.

To reproduce: load flat **ff01** with no exhibits in the Test Zoo, then run
`showstartcheck`. It replaces that test fixture and is rejected in normal
Play. It leaves linked tanks, a specialist, an orca, an orca ball, a filter
and a whale shark for inspection. The whale shark is deliberately posed near
the wall for clipping checks; this is not a natural-behavior test. Choose its
`bsubswim` behavior and run time to resume ordinary swimming.

**Tank-wall clipping: PASS, user accepted on 28 October.** The Lab tank was
filled with 16 whale sharks and left swimming at 1x. Each of the four map
rotations was held for more than a real minute (72, 72, 79 and 82 seconds),
with screenshots sampled during each view. No artwork spilling outside the
tank onto the grass was observed, including near the platform and filter.
The user also inspected it and confirmed no clipping. Crowded animal body
overlap remains a separate known issue; this pass covers tank-wall rendering.
Timing and awareness evidence: `whale-shark-four-view-acceptance.json` in
the report directory below. The Lab now holds the show-polish demo, paused
during performance with two orcas and twelve seated guests for inspection.

Reports are in `E:/zt1-shots/marine-regressions-2026-10-28/`. The render fixture
was inspected through the Lab in all four map rotations, including the filter
and platform. Publication remains held; the existing remote tag describes an
older snapshot and no GitHub/CodeRook release was created by these fixes.

The original game is the specification. This work compares the running
Marine Mania executable, its configuration and the engine. It does not
certify the existing marine prototypes as finished animals.

## Original behavior confirmed on 26 October 2026

- A show uses a show tank joined to the animals' home tank through a portal,
  an assigned marine specialist, a selected species and an ordered script.
  A spectator grandstand faces the show tank and is reached from a path.
- The show wall definitions carry `cIsShowFence`. Their animation definitions
  name `objects/portal/idle/idle`. The engine's catalogue already reads the
  flag, but the fence simulation did not retain or use it.
- `shows.cfg` supplies intervals of 65/90/120 for continuous/frequent/
  infrequent, admission $5.50 initially (range $0–$12, step $0.25), maximum
  script length 20 and ideal length 5–7. These are file values, not guessed
  timing constants.
- `loadTricks` (zoo.exe `0x4b9690`) reads each species' trick ID, behavior,
  complexity, satisfaction and repetition penalty, minimum depth, required
  show object, initial availability, visibility and trainer cues. An orca's
  Wave to Crowd needs depth 2; Play with Ball needs depth 8 and object 6502.
  Some tricks require research. The hidden food reward is trick type 3.
- Show script preview (`0x474826`) accounts for repeated tricks and inserts
  food rewards when accumulated complexity reaches `returnToKeeperThreshold`
  (5 in `shows.cfg`). Repeating a trick is not unlimited free satisfaction.
- Trick validation (`0x5a6f96`) checks the current tank's depth and dispatches
  according to trick type. Show tasks take animals through the portal
  (`0x5a3846`), and the animal behavior's `fEndTrick` (`0x5a6a22`) reports
  completion. The controller advances completed actors (`0x5a21c5`).
- `0x5a6894` sends the trick result to spectators.
## Read out of zoo.exe on 28 October 2026

These are the addresses behind the two show defects the engine had. Every one
was read from the binary (`python re/rd.py <addr>`); none is a guess from
behaviour alone.

**An animal can only cross into the tanks it has been given.** The animal
carries a `std::set` of tank ids at `ZTAnimal+0x15c`. `Task85_GoToShowTank`'s
constructor (`0x5a3c4f`) keeps a pointer to that set and stores the task's
class table (`0x634184`); `Task86_ReturnHomeTank` does the same. Both tasks'
`atTarget` accept the tank they are in only if the set holds it -
`showTankSetHas` (`0x5a3585`), called from `0x5a3897` and `0x5a375c`. Having
accepted it, they also require water (`tank+0x188 > 0`), place the animal at
the tank's water level and turn it to face the crowd. The engine now mirrors
this: a performer is standable on its show tank and its home tank and may step
through the edge `Fences::showPortal` names.

- Still open: which function inserts the two tank ids. A linear scan of
  `.text` finds no `+0x15c` base with a small displacement in the show code,
  so the insert is reached with the address in a register. Chase it by
  breaking on `0x5a3585` in the original and reading `animal+0x15c`.

**A trick's required object is matched by `cNameID`.** `0x5a6f96` is not the
general validation the 26 October note implied; it is the trick *object* check
(and the trick-type dispatch). Its guard `0x5a6f50` reads the tank and the
game manager's stage; `0x5a2682` resolves the trick record; then it dispatches
on the trick's type - type 0 to the show manager's slot `0x224`, type 1 (an
object trick) to slot `0x21c` with the trick's `building` id, type 2 to slot
`0x220`. It returns 9 when the trick cannot be played. The tank-depth test is
the compare at `0x5a700e`: the trick's `minimumdepth` (`record+0x58`) against
the tank's water (`tank+0x188`).

`building` is the object's own `cNameID` (`6502` for `objects/orcaball`), which
every placed object has - so reading it only for buildings left every show toy
at 0 and the trick unavailable forever.

**Starting and announcing a show.** `showStartingAnnounce` (`0x5a3de4`) takes
the tank the manager picked, finds the show's record for it (`0x59e216`),
builds message `11491` "A show is starting in %s." from the tank's name at
`+0x154`, then walks the show's animals and checks each with the
characteristics check at `0x6386b0` before it is sent. Newly named animal
fields: `+0x250` on a show, `+0x251` a trick playing, `+0x254` the trick id,
`+0x24f` cleared as the animal leaves the show tank.

 Guest event handling
  (`0x43a698`) selects bad/good/great reactions and the final show thoughts
  10287–10290. `0x51f59b` loads the configurable result thresholds.

## Recorded original tests

The old `lab-show-zoo` save contains a very small home tank. A second orca
was adopted and both tank bases adjusted using the original UI. Unassisted
animals became unhappy and the original displayed an adoption ban. That is
a failed welfare fixture, not evidence that the show system is broken.

A separate controlled experiment temporarily held both animals' happiness
at 100 through the Lab's memory controls. The original then announced the
show, took both animals through the portal, played their tricks and returned
them home. Frame sequences and a function recording demonstrate the cycle.
This isolates show execution; it is not a successful unmodified welfare test.
No original zoo was saved and no original executable was patched.

Evidence is retained locally in
`E:/zt1-shots/marine-shows-2026-10-26`: original configuration and decompilation
reads, the failed fixture, the controlled experiment log, screenshots and
`original-controlled-show.mp4`. Proprietary game assets remain outside Git.

## Engine verification

The engine now has a show controller, editable scripts, scheduled starts,
home/show tank portals, specialist assignments, grandstand spectators and
admission income. This is implemented in the development build; it has not
been included in a new release yet.

The native engine integration report at
`E:/zt1-shots/marine-shows-2026-10-26/engine-checks-d/marine-show-checks.txt`
records **36 checks with zero failures**. It covers the actual show editor,
trick validation (research, depth and required objects), script limits,
repetition penalties, scheduling, paused time, spectator seating, a six-trick
performance, saving and restoring during that performance, animals returning
home, released specialist assignments and admission charged only once.
Draining a tank safely cancels the show. This is engine evidence, separate
from the controlled original-game experiment above.

On 27 October, the latest build also passed **51/51 `activitycheck` checks**
in the Lab Test Zoo. These include the marine portal's opening/closing
timing and held endpoints, plus male and female specialist entry, underwater
work and exit on all four tank sides. The full show report is retained from
26 October; the 51-check regression was rerun after the menu pause fix.

These fixtures establish the tested show workflow. They do not certify every
species/trick combination, every save version, or complete Marine Mania
parity. Original `.zoo` guest restoration and original-format writing remain
separate unfinished save compatibility work.
