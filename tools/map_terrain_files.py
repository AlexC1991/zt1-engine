#!/usr/bin/env python3
"""
Comprehensively map all terrain files in terrain.ztd to understand
the correct paths for each terrain type ID (0-15)
"""
import sys
import zipfile
from pathlib import Path

def analyze_terrain_ztd(ztd_path):
    """List all terrain-related files and their structure"""

    print("=" * 80)
    print("TERRAIN.ZTD COMPREHENSIVE FILE MAPPING")
    print("=" * 80)
    print()

    # Open ZTD (it's a ZIP file)
    with zipfile.ZipFile(ztd_path, 'r') as zf:
        all_files = zf.namelist()

    # Filter for terrain files
    terrain_files = [f for f in all_files if f.startswith('terrain/')]

    print(f"Total files in terrain.ztd: {len(all_files)}")
    print(f"Terrain-related files: {len(terrain_files)}")
    print()

    # Organize by directory
    terrain_dirs = {}
    for file in terrain_files:
        parts = file.split('/')
        if len(parts) >= 2:
            dir_name = parts[1]  # e.g., "icgrass" from "terrain/icgrass/icgrass.ani"

            if dir_name not in terrain_dirs:
                terrain_dirs[dir_name] = []
            terrain_dirs[dir_name].append(file)

    print("=" * 80)
    print("TERRAIN DIRECTORIES AND THEIR FILES")
    print("=" * 80)
    print()

    for dir_name in sorted(terrain_dirs.keys()):
        files = terrain_dirs[dir_name]
        print(f"[DIR] terrain/{dir_name}/")
        for f in sorted(files):
            filename = f.split('/')[-1]
            print(f"   - {filename}")
        print()

    print("=" * 80)
    print("SEARCHING FOR .ANI FILES (Animation Definitions)")
    print("=" * 80)
    print()

    ani_files = [f for f in terrain_files if f.endswith('.ani')]
    print(f"Found {len(ani_files)} .ani files:")
    print()

    for ani in sorted(ani_files):
        print(f"  {ani}")
    print()

    print("=" * 80)
    print("TERRAIN TYPE MAPPING ANALYSIS")
    print("=" * 80)
    print()

    # Known terrain types from ZT1
    terrain_names = [
        (0,  "Grass"),
        (1,  "Savannah"),
        (2,  "Sand"),
        (3,  "Dirt"),
        (4,  "Rainforest"),
        (5,  "Brown Stone"),
        (6,  "Gray Stone"),
        (7,  "Gravel"),
        (8,  "Snow"),
        (9,  "Fresh Water"),
        (10, "Salt Water"),
        (11, "Deciduous Floor"),
        (12, "Waterfall"),
        (13, "Conifer Floor"),
        (14, "Concrete"),
        (15, "Asphalt")
    ]

    # Common abbreviations used in ZT1
    # Based on the directory names we found
    abbreviations = {
        'icgrass': 'Grass',
        'icsand': 'Sand',
        'icdirt': 'Dirt',
        'icsnow': 'Snow',
        'icwater': 'Fresh Water',
        'icgravel': 'Gravel',
        'icgrs_sv': 'Grass/Savannah blend',
        'icffloor': 'Forest Floor (generic)',
        'icfflord': 'Forest Floor Deciduous',
        'icfflorc': 'Forest Floor Conifer',
        'icdfloor': 'Deciduous Floor (alternate)',
        'iccfloor': 'Conifer Floor (alternate)',
        'icccrete': 'Concrete',
        'icaphalt': 'Asphalt',
        'icbnrock': 'Brown Rock/Stone',
        'icgrock': 'Gray Rock/Stone',
        'icdpwatr': 'Deep Water',
        'icswater': 'Salt Water (if exists)',
        'icwfall': 'Waterfall (if exists)',
        'icrforest': 'Rainforest (if exists)'
    }

    print("PROPOSED TERRAIN ID MAPPING:")
    print("(Based on directory names found in terrain.ztd)")
    print()

    # Try to match each terrain ID to a directory
    for tid, name in terrain_names:
        print(f"ID {tid:2d} - {name:20s}", end=" -> ")

        # Search for matching directories
        matches = []
        name_lower = name.lower().replace(' ', '')

        for dir_name in terrain_dirs.keys():
            # Check if directory name contains key parts of terrain name
            if 'grass' in name_lower and 'grass' in dir_name:
                matches.append(dir_name)
            elif 'savannah' in name_lower and 'sv' in dir_name:
                matches.append(dir_name)
            elif 'sand' in name_lower and 'sand' in dir_name:
                matches.append(dir_name)
            elif 'dirt' in name_lower and 'dirt' in dir_name:
                matches.append(dir_name)
            elif 'rainforest' in name_lower and 'forest' in dir_name:
                matches.append(dir_name)
            elif 'brown' in name_lower and 'bn' in dir_name:
                matches.append(dir_name)
            elif 'gray' in name_lower and ('gr' in dir_name or 'gy' in dir_name):
                matches.append(dir_name)
            elif 'gravel' in name_lower and 'gravel' in dir_name:
                matches.append(dir_name)
            elif 'snow' in name_lower and 'snow' in dir_name:
                matches.append(dir_name)
            elif 'fresh' in name_lower and 'water' in dir_name and 'dp' not in dir_name:
                matches.append(dir_name)
            elif 'salt' in name_lower and ('salt' in dir_name or 'swater' in dir_name):
                matches.append(dir_name)
            elif 'deciduous' in name_lower and ('dfloor' in dir_name or 'fflord' in dir_name):
                matches.append(dir_name)
            elif 'waterfall' in name_lower and 'wfall' in dir_name:
                matches.append(dir_name)
            elif 'conifer' in name_lower and ('cfloor' in dir_name or 'fflorc' in dir_name):
                matches.append(dir_name)
            elif 'concrete' in name_lower and 'ccrete' in dir_name:
                matches.append(dir_name)
            elif 'asphalt' in name_lower and 'aphalt' in dir_name:
                matches.append(dir_name)

        if matches:
            print(f"terrain/{matches[0]}/{matches[0]}")
            if len(matches) > 1:
                print(f"      (alternatives: {', '.join(matches[1:])})")
        else:
            print("WARNING: NOT FOUND IN ARCHIVE")

    print()
    print("=" * 80)
    print("RECOMMENDED C++ TERRAIN DEFINITIONS")
    print("=" * 80)
    print()
    print("const struct TerrainDef {")
    print("    int id;")
    print("    const char* path;")
    print("    const char* name;")
    print("} terrainDefs[] = {")

    # Generate C++ array based on findings
    mappings = {
        0: ('icgrass', 'Grass'),
        1: ('icgrs_sv', 'Savannah'),  # Best guess: grass/savannah blend
        2: ('icsand', 'Sand'),
        3: ('icdirt', 'Dirt'),
        4: ('icffloor', 'Rainforest'),  # Generic forest floor
        5: ('icbnrock', 'Brown Stone'),
        6: ('icgrock', 'Gray Stone'),
        7: ('icgravel', 'Gravel'),
        8: ('icsnow', 'Snow'),
        9: ('icwater', 'Fresh Water'),
        10: ('icdpwatr', 'Salt Water'),  # Deep water as salt water
        11: ('icfflord', 'Deciduous Floor'),
        12: ('icwater', 'Waterfall'),  # Might use water
        13: ('icfflorc', 'Conifer Floor'),
        14: ('icccrete', 'Concrete'),
        15: ('icaphalt', 'Asphalt')
    }

    for tid in range(16):
        if tid in mappings:
            dir_name, name = mappings[tid]
            exists = dir_name in terrain_dirs
            status = "OK" if exists else "MISSING"
            print(f"    {{{tid:2d}, \"terrain/{dir_name}/{dir_name}\", \"{name:20s}\"}}, // {status}")
        else:
            print(f"    {{{tid:2d}, \"terrain/UNKNOWN/UNKNOWN\",     \"Unknown{tid:2d}           \"}}, // MISSING")

    print("};")
    print()

if __name__ == '__main__':
    ztd_path = "build/Release/terrain.ztd"

    if len(sys.argv) > 1:
        ztd_path = sys.argv[1]

    if not Path(ztd_path).exists():
        print(f"ERROR: {ztd_path} not found!")
        print(f"Usage: python {sys.argv[0]} [path/to/terrain.ztd]")
        sys.exit(1)

    analyze_terrain_ztd(ztd_path)
