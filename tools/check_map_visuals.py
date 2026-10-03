#!/usr/bin/env python3
"""
Check what maps ACTUALLY look like by examining their names and terrain.
Some maps have misleading byte 5 values.
"""
import struct
from pathlib import Path
from collections import Counter
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

TERRAIN_NAMES = {
    0: "Grass", 1: "Savannah", 2: "Sand", 3: "Dirt", 4: "Rainforest",
    5: "Brown Rock", 6: "Gray Rock", 7: "Gravel", 8: "Snow", 9: "Fresh Water",
    10: "Salt Water", 11: "Deciduous", 12: "Waterfall", 13: "Coniferous",
    14: "Concrete", 15: "Asphalt"
}

def analyze_map(map_path):
    with open(map_path, 'rb') as f:
        data = f.read()

    if len(data) < 100 or data[0:4] != b'TZFB':
        return None

    name = map_path.stem
    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    base_terrain = data[0x20] & 0x0F
    map_type = struct.unpack('<I', data[0x24:0x28])[0]

    terrain_counts = Counter()
    biome_counts = Counter()

    for i in range(width * height):
        offset = 100 + i * 10
        if offset + 10 > len(data):
            break
        terrain = data[offset] & 0x0F
        actual = terrain if terrain != 0 else base_terrain
        terrain_counts[actual] += 1
        biome_counts[data[offset + 5]] += 1

    dominant_terrain = terrain_counts.most_common(1)[0][0]
    dominant_biome = biome_counts.most_common(1)[0][0]

    return {
        'name': name,
        'base': base_terrain,
        'map_type': map_type,
        'dominant_terrain': dominant_terrain,
        'dominant_biome': dominant_biome,
        'terrain_counts': terrain_counts,
        'biome_counts': biome_counts,
    }

def main():
    maps_dir = ZT_PATH / "maps"

    # Maps where we know what they should look like
    known_maps = {
        'tundra': "WHITE - Snow/ice tundra",
        'under': "GRAY - Underground subway",
        'beach': "TROPICAL - Beach/palm trees",
        'default': "GREEN - Standard grass zoo",
        'arcmaze': "DARK GREEN - Rainforest maze (NOT snow!)",
        'crater': "MIXED - Various terrain in crater",
        'mars': "RED/BROWN - Mars surface",
        'tropical': "DARK GREEN - Tropical jungle",
        'savannah': "YELLOW-GREEN - African savannah",
        'island': "MIXED - Water + land island",
        'highland': "GREEN/WHITE - Scottish highlands (some snow)",
        'lunar': "GRAY - Moon surface",
        'lavaland': "BLACK/RED - Volcanic",
    }

    print("=" * 100)
    print("MAP VISUAL EXPECTATIONS vs BYTE 5 VALUES")
    print("=" * 100)
    print()
    print(f"{'Map':<15} {'Expected Look':<30} {'Base Terrain':<15} {'Byte5':<8} {'Match?'}")
    print("-" * 100)

    for map_name, expected in known_maps.items():
        map_path = maps_dir / f"{map_name}.zoo"
        if not map_path.exists():
            continue

        result = analyze_map(map_path)
        if not result:
            continue

        base_name = TERRAIN_NAMES.get(result['base'], "?")
        biome = result['dominant_biome']

        # Check if byte 5 makes sense
        match = "?"
        if "WHITE" in expected or "SNOW" in expected.upper():
            match = "YES" if biome == 8 else "NO - should be 8"
        elif "GRAY" in expected and "Underground" in expected:
            match = "YES" if biome == 14 else "NO - should be 14"
        elif "TROPICAL" in expected or "BEACH" in expected:
            match = "YES" if biome == 10 else "NO - should be 10"
        elif "GREEN" in expected and biome in (0, 2, 3, 4, 5, 6):
            match = "OK - no overlay needed"
        elif "RAINFOREST" in expected.upper():
            match = "PROBLEM!" if biome == 8 else "OK"

        print(f"{result['name']:<15} {expected:<30} {base_name:<15} {biome:<8} {match}")

    print()
    print("=" * 100)
    print("CONCLUSION: Byte 5 may NOT be 'biome overlay' for all maps!")
    print("=" * 100)
    print("""
Key Observations:
1. arcmaze has byte 5 = 8 (Snow) but it's a RAINFOREST maze - NOT snowy!
2. This means byte 5 might be:
   - A 'climate zone' for animal compatibility (animals need certain climates)
   - NOT a visual overlay in all cases

The byte 5 value might indicate:
  0 = Normal zone
  8 = Cold/Tundra zone (for animal climate needs, not necessarily visual snow)
  10 = Tropical zone (for animal climate needs)
  14 = Indoor/City zone

Visual rendering should probably ONLY change for maps that have:
- base_terrain = gray rock/snow AND byte 5 = 8 -> show snow
- byte 5 = 14 AND map is clearly underground -> show city
""")

if __name__ == '__main__':
    main()
