#!/usr/bin/env python3
"""
Analyze under.zoo (underground/city map) to determine correct terrain colors
"""
import struct
from pathlib import Path
from collections import Counter

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

# Terrain type definitions from tiletex.cfg
TERRAIN_TYPES = {
    0: ("Grass", "grass.tga", "#4CAF50"),           # Green
    1: ("Savannah Grass", "grass_sv.tga", "#8BC34A"), # Yellow-green
    2: ("Sand", "sand.tga", "#F4D03F"),              # Yellow/tan
    3: ("Dirt", "dirt.tga", "#8B4513"),              # Brown
    4: ("Rainforest Floor", "ffloor.tga", "#2E7D32"), # Dark green
    5: ("Brown Rock", "bwnrock.tga", "#795548"),     # Brown rock
    6: ("Gray Rock", "gryrock.tga", "#607D8B"),      # Gray
    7: ("Gravel", "gravel.tga", "#9E9E9E"),          # Gray gravel
    8: ("Snow", "snow.tga", "#FFFFFF"),              # White
    9: ("Fresh Water", "water.tga", "#2196F3"),      # Blue
    10: ("Salt Water", "depwater.tga", "#1565C0"),   # Dark blue
    11: ("Deciduous Floor", "ffloord.tga", "#689F38"), # Forest green
    12: ("Waterfall", "bogus.tga", "#42A5F5"),       # Light blue
    13: ("Coniferous Floor", "ffloorc.tga", "#33691E"), # Dark forest
    14: ("Concrete", "ccrete.tga", "#BDBDBD"),       # Light gray
    15: ("Asphalt", "aphalt.tga", "#424242"),        # Dark gray
    16: ("Trampled", "worn.tga", "#A1887F"),         # Worn brown
}

def analyze_map(filename):
    """Analyze a .zoo map file"""
    map_path = ZT_PATH / "maps" / filename

    if not map_path.exists():
        print(f"Map not found: {map_path}")
        return

    with open(map_path, 'rb') as f:
        data = f.read()

    print("=" * 70)
    print(f"ANALYZING: {filename}")
    print("=" * 70)

    # Parse header
    magic = data[0:4]
    print(f"\nMagic: {magic} ({magic.hex()})")

    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    print(f"Map Dimensions: {width} x {height} tiles")
    print(f"Total Tiles: {width * height}")

    # Base terrain (this is crucial!)
    base_terrain_raw = data[0x20]
    base_terrain_type = base_terrain_raw & 0x0F
    base_terrain_flags = (base_terrain_raw >> 4) & 0x0F

    base_info = TERRAIN_TYPES.get(base_terrain_type, ("Unknown", "unknown.tga", "#FF00FF"))

    print(f"\n*** BASE TERRAIN ***")
    print(f"  Raw byte at 0x20: {base_terrain_raw} (0x{base_terrain_raw:02X})")
    print(f"  Type (low nibble): {base_terrain_type}")
    print(f"  Flags (high nibble): 0x{base_terrain_flags:X}")
    print(f"  Terrain Name: {base_info[0]}")
    print(f"  Texture File: {base_info[1]}")
    print(f"  Expected Color: {base_info[2]}")

    # Analyze tile distribution
    print(f"\n*** TILE ANALYSIS ***")

    header_size = 100
    terrain_counter = Counter()
    flag_counter = Counter()
    elevation_counter = Counter()

    # Sample tiles
    print("\nFirst 20 tiles:")
    for i in range(min(20, width * height)):
        offset = header_size + i * 10
        if offset + 10 <= len(data):
            tile = data[offset:offset+10]
            terrain_byte = tile[0]
            elevation_byte = tile[1]

            terrain_type = terrain_byte & 0x0F
            terrain_flags = (terrain_byte >> 4) & 0x0F
            elevation = elevation_byte & 0x1F

            terrain_counter[terrain_type] += 1
            flag_counter[terrain_flags] += 1
            elevation_counter[elevation] += 1

            t_info = TERRAIN_TYPES.get(terrain_type, ("?", "?", "?"))

            # Show if terrain is 0 (uses base terrain)
            actual_terrain = terrain_type if terrain_type != 0 else base_terrain_type
            actual_info = TERRAIN_TYPES.get(actual_terrain, ("?", "?", "?"))

            if i < 20:
                note = f" -> uses BASE ({base_info[0]})" if terrain_type == 0 else ""
                print(f"  Tile {i:3d}: terrain={terrain_type:2d} ({t_info[0]:20s}) flags=0x{terrain_flags:X} elev={elevation:2d}{note}")

    # Count all tiles
    for i in range(width * height):
        offset = header_size + i * 10
        if offset + 10 <= len(data):
            tile = data[offset:offset+10]
            terrain_byte = tile[0]
            terrain_type = terrain_byte & 0x0F
            terrain_flags = (terrain_byte >> 4) & 0x0F
            elevation = tile[1] & 0x1F

            if i >= 20:  # Already counted first 20
                terrain_counter[terrain_type] += 1
                flag_counter[terrain_flags] += 1
                elevation_counter[elevation] += 1

    print(f"\n*** TERRAIN DISTRIBUTION ***")
    total = sum(terrain_counter.values())
    for terrain_id, count in sorted(terrain_counter.items(), key=lambda x: -x[1]):
        t_info = TERRAIN_TYPES.get(terrain_id, ("Unknown", "?", "?"))
        pct = count / total * 100

        # Note if this terrain uses base
        if terrain_id == 0:
            actual = f" -> Actually {base_info[0]} (base terrain)"
        else:
            actual = ""

        print(f"  Type {terrain_id:2d}: {count:6d} tiles ({pct:5.1f}%) - {t_info[0]}{actual}")

    print(f"\n*** FLAG DISTRIBUTION ***")
    for flag, count in sorted(flag_counter.items(), key=lambda x: -x[1]):
        pct = count / total * 100
        flag_meaning = {
            0x0: "Natural/unmodified",
            0x1: "Modified variant 1",
            0x4: "Biome transition",
            0x5: "Modified variant 3",
            0x6: "Special marker",
            0xF: "Player-placed"
        }.get(flag, "Unknown")
        print(f"  Flag 0x{flag:X}: {count:6d} tiles ({pct:5.1f}%) - {flag_meaning}")

    print(f"\n*** ELEVATION DISTRIBUTION ***")
    for elev, count in sorted(elevation_counter.items()):
        pct = count / total * 100
        if pct > 1:  # Only show significant elevations
            print(f"  Elevation {elev:2d}: {count:6d} tiles ({pct:5.1f}%)")

    # Final summary
    print(f"\n" + "=" * 70)
    print("CONCLUSION")
    print("=" * 70)

    # Determine dominant terrain
    dominant_terrain = terrain_counter.most_common(1)[0][0]
    if dominant_terrain == 0:
        actual_dominant = base_terrain_type
        print(f"\nDominant terrain type: 0 (which maps to BASE TERRAIN)")
        print(f"Base terrain type: {base_terrain_type} = {base_info[0]}")
    else:
        actual_dominant = dominant_terrain
        dom_info = TERRAIN_TYPES.get(actual_dominant, ("?", "?", "?"))
        print(f"\nDominant terrain type: {actual_dominant} = {dom_info[0]}")

    final_info = TERRAIN_TYPES.get(actual_dominant, ("?", "?", "?"))
    print(f"\n*** THE MAP SHOULD APPEAR AS: ***")
    print(f"  Terrain: {final_info[0]}")
    print(f"  Texture: {final_info[1]}")
    print(f"  Color: {final_info[2]}")

    return actual_dominant, final_info

# Analyze under.zoo
print("\n" + "#" * 70)
print("# UNDERGROUND/CITY MAP ANALYSIS")
print("#" * 70)

analyze_map("under.zoo")

# Also check a few other maps for comparison
print("\n\n")
print("#" * 70)
print("# COMPARISON WITH OTHER MAPS")
print("#" * 70)

for map_name in ["tundra.zoo", "beach.zoo", "default.zoo"]:
    print()
    analyze_map(map_name)
