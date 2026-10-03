#!/usr/bin/env python3
"""
Debug terrain type decoding to find why water shows as sand.
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
        return

    name = map_path.stem
    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    base_terrain_raw = data[0x20]
    base_terrain = base_terrain_raw & 0x0F

    print(f"\n{'='*60}")
    print(f"MAP: {name}.zoo")
    print(f"{'='*60}")
    print(f"Size: {width}x{height}")
    print(f"Base terrain raw: {base_terrain_raw} (0x{base_terrain_raw:02X})")
    print(f"Base terrain type: {base_terrain} = {TERRAIN_NAMES.get(base_terrain, '?')}")

    # Count RAW terrain bytes
    raw_counts = Counter()
    decoded_counts = Counter()

    for i in range(width * height):
        offset = 100 + i * 10
        if offset + 10 > len(data):
            break

        raw_byte = data[offset]
        terrain_type = raw_byte & 0x0F

        # Resolve type 0 to base terrain
        if terrain_type == 0:
            terrain_type = base_terrain

        raw_counts[raw_byte] += 1
        decoded_counts[terrain_type] += 1

    print(f"\nRAW TERRAIN BYTES (top 10):")
    for raw, count in raw_counts.most_common(10):
        decoded = raw & 0x0F
        if decoded == 0:
            decoded = base_terrain
        flags = (raw >> 4) & 0x0F
        pct = count / (width * height) * 100
        print(f"  Raw {raw:3d} (0x{raw:02X}) -> Type {decoded:2d} ({TERRAIN_NAMES.get(decoded, '?'):<12}) Flags=0x{flags:X}  {count:6d} ({pct:5.1f}%)")

    print(f"\nDECODED TERRAIN DISTRIBUTION:")
    for terrain, count in sorted(decoded_counts.items(), key=lambda x: -x[1]):
        pct = count / (width * height) * 100
        print(f"  {terrain:2d} {TERRAIN_NAMES.get(terrain, '?'):<15} {count:6d} ({pct:5.1f}%)")

    # Check for water specifically
    water_count = decoded_counts.get(9, 0) + decoded_counts.get(10, 0) + decoded_counts.get(12, 0)
    if water_count > 0:
        print(f"\n** WATER TILES: {water_count} (Fresh={decoded_counts.get(9,0)}, Salt={decoded_counts.get(10,0)}, Waterfall={decoded_counts.get(12,0)})")

def main():
    maps_dir = ZT_PATH / "maps"

    # Check maps that should have water
    water_maps = ['island', 'lagoon', 'nile', 'jungriv', 'crater', 'locean']

    for map_name in water_maps:
        map_path = maps_dir / f"{map_name}.zoo"
        if map_path.exists():
            analyze_map(map_path)

    # Also check a few standard maps
    print("\n" + "="*60)
    print("STANDARD MAPS")
    print("="*60)

    for map_name in ['default', 'tundra', 'under', 'beach']:
        map_path = maps_dir / f"{map_name}.zoo"
        if map_path.exists():
            analyze_map(map_path)

if __name__ == '__main__':
    main()
