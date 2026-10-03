#!/usr/bin/env python3
"""
Verify terrain ID to sprite mapping by analyzing actual map files
and comparing with what sprites are loaded for each terrain type.

This helps identify if we're loading the right sprites for the right terrain IDs.
"""
import sys
import struct
from pathlib import Path
from collections import Counter

def analyze_zoo_terrain(zoo_path):
    """Analyze terrain usage in a .zoo map file"""
    print("=" * 80)
    print(f"ANALYZING MAP: {zoo_path}")
    print("=" * 80)
    print()

    with open(zoo_path, 'rb') as f:
        data = f.read()

    # Find MAP section
    map_pos = data.find(b'MAP ')
    if map_pos == -1:
        print("ERROR: No MAP section found")
        return None

    # Read map dimensions
    width = struct.unpack('<I', data[map_pos + 8:map_pos + 12])[0]
    height = struct.unpack('<I', data[map_pos + 12:map_pos + 16])[0]

    print(f"Map dimensions: {width}x{height}")
    print()

    # Find terrain data (after dimensions)
    terrain_start = map_pos + 16
    terrain_size = width * height
    terrain_bytes = data[terrain_start:terrain_start + terrain_size]

    if len(terrain_bytes) != terrain_size:
        print(f"WARNING: Expected {terrain_size} terrain bytes, got {len(terrain_bytes)}")
        return None

    # Analyze terrain IDs (low nibble = terrain type 0-15)
    terrain_ids = [b & 0x0F for b in terrain_bytes]
    terrain_counts = Counter(terrain_ids)

    print("TERRAIN TYPE USAGE IN THIS MAP:")
    print("(ID = Low nibble of terrain byte, should map to terrain sprite)")
    print()

    # Map terrain IDs to expected terrain names
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

    for tid in range(16):
        if tid in terrain_counts:
            count = terrain_counts[tid]
            pct = (count / len(terrain_ids)) * 100
            name = terrain_names.get(tid, f"Unknown{tid}")
            print(f"  ID {tid:2d} ({name:20s}): {count:6d} tiles ({pct:5.1f}%)")

    print()

    # Find what the dominant terrain types are
    top_terrains = terrain_counts.most_common(5)
    print("TOP 5 TERRAIN TYPES IN THIS MAP:")
    for tid, count in top_terrains:
        name = terrain_names.get(tid, f"Unknown{tid}")
        pct = (count / len(terrain_ids)) * 100
        print(f"  {name:20s} (ID {tid:2d}): {count:6d} tiles ({pct:5.1f}%)")

    print()
    return terrain_counts

def compare_maps(zoo_files):
    """Compare terrain usage across multiple maps"""
    print("\n")
    print("=" * 80)
    print("CROSS-MAP TERRAIN USAGE COMPARISON")
    print("=" * 80)
    print()

    all_results = {}
    for zoo_file in zoo_files:
        if Path(zoo_file).exists():
            result = analyze_zoo_terrain(zoo_file)
            if result:
                all_results[Path(zoo_file).name] = result
            print()

    if not all_results:
        return

    # Show which terrain IDs are actually used across all maps
    all_terrain_ids = set()
    for counts in all_results.values():
        all_terrain_ids.update(counts.keys())

    print("=" * 80)
    print("TERRAIN IDs ACTUALLY USED ACROSS ALL ANALYZED MAPS:")
    print("=" * 80)
    print()

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

    for tid in sorted(all_terrain_ids):
        name = terrain_names.get(tid, f"Unknown{tid}")
        used_in = [map_name for map_name, counts in all_results.items() if tid in counts]
        print(f"  ID {tid:2d} ({name:20s}): Used in {len(used_in)} maps - {', '.join(used_in[:3])}")

    print()

    # Show which IDs are NEVER used (might indicate wrong mapping)
    unused_ids = set(range(16)) - all_terrain_ids
    if unused_ids:
        print("WARNING: These terrain IDs are NEVER used in any analyzed map:")
        for tid in sorted(unused_ids):
            name = terrain_names.get(tid, f"Unknown{tid}")
            print(f"  ID {tid:2d} ({name:20s})")
        print()
        print("This might indicate the terrain ID mapping is incorrect!")

    print()

if __name__ == '__main__':
    # Default: analyze some common ZT1 maps
    default_maps = [
        "build/Release/under.zoo",
        "build/Release/grasslan.zoo",
        "build/Release/desert.zoo",
        "build/Release/tropical.zoo",
        "build/Release/alpine.zoo"
    ]

    maps_to_analyze = sys.argv[1:] if len(sys.argv) > 1 else default_maps

    # Filter to only existing maps
    existing_maps = [m for m in maps_to_analyze if Path(m).exists()]

    if not existing_maps:
        print("No valid map files found!")
        print(f"Usage: python {sys.argv[0]} [map1.zoo] [map2.zoo] ...")
        print(f"Default maps searched: {', '.join(default_maps)}")
        sys.exit(1)

    compare_maps(existing_maps)
