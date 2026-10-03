#!/usr/bin/env python3
"""
Analyze .zoo file structure to determine correct offsets and strides
"""
import sys
import struct

def analyze_zoo(filepath):
    with open(filepath, 'rb') as f:
        data = f.read()

    print(f"=== Analyzing {filepath} ===")
    print(f"File size: {len(data)} bytes\n")

    # Header
    magic = data[0:4]
    version = struct.unpack('<I', data[4:8])[0]
    print(f"Magic: {magic}")
    print(f"Version: {version}\n")

    # Try to find dimensions
    w1 = struct.unpack('<I', data[8:12])[0]
    h1 = struct.unpack('<I', data[12:16])[0]
    w2 = struct.unpack('<I', data[16:20])[0]
    h2 = struct.unpack('<I', data[20:24])[0]

    print(f"Offset 0x08: {w1} (might be width)")
    print(f"Offset 0x0C: {h1} (might be width)")
    print(f"Offset 0x10: {w2} (might be height)")
    print(f"Offset 0x14: {h2} (might be height)\n")

    # Try all possible dimension combinations
    possible_dims = [
        (w1, h1, "w1 x h1"),
        (h1, w1, "h1 x w1"),
        (w2, h2, "w2 x h2"),
        (h2, w2, "h2 x w2"),
        (75, 75, "hardcoded 75x75"),
        (150, 150, "hardcoded 150x150"),
    ]

    print("Possible dimensions:")
    for w, h, desc in possible_dims:
        if 10 <= w <= 200 and 10 <= h <= 200:
            print(f"  {desc}: {w} x {h} = {w*h} tiles")

    # Use the most reasonable one (from logs we know it's 75x75 for tundra)
    mapW, mapH = 75, 75
    numTiles = mapW * mapH
    print(f"\nUsing dimensions: {mapW} x {mapH} = {numTiles} tiles\n")

    # Look for repeating patterns that might be tiles
    print("=== Searching for tile data patterns ===")

    # Try different header sizes and strides
    for header_size in [44, 64, 100]:
        for stride in [4, 6, 8, 10]:
            # Check if this combination makes sense
            expected_size = header_size + (numTiles * stride)

            if expected_size > len(data):
                continue

            print(f"\nTrying HEADER={header_size}, STRIDE={stride}:")
            print(f"  Expected data end: {expected_size} (file is {len(data)})")

            # Sample first 5 tiles
            all_zero = True
            for i in range(min(5, numTiles)):
                offset = header_size + (i * stride)
                tile_bytes = data[offset:offset+stride]

                if any(b != 0 for b in tile_bytes):
                    all_zero = False

                print(f"  Tile {i:3d} @ 0x{offset:04X}: {' '.join(f'{b:02X}' for b in tile_bytes)}")

            if all_zero:
                print(f"  ❌ All tiles are zero - wrong offset/stride")
            else:
                # Check terrain distribution
                terrain_counts = {}
                for i in range(numTiles):
                    offset = header_size + (i * stride)
                    if offset + stride > len(data):
                        break
                    terrain_id = data[offset]  # Assume first byte is terrain
                    terrain_counts[terrain_id] = terrain_counts.get(terrain_id, 0) + 1

                print(f"  Terrain distribution:")
                for tid in sorted(terrain_counts.keys())[:10]:
                    print(f"    ID {tid:3d}: {terrain_counts[tid]:5d} tiles")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python analyze_zoo_structure.py <zoo_file>")
        sys.exit(1)

    analyze_zoo(sys.argv[1])
