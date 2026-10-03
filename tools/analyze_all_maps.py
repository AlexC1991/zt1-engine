#!/usr/bin/env python3
"""
Analyze ALL map files to understand their actual terrain composition.
We need to understand what each map LOOKS like, not just what the base terrain byte says.
"""
import struct
from pathlib import Path
from collections import Counter
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

# Terrain type definitions from tiletex.cfg
TERRAIN_TYPES = {
    0: "Grass",
    1: "Savannah",
    2: "Sand",
    3: "Dirt",
    4: "Rainforest",
    5: "Brown Rock",
    6: "Gray Rock",
    7: "Gravel",
    8: "Snow",
    9: "Fresh Water",
    10: "Salt Water",
    11: "Deciduous",
    12: "Waterfall",
    13: "Coniferous",
    14: "Concrete",
    15: "Asphalt",
}

def analyze_map(map_path):
    """Analyze a single map file and return summary"""
    try:
        with open(map_path, 'rb') as f:
            data = f.read()

        if len(data) < 100 or data[0:4] != b'TZFB':
            return None

        width = struct.unpack('<I', data[0x0C:0x10])[0]
        height = struct.unpack('<I', data[0x10:0x14])[0]

        if width == 0 or height == 0 or width > 500 or height > 500:
            return None

        # Base terrain
        base_raw = data[0x20]
        base_type = base_raw & 0x0F

        # Count actual terrain types (resolving type 0 to base)
        header_size = 100
        terrain_counter = Counter()
        elevation_counter = Counter()

        for i in range(min(width * height, 100000)):  # Limit for safety
            offset = header_size + i * 10
            if offset + 10 > len(data):
                break

            tile = data[offset:offset+10]
            terrain_byte = tile[0]
            terrain_type = terrain_byte & 0x0F
            elevation = tile[1] & 0x1F

            # Resolve terrain type 0 to base terrain
            actual_type = terrain_type if terrain_type != 0 else base_type
            terrain_counter[actual_type] += 1
            elevation_counter[elevation] += 1

        total = sum(terrain_counter.values())
        if total == 0:
            return None

        # Find dominant terrain (what you actually SEE)
        dominant_type, dominant_count = terrain_counter.most_common(1)[0]
        dominant_pct = dominant_count / total * 100

        # Get elevation range
        min_elev = min(elevation_counter.keys()) if elevation_counter else 0
        max_elev = max(elevation_counter.keys()) if elevation_counter else 0

        return {
            'name': map_path.stem,
            'width': width,
            'height': height,
            'base_terrain_raw': base_raw,
            'base_terrain_type': base_type,
            'base_terrain_name': TERRAIN_TYPES.get(base_type, "?"),
            'dominant_terrain': dominant_type,
            'dominant_terrain_name': TERRAIN_TYPES.get(dominant_type, "?"),
            'dominant_pct': dominant_pct,
            'terrain_counts': terrain_counter,
            'total_tiles': total,
            'min_elevation': min_elev,
            'max_elevation': max_elev,
        }
    except Exception as e:
        return None

def main():
    maps_dir = ZT_PATH / "maps"

    if not maps_dir.exists():
        print(f"Maps directory not found: {maps_dir}")
        return

    zoo_files = sorted(maps_dir.glob("*.zoo"))
    print(f"Found {len(zoo_files)} map files\n")

    print("=" * 110)
    print("COMPLETE MAP ANALYSIS - What each map ACTUALLY looks like (terrain type 0 resolved to base)")
    print("=" * 110)
    print()

    # Table header
    print(f"{'Map Name':<20} {'Size':>10} {'Base':<12} {'Dominant (visible)':<20} {'%':>6} {'Elev':>8}")
    print("-" * 110)

    all_results = []

    for zoo_file in zoo_files:
        result = analyze_map(zoo_file)
        if result:
            all_results.append(result)

            size_str = f"{result['width']}x{result['height']}"
            elev_str = f"{result['min_elevation']}-{result['max_elevation']}"

            print(f"{result['name']:<20} {size_str:>10} {result['base_terrain_name']:<12} "
                  f"{result['dominant_terrain_name']:<20} {result['dominant_pct']:>5.1f}% {elev_str:>8}")

    # Detailed breakdown for key maps
    print("\n")
    print("=" * 110)
    print("DETAILED TERRAIN BREAKDOWN FOR KEY MAPS")
    print("=" * 110)

    key_maps = ['under', 'tundra', 'beach', 'default', 'crater', 'deathmtn', 'savannah', 'tropical', 'rockdes', 'island']

    for result in all_results:
        if result['name'] in key_maps:
            print(f"\n{'='*70}")
            print(f"MAP: {result['name']}.zoo")
            print(f"{'='*70}")
            print(f"Size: {result['width']} x {result['height']} tiles ({result['total_tiles']} total)")
            print(f"Base terrain byte: {result['base_terrain_raw']} (0x{result['base_terrain_raw']:02X})")
            print(f"Base terrain type: {result['base_terrain_type']} = {result['base_terrain_name']}")
            print(f"Elevation range: {result['min_elevation']} to {result['max_elevation']}")
            print()
            print("ACTUAL VISIBLE TERRAIN (type 0 resolved to base):")

            for terrain_type, count in sorted(result['terrain_counts'].items(), key=lambda x: -x[1]):
                pct = count / result['total_tiles'] * 100
                name = TERRAIN_TYPES.get(terrain_type, "?")
                bar = "#" * int(pct / 2)
                print(f"  {terrain_type:2d} {name:<15} {count:6d} ({pct:5.1f}%) {bar}")

    # Summary of unique base terrains used
    print("\n")
    print("=" * 110)
    print("BASE TERRAIN USAGE ACROSS ALL MAPS")
    print("=" * 110)

    base_usage = Counter()
    for result in all_results:
        base_usage[result['base_terrain_type']] += 1

    for base_type, count in sorted(base_usage.items(), key=lambda x: -x[1]):
        name = TERRAIN_TYPES.get(base_type, "?")
        maps_with_this = [r['name'] for r in all_results if r['base_terrain_type'] == base_type]
        print(f"\nBase {base_type} ({name}): {count} maps")
        print(f"  Maps: {', '.join(maps_with_this[:15])}")
        if len(maps_with_this) > 15:
            print(f"  ... and {len(maps_with_this) - 15} more")

if __name__ == '__main__':
    main()
