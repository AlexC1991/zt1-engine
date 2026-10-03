#!/usr/bin/env python3
"""
Compare actual tile data between maps to find what makes them look different.
"""
import struct
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

def analyze_tiles(map_path, num_tiles=50):
    """Analyze tile data in detail"""
    with open(map_path, 'rb') as f:
        data = f.read()

    name = map_path.stem
    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    base_terrain = data[0x20] & 0x0F

    print(f"\n{'='*80}")
    print(f"MAP: {name}.zoo - {width}x{height} tiles, base terrain = {base_terrain}")
    print(f"{'='*80}")

    header_size = 100

    print(f"\nFirst {num_tiles} tiles (10 bytes each):")
    print(f"{'#':<5} {'Raw Hex':<25} {'Terr':>5} {'Elev':>5} {'B2':>4} {'B3':>4} {'B4':>4} {'B5':>4} {'B6':>4} {'B7':>4} {'B8':>4} {'B9':>4}")
    print("-" * 100)

    for i in range(min(num_tiles, width * height)):
        offset = header_size + i * 10
        if offset + 10 > len(data):
            break

        tile = data[offset:offset+10]
        hex_str = tile.hex()

        terrain_byte = tile[0]
        terrain_type = terrain_byte & 0x0F
        terrain_flags = (terrain_byte >> 4) & 0x0F

        elevation = tile[1] & 0x1F
        elev_flags = (tile[1] >> 5) & 0x07

        # Show resolved terrain (if 0, show base)
        actual_terrain = terrain_type if terrain_type != 0 else base_terrain

        print(f"{i:<5} {hex_str:<25} {actual_terrain:>5} {elevation:>5} "
              f"{tile[2]:>4} {tile[3]:>4} {tile[4]:>4} {tile[5]:>4} "
              f"{tile[6]:>4} {tile[7]:>4} {tile[8]:>4} {tile[9]:>4}")

    # Analyze byte patterns
    print(f"\n\nBYTE PATTERN ANALYSIS (all tiles):")

    byte_values = [[] for _ in range(10)]

    for i in range(width * height):
        offset = header_size + i * 10
        if offset + 10 > len(data):
            break
        tile = data[offset:offset+10]
        for b in range(10):
            byte_values[b].append(tile[b])

    for b in range(10):
        values = byte_values[b]
        unique = len(set(values))
        min_v = min(values)
        max_v = max(values)

        # Most common values
        from collections import Counter
        common = Counter(values).most_common(5)
        common_str = ', '.join(f"{v}({c})" for v, c in common)

        print(f"  Byte {b}: unique={unique:4d}, range={min_v:3d}-{max_v:3d}, common: {common_str}")

def main():
    maps_dir = ZT_PATH / "maps"

    # Compare these maps
    maps = ['tundra', 'under', 'default', 'beach']

    for map_name in maps:
        map_path = maps_dir / f"{map_name}.zoo"
        if map_path.exists():
            analyze_tiles(map_path, 30)

if __name__ == '__main__':
    main()
