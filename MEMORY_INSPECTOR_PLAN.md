# Memory Inspector Execution Plan

## Prerequisites

Install required Python packages:
```bash
pip install psutil pymem
```

## Step-by-Step Execution

### Step 1: Launch Original Zoo Tycoon 1
1. Navigate to `build/Release/`
2. Run `zoo.exe`
3. **IMPORTANT**: Let the game fully load to the main menu
4. Select "Freeform Games"
5. Choose a map like "An old excavation site" (under.zoo)
6. **Let the map FULLY load and display on screen**
7. **Leave the game running** - don't close it!

### Step 2: Run the Memory Inspector

Open a NEW command prompt/terminal (keep zoo.exe running), then:

```bash
cd C:\Users\batty\OneDrive\Documents\GitHub\zt1-engine
python tools\inspect_zt1_process.py
```

### Step 3: What the Tool Will Do

The inspector will:
1. Find the running zoo.exe process
2. Attach to it (read-only, won't crash the game)
3. Scan memory for terrain-related strings like:
   - `terrain/icgrass`
   - `terrain/icsand`
   - `terrain/icdirt`
   - etc.
4. Try to find the terrain array (16 pointers mapping IDs 0-15 to sprite paths)
5. Print results showing what it found

### Step 4: Interpret Results

The tool will output something like:

```
Found terrain-related strings in memory:
  'terrain/icgrass':
    0x12345678
  'terrain/icsand':
    0x12345690
  ...

POTENTIAL TERRAIN ARRAY FOUND at 0x12340000:
  [0] terrain/icgrass/icgrass
  [1] terrain/icsand/icsand
  [2] terrain/icgrs_sv/icgrs_sv
  ...
```

This will tell us the ACTUAL order the original game uses!

### Step 5: If It Doesn't Find the Array

If the tool can't find the array automatically, try:

1. **Load a DIFFERENT map** in zoo.exe (maybe data gets loaded differently)
2. **Run the inspector again**
3. Or **manually search** the output for patterns

### Step 6: Alternative - Manual Memory Search

If the Python tool doesn't work, we can use a different approach:

1. Use a tool like **Cheat Engine** or **Process Hacker**
2. Attach to zoo.exe
3. Search memory for the string "icgrass" or "icsand"
4. Look for arrays of consecutive string pointers

## Expected Outcome

The tool should reveal one of:

**Scenario A: Different order**
```
Original ZT1 uses:
  [0] icgrass  ← matches our assumption
  [1] icsand   ← DIFFERENT! We have icgrs_sv
  [2] icdirt   ← DIFFERENT! We have icsand
  ...
```

**Scenario B: Same order but different names**
```
Original ZT1 uses completely different sprite paths
```

**Scenario C: No array found**
```
The game might load sprites dynamically or use a different structure
```

## Troubleshooting

### "Permission denied" error
- Run the command prompt as **Administrator**

### "zoo.exe not found"
- Make sure zoo.exe is actually running (check Task Manager)
- The game needs to be FULLY loaded (not just starting up)

### "No terrain data found"
- Load a map in the game first (main menu doesn't load terrain)
- Try loading a different map
- The game might use dynamic loading - try zooming around the map

### Python packages won't install
```bash
# Try with explicit pip path:
python -m pip install psutil pymem

# Or if you have multiple Python versions:
py -3 -m pip install psutil pymem
```

## What to Send Me

After running the tool, send me:
1. **The complete output** from the inspector
2. **Any "POTENTIAL TERRAIN ARRAY FOUND"** sections especially
3. **What map you had loaded** when you ran it

If the tool finds the array, we'll immediately know the correct terrain mapping and can fix it!

## Backup Plan

If the memory inspector doesn't work, we can fall back to:
1. Visual comparison (screenshot matching)
2. Hex editor analysis of zoo.exe
3. Decompiling/disassembling parts of zoo.exe (more advanced)

But the memory inspector should be the fastest way to get the answer!
