#!/usr/bin/env python3
"""
Analyze ALL biome values across ALL maps to understand the full biome system.
"""
import struct
from pathlib import Path
from collections import Counter, defaultdict
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

def analyze_all_maps():
    """Analyze biome distribution across all maps."""
    maps_dir = ZT_PATH / "maps"

    if not maps_dir.exists():
        print(f"Maps directory not found: {maps_dir}")
        return

    # Track biome usage across all maps
    global_biome_counts = Counter()
    biome_to_maps = defaultdict(list)

    zoo_files = sorted(maps_dir.glob("*.zoo"))
    print(f"Analyzing {len(zoo_files)} map files...\n")

    for map_path in zoo_files:
        try:
            with open(map_path, 'rb') as f:
                data = f.read()

            if len(data) < 100 or data[0:4] != b'TZFB':
                continue

            name = map_path.stem
            width = struct.unpack('<I', data[0x0C:0x10])[0]
            height = struct.unpack('<I', data[0x10:0x14])[0]

            if width == 0 or height == 0 or width > 500:
                continue

            header_size = 100
            biome_counts = Counter()

            for i in range(width * height):
                offset = header_size + i * 10
                if offset + 10 > len(data):
                    break
                biome = data[offset + 5]
                biome_counts[biome] += 1
                global_biome_counts[biome] += 1

            # Track which maps use which biomes
            for biome in biome_counts:
                if biome_counts[biome] > 100:  # Significant presence
                    biome_to_maps[biome].append(name)

        except Exception as e:
            continue

    # Print global biome distribution
    print("=" * 80)
    print("GLOBAL BIOME DISTRIBUTION (all maps combined)")
    print("=" * 80)
    print(f"{'Biome':<8} {'Count':>12} {'Maps Using It'}")
    print("-" * 80)

    for biome, count in sorted(global_biome_counts.items(), key=lambda x: -x[1]):
        maps = biome_to_maps.get(biome, [])
        maps_str = ', '.join(maps[:8])
        if len(maps) > 8:
            maps_str += f" (+{len(maps)-8} more)"
        print(f"{biome:<8} {count:>12,} {maps_str}")

    # Group by biome ranges to find patterns
    print("\n")
    print("=" * 80)
    print("BIOME VALUE ANALYSIS")
    print("=" * 80)

    # Check if biome might encode multiple things
    print("\nCommon biome values and their binary representation:")
    for biome in sorted(set(global_biome_counts.keys())):
        if global_biome_counts[biome] > 10000:
            print(f"  {biome:3d} = 0x{biome:02X} = {biome:08b}b")

    # Look for patterns
    print("\n\nHypothesis: Maybe byte 5 isn't just 'biome'?")
    print("Let's check if the values might be flags or combined data...")

    # Sample some tiles from different maps to see byte 4, 5, 6 together
    print("\n")
    print("=" * 80)
    print("SAMPLE RAW TILE DATA (bytes 4, 5, 6) from key maps")
    print("=" * 80)

    for map_name in ['tundra', 'under', 'beach', 'default', 'crater', 'mars']:
        map_path = maps_dir / f"{map_name}.zoo"
        if not map_path.exists():
            continue

        with open(map_path, 'rb') as f:
            data = f.read()

        width = struct.unpack('<I', data[0x0C:0x10])[0]
        height = struct.unpack('<I', data[0x10:0x14])[0]

        print(f"\n--- {map_name}.zoo ---")
        print("Sample tiles (terrain, elev, b2, b3, b4, b5, b6, b7, b8, b9):")

        # Sample center tiles
        for y in range(height//2 - 2, height//2 + 3):
            for x in range(width//2 - 2, width//2 + 3):
                offset = 100 + (y * width + x) * 10
                tile = data[offset:offset+10]
                terrain = tile[0] & 0x0F
                print(f"  ({x:3d},{y:3d}): t={terrain:2d} e={tile[1]:2d} [{tile[2]:3d} {tile[3]:3d} {tile[4]:3d} {tile[5]:3d} {tile[6]:3d} {tile[7]:3d} {tile[8]:3d} {tile[9]:3d}]")

if __name__ == '__main__':
    analyze_all_maps()
