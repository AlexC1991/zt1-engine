# Terrain Mapping Test Plan

## Problem
Terrain colors are wrong despite all sprites loading and base terrain nibble decoding working correctly.

## Hypothesis
The sprite directory names (icgrass, icsand, etc.) might not map directly to terrain IDs 0-15 in the order we assume. The original ZT1 engine might use a different mapping.

## Test Maps
1. **under.zoo**: Base terrain ID 2 → Should show mostly SAND (tan/beige color)
2. **tundra.zoo**: Base terrain ID 6 → Should show mostly GRAY STONE or SNOW
3. **beach.zoo**: Base terrain ID 1 → Should show mostly SAVANNAH GRASS or SAND

## Current Mapping (might be wrong)
```cpp
{0,  "terrain/icgrass/icgrass",     "Grass"},
{1,  "terrain/icgrs_sv/icgrs_sv",   "Savannah"},
{2,  "terrain/icsand/icsand",       "Sand"},
{3,  "terrain/icdirt/icdirt",       "Dirt"},
{4,  "terrain/icffloor/icffloor",   "Rainforest"},
{5,  "terrain/icbnrock/icbnrock",   "Brown Stone"},
{6,  "terrain/icgrock/icgrock",     "Gray Stone"},
{7,  "terrain/icgravel/icgravel",   "Gravel"},
{8,  "terrain/icsnow/icsnow",       "Snow"},
{9,  "terrain/icwater/icwater",     "Fresh Water"},
{10, "terrain/icdpwatr/icdpwatr",   "Salt Water"},
{11, "terrain/icfflord/icfflord",   "Deciduous Floor"},
{12, "terrain/icwater/icwater",     "Waterfall"},
{13, "terrain/icfflorc/icfflorc",   "Conifer Floor"},
{14, "terrain/icccrete/icccrete",   "Concrete"},
{15, "terrain/icaphalt/icaphalt",   "Asphalt"}
```

## Possible Issues

### Issue 1: Sprite directories are alphabetically ordered, not ID-ordered
Maybe the original engine loads sprites alphabetically and assigns IDs based on that order?

Alphabetical order of ic* directories:
1. icaphalt
2. icbnrock
3. icccrete
4. icdirt
5. icdpwatr
6. icffloor
7. icfflorc
8. icfflord
9. icgrass
10. icgravel
11. icgrock
12. icgrs_sv
13. icsand
14. icsnow
15. icwater

This doesn't match either - we'd need to test this.

### Issue 2: The sprite directory names are abbreviations that don't match our assumptions

Maybe:
- `icgrs_sv` isn't "Savannah" but something else
- `icsand` isn't at ID 2
- The mapping is completely different

## What to Check

User, can you tell me:
1. What color are you SEEING in under.zoo? (You said wrong colors - what color is dominant?)
2. What color SHOULD under.zoo be? (Expected: tan/beige sand)
3. For tundra.zoo - what do you see vs what should it be?

This will help me figure out if there's a systematic shift in the mappings.

## Next Steps

Once I know what colors you're seeing, I can:
1. Deduce which terrain ID is actually loading which sprite
2. Determine the correct mapping
3. Fix the SpriteDatabase.cpp terrain definitions
