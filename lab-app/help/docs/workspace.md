# Find your way around the Lab

## Explore: find what you need

Try a plain question such as “selling animals”, “tank filter” or “where is the zoo saved”. Results connect functions, recorded actions, game configuration and engine source. Open a result's code or its game files to investigate further. Rebuild the map after adding recordings or naming functions if Explore says its map is stale.

## Original Game: understand a behavior

Find a function on the left, read its decompiled code in the middle and use the details on the right to name it or record what you learned. Search names, game text, an address, or a field such as `field:0x3d6`. Ghidra works from your own executable. Decompiled code and guesses still need checking against observed behavior.

## Write Code: change the engine

Open the engine file, make the change and choose **Build engine**. Read the Build output and Problems list. A successful build restarts the Test Zoo on the new engine. Keep a reproducible test setup before rebuilding. A normal art or needs change belongs in Mods and usually does not require an engine build.

## Test Zoo: make a controlled experiment

Pause, select a map and place a small set of animals or objects. Use the Inspector to inspect the selected thing, then run or advance time. Compare a known working stock item with your custom item in similar conditions. Use the outline to find an animal, enclosure or building again.

## Play Game: check what a player experiences

**Play ZT1 Engine** runs the current engine. **Play original** runs the installed original game for comparison. Stop and start Play after changing loaded mods. Embedded Play is a separate game session from the Test Zoo: placing an animal in one does not place it in the other.

## Mods: use the right workspace

The studio lists a mod's animals, items, maps and files. Choose an entry to edit it and use the Inspector for its properties. **About** holds the name, description, version and cover. **Publish** holds checks, upload and theme-pack creation. The Mods workspace also provides **Game files** and **Lab scripts**; those are different from a playable mod's drawings and definitions.

## Restore a hidden panel

In Original Game, Write Code and Test Zoo, use **Layout** to bring back a closed panel or reset the tab's layout. Drag panel tabs onto a side to dock, into the middle to stack, or out to float. Drag edges to resize. A saved layout is remembered between sessions.

## Sharing findings

The header's **Sharing** control manages knowledge-base contributions. Review its dialog before turning it on. Findings about the executable are separate from publishing a mod. Your private recordings and zoos are not the mod repository.

[Read the complete Lab reference](help:lab-reference) for the detailed inspectors, traces, formulas and recording tools.
