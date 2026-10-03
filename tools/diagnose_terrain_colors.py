#!/usr/bin/env python3
"""
Diagnostic tool to help identify terrain color mismatches.

This compares:
1. What terrain types are actually in the map files
2. What sprites are loaded for each terrain ID
3. What the expected colors should be based on map theme
"""
import sys
import struct
from pathlib import Path
from collections import Counter

def analyze_map_terrain_usage(zoo_path):
    """Analyze which terrain IDs are most commonly used in a map"""
    with open(zoo_path, 'rb') as f:
        data = f.read()

    # Parse map header
    magic = struct.unpack('<I', data[0:4])[0]
    version = struct.unpack('<I', data[4:8])[0]
    width = struct.unpack('<I', data[8:12])[0]
    height = struct.unpack('<I', data[12:16])[0]
    base_terrain = struct.unpack('<I', data[0x20:0x24])[0]
    map_type = struct.unpack('<I', data[0x24:0x28])[0]

    # Decode base terrain nibble
    base_terrain_type = base_terrain & 0x0F
    base_terrain_flags = (base_terrain >> 4) & 0x0F

    # Find terrain data (starts at offset 100)
    HEADER_SIZE = 100
    terrain_start = HEADER_SIZE
    terrain_size = width * height
    terrain_bytes = data[terrain_start:terrain_start + terrain_size]

    # Count terrain types (low nibble only)
    terrain_types = [b & 0x0F for b in terrain_bytes]
    terrain_counts = Counter(terrain_types)

    # Apply base terrain substitution (like the engine does)
    adjusted_counts = Counter()
    for terrain_type, count in terrain_counts.items():
        if terrain_type == 0:
            # ID 0 gets replaced with base terrain
            adjusted_counts[base_terrain_type] += count
        else:
            adjusted_counts[terrain_type] += count

    return {
        'path': zoo_path,
        'width': width,
        'height': height,
        'base_terrain_raw': base_terrain,
        'base_terrain_type': base_terrain_type,
        'base_terrain_flags': base_terrain_flags,
        'map_type': map_type,
        'terrain_counts': adjusted_counts,
        'total_tiles': width * height
    }

def main():
    if len(sys.argv) < 2:
        print("Usage: python diagnose_terrain_colors.py <map.zoo>")
        sys.exit(1)

    zoo_path = sys.argv[1]

    if not Path(zoo_path).exists():
        print(f"ERROR: {zoo_path} not found!")
        sys.exit(1)

    result = analyze_map_terrain_usage(zoo_path)

    terrain_names = {
        0: "Grass",
        1: "Savannah",
        2: "Sand",
        3: "Dirt",
        4: "Rainforest",
        5: "Brown Stone",
        6: "Gray Stone",
        7: "Gravel",
        8: "Snow",
        9: "Fresh Water",
        10: "Salt Water",
        11: "Deciduous Floor",
        12: "Waterfall",
        13: "Conifer Floor",
        14: "Concrete",
        15: "Asphalt"
    }

    print("=" * 80)
    print(f"TERRAIN DIAGNOSIS: {Path(zoo_path).name}")
    print("=" * 80)
    print()
    print(f"Map dimensions: {result['width']}x{result['height']}")
    print(f"Base terrain (raw): {result['base_terrain_raw']} (0x{result['base_terrain_raw']:02X})")
    print(f"Base terrain (decoded): Type {result['base_terrain_type']} ({terrain_names[result['base_terrain_type']]})")
    print(f"Base terrain flags: 0x{result['base_terrain_flags']:X}0")
    print()

    print("=" * 80)
    print("EXPECTED TERRAIN DISTRIBUTION (after base terrain substitution)")
    print("=" * 80)
    print()

    top_terrains = result['terrain_counts'].most_common()
    for terrain_id, count in top_terrains:
        if count == 0:
            continue
        name = terrain_names.get(terrain_id, f"Unknown({terrain_id})")
        pct = (count / result['total_tiles']) * 100
        print(f"  {terrain_id:2d}. {name:20s}: {count:6d} tiles ({pct:5.1f}%)")

    print()
    print("=" * 80)
    print("WHAT YOU SHOULD SEE:")
    print("=" * 80)
    print()

    # Determine map theme based on filename and base terrain
    map_name = Path(zoo_path).stem.lower()
    dominant_terrain = top_terrains[0][0] if top_terrains else 0
    dominant_name = terrain_names.get(dominant_terrain, "Unknown")

    print(f"This map's dominant terrain should be: {dominant_name} (ID {dominant_terrain})")
    print()

    # Expected colors for each terrain type
    expected_colors = {
        0: "Green grass",
        1: "Yellow/tan savannah grass",
        2: "Tan/beige sand",
        3: "Brown dirt",
        4: "Dark green forest floor",
        5: "Brown/tan rock",
        6: "Gray rock",
        7: "Gray gravel",
        8: "White/light gray snow",
        9: "Blue water",
        10: "Dark blue/cyan water",
        11: "Brown/green deciduous floor",
        12: "Blue water (waterfall)",
        13: "Dark green conifer floor",
        14: "Gray concrete",
        15: "Dark gray/black asphalt"
    }

    print("Expected color for dominant terrain:")
    print(f"  {expected_colors.get(dominant_terrain, 'Unknown color')}")
    print()

    if 'tundra' in map_name or dominant_terrain == 8:
        print("THEME: Tundra/Arctic - Should see mostly WHITE/GRAY (snow)")
    elif 'beach' in map_name or dominant_terrain == 2:
        print("THEME: Beach/Desert - Should see mostly TAN (sand)")
    elif 'under' in map_name or dominant_terrain in [6, 7]:
        print("THEME: Underground - Should see mostly GRAY (rock/gravel)")
    elif 'grass' in map_name or dominant_terrain == 0:
        print("THEME: Grassland - Should see mostly GREEN (grass)")
    elif dominant_terrain == 9 or dominant_terrain == 10:
        print("THEME: Water/Ocean - Should see mostly BLUE (water)")

    print()
    print("=" * 80)
    print("IF COLORS ARE WRONG:")
    print("=" * 80)
    print()
    print("1. Check if the DOMINANT terrain is rendering with the expected color")
    print("2. If you see GREEN everywhere but expected a different color:")
    print("   → The terrain ID to sprite mapping might be wrong")
    print("3. If you see correct terrain types but wrong shades/hues:")
    print("   → Palette loading might be incorrect")
    print()

if __name__ == '__main__':
    main()
