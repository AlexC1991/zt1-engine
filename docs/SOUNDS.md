# Sounds

All 668 game sounds (the 8-bit `altsound/` copies left out) are catalogued by
`E:\zt1-shots\tools\sound_catalog.py`: each file, which config keys name it, and
what zoo.exe plays directly. Most are named in the data (animal `[Sounds]`
lists, building `[AmbientSound]` / `[UseSound]`, `crowd.cfg`, `worldsnd.cfg`,
the scenarios' `worldConfig`). This file records the ones nothing names, as
identified by ear by the project owner (2026-10-05), and how the engine plays
sounds.

## How the original plays them (zoo.exe)

- Attenuation in hundredths of a dB: the file's own (an `.ai` `[Sounds]`
  second value), plus the options' master; over 6000 it isn't played.
- On the map: heard from the middle of the view, 2.75 more per screen pixel
  away (10000 at most), panned 6 per pixel.
- Each map's ambience loop: the `.scn` `[start] worldConfig` (e.g.
  `sounds/sea1.cfg` -> `sounds/sea1.wav` at 1000).
- Random bird calls: `worldsnd.cfg` (a 1 in `chance` try each second).
- Menu music `sounds/mainmenu.wav`; no music in a game.

## Identified by ear

| File | What it is | In the engine |
|---|---|---|
| sounds/applsmd.wav | Marine show went well | later (shows) |
| sounds/applssm.wav | Marine show went alright | later (shows) |
| sounds/applsvs.wav | Marine show went somewhat okay | later (shows) |
| sounds/clap.wav | Marine show went really well | later (shows) |
| sounds/boo.wav | Marine show went horribly | later (shows) |
| sounds/ovat.wav | standing ovation (best show, by the score bands) | later (shows) |
| sounds/crwdahh.wav | crowd "ahh" during a marine show | later (shows) |
| sounds/splash3.wav | splashing in a marine show | later (shows) |
| sounds/crwdloop.wav | random crowd noise | later (guests) |
| sounds/biplane.wav, bluejay1, crow1 | fly-overs in the background sky | later (ambient fliers) |
| sounds/brokfenc.wav | a fence breaks | later (fence bashing) |
| sounds/cheats.wav | a cheat code: kept for the dev console's cheats | later |
| sounds/splash.wav | an animal in the water | later |
| sounds/submrg.wav | a marine specialist jumping in from the platform | later (marine) |
| sounds/submrg2.wav | a marine specialist swimming in the tank | later (marine) |
| sounds/tankfill.wav | a tank refilling | yes |
| sounds/tankemt2.wav | a tank draining (before a refill) | yes |
| sounds/tankfil2.wav | tank walls moved up | yes |
| sounds/tankemty.wav | tank walls moved down | yes |
| sounds/zleaves.wav, zwind.wav | beach sounds on the coastal maps | later (check which maps) |
| guests/vofem3.wav | clicking a woman guest | later (guests) |
| guests/vofem2.wav | possibly dropping a guest (unsure) | later (guests) |
| scenery/building/crushus2.wav | an attraction (the crushing machine) | later (buildings) |
| scenery/building/fbatroom.wav | fancy restroom in use | later (buildings) |
| scenery/building/restroom.wav | restroom in use | later (buildings) |
| scenery/building/filter2.wav | the tank filter running | later |
| scenery/building/lwishin2.wav | large wishing fountain | later (buildings) |
| scenery/building/swishin2.wav | small wishing fountain | later (buildings) |
| scenery/building/tofu.wav | the tofu stand in use | later (buildings) |

Still unknown (to check in the running original): sounds/opmax.wav,
sounds/opmin.wav, sounds/subfeed.wav, sounds/tankl.wav, sounds/tanks.wav,
guests/dolridew.wav, guests/surfidle.wav, scenery/building/aquarm2.wav,
scenery/building/jmpfoun2.wav. 39 animal sounds no file names are probably
unused spare takes.
