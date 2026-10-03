#!/usr/bin/env python3
"""
Analyze raw terrain byte values in .zoo files to understand encoding
WITHOUT assuming nibble-based encoding
"""
import sys
import struct
from collections import Counter

def analyze_zoo_terrain(filepath):
    """Analyze terrain bytes without assumptions"""
    with open(filepath, 'rb') as f:
        data = f.read()

    # Read header
    magic = struct.unpack('<I', data[0:4])[0]
    version = struct.unpack('<I', data[4:8])[0]
    width = struct.unpack('<I', data[12:16])[0]
    height = struct.unpack('<I', data[16:20])[0]
    base_terrain = struct.unpack('<I', data[0x20:0x24])[0]
    map_type = struct.unpack('<I', data[0x24:0x28])[0]

    print(f"=== {filepath} ===")
    print(f"Magic: 0x{magic:08X}, Version: {version}")
    print(f"Dimensions: {width}x{height}")
    print(f"Base Terrain ID: {base_terrain}")
    print(f"Map Type: {map_type} (0x{map_type:04X})")
    print()

    # Read ALL terrain bytes
    HEADER_SIZE = 100
    TILE_STRIDE = 10

    terrain_bytes = []
    for y in range(height):
        for x in range(width):
            idx = HEADER_SIZE + (y * width + x) * TILE_STRIDE
            terrain_byte = data[idx + 0]
            terrain_bytes.append(terrain_byte)

    # Count occurrences
    counts = Counter(terrain_bytes)

    print("=== RAW TERRAIN BYTE DISTRIBUTION ===")
    print(f"Total tiles: {len(terrain_bytes)}")
    print(f"Unique terrain byte values: {len(counts)}")
    print()

    # Show all unique values and their counts
    for byte_val in sorted(counts.keys()):
        count = counts[byte_val]
        pct = (count / len(terrain_bytes)) * 100

        # Show nibble breakdown
        low_nibble = byte_val & 0x0F
        high_nibble = (byte_val >> 4) & 0x0F

        print(f"Byte 0x{byte_val:02X} ({byte_val:3d}): {count:6d} tiles ({pct:5.1f}%)")
        print(f"       Nibbles: Low=0x{low_nibble:X} ({low_nibble:2d}), High=0x{high_nibble:X} ({high_nibble:2d})")

    print()
    print("=== PATTERN ANALYSIS ===")

    # Check if values follow nibble pattern (0-15 in low nibble)
    low_nibbles = [b & 0x0F for b in terrain_bytes]
    high_nibbles = [(b >> 4) & 0x0F for b in terrain_bytes]

    low_counts = Counter(low_nibbles)
    high_counts = Counter(high_nibbles)

    print("Low nibble distribution:")
    for val in sorted(low_counts.keys()):
        print(f"  0x{val:X} ({val:2d}): {low_counts[val]:6d} tiles")

    print()
    print("High nibble distribution:")
    for val in sorted(high_counts.keys()):
        print(f"  0x{val:X} ({val:2d}): {high_counts[val]:6d} tiles")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python analyze_terrain_bytes.py <zoo_file> [zoo_file2 ...]")
        sys.exit(1)

    for filepath in sys.argv[1:]:
        analyze_zoo_terrain(filepath)
        print("\n" + "="*60 + "\n")
