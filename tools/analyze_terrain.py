#!/usr/bin/env python3
"""
Analyze terrain ID encoding in ZT1 .zoo map files.
Helps understand how high terrain IDs (244-255) are encoded.
"""
import struct
import os
import sys
from collections import defaultdict

def analyze_zoo_file(filepath):
    """Analyze terrain ID distribution in a .zoo file."""
    try:
        with open(filepath, 'rb') as f:
            data = f.read()

        if len(data) < 100:
            print(f"  [SKIP] Too small: {len(data)} bytes")
            return None

        # Parse header
        magic = struct.unpack('<I', data[0:4])[0]
        version = struct.unpack('<I', data[4:8])[0]
        width = struct.unpack('<I', data[12:16])[0]
        height = struct.unpack('<I', data[16:20])[0]
        base_terrain_id = struct.unpack('<I', data[0x20:0x24])[0]
        map_type = struct.unpack('<I', data[0x24:0x28])[0]

        # Validate dimensions
        if width == 0 or height == 0 or width > 200 or height > 200:
            # Try to find dimensions in header scan
            for i in range(0, min(128, len(data) - 4), 4):
                v1 = struct.unpack('<I', data[i:i+4])[0]
                v2 = struct.unpack('<I', data[i+4:i+8])[0] if i + 8 <= len(data) else 0
                if v1 in [75, 100, 125, 150, 128] and v1 == v2:
                    width, height = v1, v2
                    break

        if width == 0 or height == 0:
            print(f"  [SKIP] Invalid dimensions: {width}x{height}")
            return None

        print(f"\n{'='*60}")
        print(f"File: {filepath}")
        print(f"{'='*60}")
        print(f"  Size: {len(data)} bytes")
        print(f"  Magic: 0x{magic:08X}, Version: {version}")
        print(f"  Dimensions: {width}x{height} ({width*height} tiles)")
        print(f"  Base Terrain ID: {base_terrain_id}")
        print(f"  Map Type: {map_type}")

        # Parse tiles
        HEADER_SIZE = 100
        TILE_STRIDE = 10

        terrain_counts = defaultdict(int)
        elevation_counts = defaultdict(int)

        required = HEADER_SIZE + (width * height * TILE_STRIDE)
        if len(data) < required:
            print(f"  [WARN] Data too small: {len(data)} < {required}")
            return None

        for y in range(height):
            for x in range(width):
                idx = HEADER_SIZE + (y * width + x) * TILE_STRIDE
                terrain_id = data[idx]
                elevation = data[idx + 1] & 0x1F
                terrain_counts[terrain_id] += 1
                elevation_counts[elevation] += 1

        # Print terrain distribution
        print(f"\n  Terrain Distribution:")
        # Sort by count descending
        sorted_terrains = sorted(terrain_counts.items(), key=lambda x: -x[1])
        for terrain_id, count in sorted_terrains[:15]:
            pct = (count * 100) // (width * height)
            bar = '#' * (pct // 2)

            # Decode potential meaning
            decoded = ""
            if terrain_id <= 16:
                names = ["Grass", "Savannah", "Sand", "Dirt", "Rainforest", "BrownRock",
                        "GrayRock", "Gravel", "Snow", "FreshWater", "SaltWater",
                        "Deciduous", "Waterfall", "Conifer", "Concrete", "Asphalt", "Trampled"]
                decoded = f" = {names[terrain_id]}"
            elif terrain_id >= 240:
                inv = 255 - terrain_id
                if inv <= 16:
                    names = ["Grass", "Savannah", "Sand", "Dirt", "Rainforest", "BrownRock",
                            "GrayRock", "Gravel", "Snow", "FreshWater", "SaltWater",
                            "Deciduous", "Waterfall", "Conifer", "Concrete", "Asphalt"]
                    decoded = f" -> inv={inv} ({names[inv]}?)"
                else:
                    decoded = f" -> inv={inv}"

            print(f"    ID {terrain_id:3d} (0x{terrain_id:02X}): {count:5d} ({pct:2d}%) {bar}{decoded}")

        if len(sorted_terrains) > 15:
            print(f"    ... and {len(sorted_terrains) - 15} more terrain IDs")

        # Print elevation range
        min_elev = min(elevation_counts.keys())
        max_elev = max(elevation_counts.keys())
        print(f"\n  Elevation Range: {min_elev} to {max_elev}")

        return {
            'width': width,
            'height': height,
            'base_terrain_id': base_terrain_id,
            'map_type': map_type,
            'terrain_counts': dict(terrain_counts),
        }

    except Exception as e:
        print(f"  [ERROR] {e}")
        return None

def find_zoo_files(root_dir):
    """Find all .zoo files in directory tree."""
    zoo_files = []
    for dirpath, dirnames, filenames in os.walk(root_dir):
        for f in filenames:
            if f.lower().endswith('.zoo'):
                zoo_files.append(os.path.join(dirpath, f))
    return zoo_files

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Analyze specific files
        for filepath in sys.argv[1:]:
            if os.path.isdir(filepath):
                zoo_files = find_zoo_files(filepath)
                print(f"Found {len(zoo_files)} .zoo files in {filepath}")
                for zf in zoo_files[:10]:
                    analyze_zoo_file(zf)
            else:
                analyze_zoo_file(filepath)
    else:
        # Check for maps in common locations
        search_paths = [
            'maps',
            '../maps',
            '../../maps',
            'Release/maps',
            'build/Release/maps',
        ]
        for path in search_paths:
            if os.path.isdir(path):
                zoo_files = find_zoo_files(path)
                if zoo_files:
                    print(f"Found {len(zoo_files)} .zoo files in {path}")
                    for zf in zoo_files[:5]:
                        analyze_zoo_file(zf)
                    break
        else:
            print("No maps directory found. Usage: python analyze_terrain.py <path_to_zoo_files>")
