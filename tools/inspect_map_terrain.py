#!/usr/bin/env python3
"""
Inspect terrain distribution in a .zoo map file
"""
import sys
import struct

def read_zoo_file(filepath):
    """Read and analyze a .zoo map file"""
    with open(filepath, 'rb') as f:
        data = f.read()

    # Read header
    magic = struct.unpack('<I', data[0:4])[0]
    version = struct.unpack('<I', data[4:8])[0]
    width = struct.unpack('<I', data[12:16])[0]
    height = struct.unpack('<I', data[16:20])[0]
    base_terrain = struct.unpack('<I', data[0x20:0x24])[0]
    map_type = struct.unpack('<I', data[0x24:0x28])[0]

    print(f"=== MAP HEADER ===")
    print(f"Magic: 0x{magic:08X}")
    print(f"Version: {version}")
    print(f"Dimensions: {width}x{height}")
    print(f"Base Terrain ID: {base_terrain}")
    print(f"Map Type: {map_type}")
    print()

    # Read tiles
    HEADER_SIZE = 100
    TILE_STRIDE = 10

    terrain_counts = {}
    elevation_range = [999, 0]

    print(f"=== TILE ANALYSIS ===")
    print(f"Analyzing {width * height} tiles...")

    for y in range(height):
        for x in range(width):
            idx = HEADER_SIZE + (y * width + x) * TILE_STRIDE

            terrain_id = data[idx + 0]
            elev_byte = data[idx + 1]
            elevation = elev_byte & 0x1F

            # Extract terrain type (low nibble)
            terrain_type = terrain_id & 0x0F
            terrain_flags = (terrain_id >> 4) & 0x0F

            if terrain_type not in terrain_counts:
                terrain_counts[terrain_type] = {
                    'count': 0,
                    'raw_ids': set(),
                    'flags': set()
                }

            terrain_counts[terrain_type]['count'] += 1
            terrain_counts[terrain_type]['raw_ids'].add(terrain_id)
            terrain_counts[terrain_type]['flags'].add(terrain_flags)

            elevation_range[0] = min(elevation_range[0], elevation)
            elevation_range[1] = max(elevation_range[1], elevation)

    print(f"Elevation range: {elevation_range[0]} to {elevation_range[1]}")
    print()

    # Terrain names
    terrain_names = [
        "Grass", "Savannah", "Sand", "Dirt", "Rainforest", "Brown Stone",
        "Gray Stone", "Gravel", "Snow", "Fresh Water", "Salt Water",
        "Deciduous", "Waterfall", "Conifer", "Concrete", "Asphalt"
    ]

    print("=== TERRAIN DISTRIBUTION ===")
    total_tiles = width * height

    for tid in sorted(terrain_counts.keys()):
        info = terrain_counts[tid]
        name = terrain_names[tid] if tid < len(terrain_names) else f"Unknown({tid})"
        pct = (info['count'] / total_tiles) * 100

        print(f"ID {tid:2d} ({name:15s}): {info['count']:6d} tiles ({pct:5.1f}%)")

        # Show raw IDs and flags
        raw_ids_str = ', '.join(f"0x{rid:02X}" for rid in sorted(info['raw_ids']))
        flags_str = ', '.join(f"0x{f:X}0" for f in sorted(info['flags']))
        print(f"     Raw IDs: {raw_ids_str}")
        print(f"     Flags:   {flags_str}")
        print()

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python inspect_map_terrain.py <path_to_zoo_file>")
        sys.exit(1)

    read_zoo_file(sys.argv[1])
