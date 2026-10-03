# Terrain Rendering Comparison Guide

## Goal
Determine the correct terrain ID to sprite mapping by comparing original ZT1 with our engine.

## Method 1: Memory Inspection (Requires pip install)

1. **Install dependencies:**
   ```bash
   pip install psutil pymem
   ```

2. **Launch original Zoo Tycoon 1:**
   - Run `build/Release/zoo.exe`
   - Load a freeform map (like "under.zoo")
   - Let it fully load and display the map

3. **Run the inspector:**
   ```bash
   python tools/inspect_zt1_process.py
   ```

This will scan the zoo.exe process memory and try to find:
- Terrain sprite path strings in memory
- The terrain array structure (ID → sprite path mapping)

## Method 2: Visual Comparison (Manual but reliable)

1. **Load same map in both engines:**
   - Original ZT1: Load `under.zoo`
   - Our engine: Load `under.zoo`

2. **Take screenshots of BOTH showing the same area**

3. **Compare terrain colors:**
   - Identify what terrain TYPE is at a specific location
   - Compare if the COLORS match between original and our engine
   - If colors don't match, note which terrain ID is showing wrong color

4. **Test with different base terrain IDs:**
   - Test maps with different base terrains:
     - `under.zoo` (base terrain 2 = Sand)
     - `tundra.zoo` (base terrain 6 = Gray Stone)
     - `beach.zoo` (base terrain 1 = Savannah)

## Method 3: Hex Dump Comparison

If the original game has a config file or data structure that defines terrain mappings:

1. Search zoo.exe for terrain sprite references
2. Look for arrays of 16 pointers (one per terrain type)
3. Extract the mapping from the executable

## What We're Looking For

The correct order of sprite directories for terrain IDs 0-15:

```
ID 0 → ??? (currently icgrass, but might be wrong)
ID 1 → ??? (currently icgrs_sv, but might be wrong)
ID 2 → ??? (currently icsand, but might be wrong)
...
ID 15 → ??? (currently icaphalt, but might be wrong)
```

## User Instructions

**Please do ONE of the following:**

### Option A: Run the memory inspector
1. Launch original zoo.exe
2. Load a map
3. Run `python tools/inspect_zt1_process.py`
4. Share the output

### Option B: Manual comparison
1. Load `under.zoo` in ORIGINAL zoo.exe - take screenshot
2. Load `under.zoo` in OUR engine - take screenshot
3. Tell me: Do the dominant terrain colors MATCH?
   - If yes → our mapping might be correct, just colors/palettes off
   - If no → tell me what color you see in original vs ours

### Option C: Check the original game files
Look for any .cfg or .ini files in build/Release that might define terrain types or sprite paths.

Once we have this data, I can fix the terrain mapping definitively!
