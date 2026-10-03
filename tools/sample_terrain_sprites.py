#!/usr/bin/env python3
"""
Extract and display sample information from terrain .ani files
to verify they contain the expected terrain types.

This helps identify if sprite files are misnamed or contain wrong terrain visuals.
"""
import sys
import struct
import zipfile
from pathlib import Path

def analyze_ani_file(ani_data, name):
    """Analyze .ani file structure"""
    if len(ani_data) < 20:
        return None

    # ANI file format (simplified):
    # 0x00: Frame count (4 bytes)
    # 0x04: Width (4 bytes)
    # 0x08: Height (4 bytes)
    # ... more header data ...

    frame_count = struct.unpack('<I', ani_data[0:4])[0]
    width = struct.unpack('<I', ani_data[4:8])[0]
    height = struct.unpack('<I', ani_data[8:12])[0]

    # Sanity checks
    if frame_count > 1000 or frame_count == 0:
        return None
    if width > 500 or width == 0:
        return None
    if height > 500 or height == 0:
        return None

    return {
        'name': name,
        'frames': frame_count,
        'width': width,
        'height': height,
        'size': len(ani_data)
    }

def compare_terrain_sprites(ztd_path):
    """Compare all terrain sprites to look for patterns"""
    print("=" * 80)
    print("TERRAIN SPRITE ANALYSIS")
    print("=" * 80)
    print()

    with zipfile.ZipFile(ztd_path, 'r') as zf:
        # Get all terrain .ani files
        terrain_anis = [f for f in zf.namelist()
                       if f.startswith('terrain/') and f.endswith('.ani')]

        results = []
        for ani_path in sorted(terrain_anis):
            ani_data = zf.read(ani_path)
            dir_name = ani_path.split('/')[1]  # e.g., "icgrass"

            info = analyze_ani_file(ani_data, dir_name)
            if info:
                results.append(info)

        print(f"Found {len(results)} valid terrain .ani files")
        print()
        print(f"{'Directory':<15} {'Frames':<8} {'Size (W x H)':<15} {'File Size':<12}")
        print("-" * 70)

        for r in results:
            size_str = f"{r['width']} x {r['height']}"
            file_size_str = f"{r['size']:,} bytes"
            print(f"{r['name']:<15} {r['frames']:<8} {size_str:<15} {file_size_str:<12}")

        print()
        print("=" * 80)
        print("OBSERVATIONS:")
        print("=" * 80)
        print()

        # Look for patterns
        frame_counts = {}
        for r in results:
            fc = r['frames']
            if fc not in frame_counts:
                frame_counts[fc] = []
            frame_counts[fc].append(r['name'])

        print("Frame count groupings:")
        for fc in sorted(frame_counts.keys()):
            dirs = frame_counts[fc]
            print(f"  {fc} frames: {', '.join(dirs)}")

        print()

        # File size comparison (larger files might be more complex/detailed terrains)
        print("Sorted by file size (largest first):")
        sorted_results = sorted(results, key=lambda x: x['size'], reverse=True)
        for r in sorted_results[:10]:
            print(f"  {r['name']:<15} {r['size']:>10,} bytes  ({r['frames']} frames)")

        print()
        print("Smallest files:")
        for r in sorted_results[-5:]:
            print(f"  {r['name']:<15} {r['size']:>10,} bytes  ({r['frames']} frames)")

        print()

    # Now map these to expected terrain IDs
    print("=" * 80)
    print("EXPECTED TERRAIN ID MAPPING:")
    print("=" * 80)
    print()

    # This is what SpriteDatabase.cpp currently uses
    mapping = [
        (0,  "icgrass",   "Grass"),
        (1,  "icgrs_sv",  "Savannah"),
        (2,  "icsand",    "Sand"),
        (3,  "icdirt",    "Dirt"),
        (4,  "icffloor",  "Rainforest"),
        (5,  "icbnrock",  "Brown Stone"),
        (6,  "icgrock",   "Gray Stone"),
        (7,  "icgravel",  "Gravel"),
        (8,  "icsnow",    "Snow"),
        (9,  "icwater",   "Fresh Water"),
        (10, "icdpwatr",  "Salt Water"),
        (11, "icfflord",  "Deciduous Floor"),
        (12, "icwater",   "Waterfall (uses icwater)"),
        (13, "icfflorc",  "Conifer Floor"),
        (14, "icccrete",  "Concrete"),
        (15, "icaphalt",  "Asphalt")
    ]

    # Find the corresponding sprite info for each
    sprite_map = {r['name']: r for r in results}

    print(f"{'ID':<4} {'Name':<20} {'Sprite Dir':<12} {'Frames':<8} {'Size':<12}")
    print("-" * 70)

    for tid, sprite_dir, name in mapping:
        if sprite_dir in sprite_map:
            s = sprite_map[sprite_dir]
            print(f"{tid:<4} {name:<20} {sprite_dir:<12} {s['frames']:<8} {s['width']}x{s['height']}")
        else:
            print(f"{tid:<4} {name:<20} {sprite_dir:<12} MISSING!")

    print()

if __name__ == '__main__':
    ztd_path = "build/Release/terrain.ztd"

    if len(sys.argv) > 1:
        ztd_path = sys.argv[1]

    if not Path(ztd_path).exists():
        print(f"ERROR: {ztd_path} not found!")
        print(f"Usage: python {sys.argv[0]} [path/to/terrain.ztd]")
        sys.exit(1)

    compare_terrain_sprites(ztd_path)
