#!/usr/bin/env python3
"""
Analyze terrain ID encoding patterns in ZT1 .zoo files.
Test hypothesis: terrainId = rawByte & 0x0F (low nibble)
"""
import struct
import os
import sys
from collections import defaultdict

def analyze_zoo_file(filepath):
    """Analyze terrain ID patterns."""
    try:
        with open(filepath, 'rb') as f:
            data = f.read()

        if len(data) < 100:
            return None

        # Parse header
        width = struct.unpack('<I', data[12:16])[0]
        height = struct.unpack('<I', data[16:20])[0]
        base_terrain_id = struct.unpack('<I', data[0x20:0x24])[0]
        map_type = struct.unpack('<I', data[0x24:0x28])[0]

        # Validate dimensions
        if width == 0 or height == 0 or width > 200 or height > 200:
            for i in range(0, min(128, len(data) - 4), 4):
                v1 = struct.unpack('<I', data[i:i+4])[0]
                v2 = struct.unpack('<I', data[i+4:i+8])[0] if i + 8 <= len(data) else 0
                if v1 in [75, 100, 125, 150, 128] and v1 == v2:
                    width, height = v1, v2
                    break

        if width == 0 or height == 0:
            return None

        HEADER_SIZE = 100
        TILE_STRIDE = 10

        required = HEADER_SIZE + (width * height * TILE_STRIDE)
        if len(data) < required:
            return None

        # Count terrain by low nibble vs raw value
        raw_counts = defaultdict(int)
        nibble_counts = defaultdict(int)
        high_nibble_counts = defaultdict(int)

        for y in range(height):
            for x in range(width):
                idx = HEADER_SIZE + (y * width + x) * TILE_STRIDE
                raw = data[idx]
                low_nibble = raw & 0x0F
                high_nibble = (raw >> 4) & 0x0F
                raw_counts[raw] += 1
                nibble_counts[low_nibble] += 1
                high_nibble_counts[high_nibble] += 1

        # Check if low nibble mapping makes sense
        terrain_names = ["Grass", "Savannah", "Sand", "Dirt", "Rainforest", "BrownRock",
                        "GrayRock", "Gravel", "Snow", "FreshWater", "SaltWater",
                        "Deciduous", "Waterfall", "Conifer", "Concrete", "Asphalt"]

        print(f"\n{'='*70}")
        print(f"File: {os.path.basename(filepath)}")
        print(f"  Dims: {width}x{height}, BaseID: {base_terrain_id}, Type: {map_type}")
        print(f"{'='*70}")

        print(f"\n  Raw terrain ID distribution (top 10):")
        for raw, count in sorted(raw_counts.items(), key=lambda x: -x[1])[:10]:
            low = raw & 0x0F
            high = (raw >> 4) & 0x0F
            name = terrain_names[low] if low < 16 else "?"
            pct = (count * 100) // (width * height)
            print(f"    Raw {raw:3d} (0x{raw:02X}): {count:5d} ({pct:2d}%) = low:{low:2d} ({name}) high:{high}")

        print(f"\n  Low nibble distribution (interpreted terrain):")
        for nib, count in sorted(nibble_counts.items(), key=lambda x: -x[1]):
            name = terrain_names[nib] if nib < 16 else "?"
            pct = (count * 100) // (width * height)
            bar = '#' * (pct // 2)
            print(f"    Terrain {nib:2d} ({name:12s}): {count:5d} ({pct:2d}%) {bar}")

        print(f"\n  High nibble distribution (flags?):")
        for nib, count in sorted(high_nibble_counts.items(), key=lambda x: -x[1]):
            pct = (count * 100) // (width * height)
            print(f"    Flag 0x{nib:X}0: {count:5d} ({pct:2d}%)")

        return True

    except Exception as e:
        print(f"  [ERROR] {e}")
        return None

def find_zoo_files(root_dir, limit=10):
    """Find .zoo files."""
    zoo_files = []
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for f in filenames:
            if f.lower().endswith('.zoo'):
                zoo_files.append(os.path.join(dirpath, f))
                if len(zoo_files) >= limit:
                    return zoo_files
    return zoo_files

if __name__ == "__main__":
    if len(sys.argv) > 1:
        path = sys.argv[1]
        if os.path.isdir(path):
            zoo_files = find_zoo_files(path, 15)
            print(f"Analyzing {len(zoo_files)} .zoo files from {path}")
            for zf in zoo_files:
                analyze_zoo_file(zf)
        else:
            analyze_zoo_file(path)
    else:
        print("Usage: python analyze_terrain_v2.py <maps_directory>")
