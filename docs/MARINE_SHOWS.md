# Marine shows: original-game research and checks

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
- `0x5a6894` sends the trick result to spectators. Guest event handling
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
