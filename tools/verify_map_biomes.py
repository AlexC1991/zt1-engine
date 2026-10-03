#!/usr/bin/env python3
"""
Verify map biomes and terrain - simplified analysis without elevation complexity.
Focus on what color each map SHOULD render as.
"""
import struct
from pathlib import Path
from collections import Counter
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

# Terrain colors (RGB)
TERRAIN_COLORS = {
    0: ("Grass", (76, 175, 80)),        # Green
    1: ("Savannah", (139, 195, 74)),    # Yellow-green
    2: ("Sand", (244, 208, 63)),        # Yellow/tan
    3: ("Dirt", (139, 69, 19)),         # Brown
    4: ("Rainforest", (46, 125, 50)),   # Dark green
    5: ("Brown Rock", (121, 85, 72)),   # Brown
    6: ("Gray Rock", (96, 125, 139)),   # Gray
    7: ("Gravel", (158, 158, 158)),     # Light gray
    8: ("Snow", (255, 255, 255)),       # White
    9: ("Fresh Water", (33, 150, 243)), # Blue
    10: ("Salt Water", (21, 101, 192)), # Dark blue
    11: ("Deciduous", (104, 159, 56)),  # Forest green
    12: ("Waterfall", (66, 165, 245)),  # Light blue
    13: ("Coniferous", (51, 105, 30)),  # Dark forest
    14: ("Concrete", (189, 189, 189)),  # Light gray
    15: ("Asphalt", (66, 66, 66)),      # Dark gray
}

# Biome overlays and their effects
BIOME_INFO = {
    0: ("Normal", None),
    8: ("Snow", (240, 245, 255)),       # White/snowy
    10: ("Beach", "warm_tint"),          # Warm tint
    14: ("City", "gray_tint"),           # Gray/urban
    15: ("City2", "gray_tint"),          # Gray/urban
}

def analyze_map(map_path):
    """Analyze a map and determine what it should look like."""
    with open(map_path, 'rb') as f:
        data = f.read()

    if len(data) < 100 or data[0:4] != b'TZFB':
        return None

    name = map_path.stem
    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    base_terrain = data[0x20] & 0x0F

    # Analyze all tiles
    header_size = 100
    tile_stride = 10

    terrain_counts = Counter()
    biome_counts = Counter()

    for i in range(width * height):
        offset = header_size + i * tile_stride
        if offset + 10 > len(data):
            break

        terrain_byte = data[offset]
        terrain_type = terrain_byte & 0x0F
        biome = data[offset + 5]

        # Resolve terrain 0 to base
        actual_terrain = terrain_type if terrain_type != 0 else base_terrain

        terrain_counts[actual_terrain] += 1
        biome_counts[biome] += 1

    total = sum(terrain_counts.values())
    if total == 0:
        return None

    # Find dominant terrain and biome
    dominant_terrain = terrain_counts.most_common(1)[0][0]
    dominant_biome = biome_counts.most_common(1)[0][0]

    return {
        'name': name,
        'width': width,
        'height': height,
        'base_terrain': base_terrain,
        'dominant_terrain': dominant_terrain,
        'dominant_biome': dominant_biome,
        'terrain_counts': terrain_counts,
        'biome_counts': biome_counts,
        'total': total,
    }

def get_expected_color(terrain, biome):
    """Calculate what color a tile should render as."""
    # Start with terrain color
    terrain_name, base_color = TERRAIN_COLORS.get(terrain, ("Unknown", (255, 0, 255)))
    r, g, b = base_color

    # Apply biome overlay
    if biome == 8:  # Snow
        # Heavy blend toward white
        r = (r + 255 * 3) // 4
        g = (g + 255 * 3) // 4
        b = (b + 255 * 3) // 4
    elif biome == 10:  # Beach
        # Warm tint
        r = min(255, r + 20)
        g = min(255, g + 10)
    elif biome in (14, 15):  # City
        # Gray desaturation
        avg = (r + g + b) // 3
        r = (r + avg) // 2
        g = (g + avg) // 2
        b = (b + avg) // 2

    return (r, g, b)

def color_to_hex(rgb):
    return f"#{rgb[0]:02X}{rgb[1]:02X}{rgb[2]:02X}"

def main():
    maps_dir = ZT_PATH / "maps"

    if not maps_dir.exists():
        print(f"Maps directory not found: {maps_dir}")
        return

    # Key maps to analyze
    key_maps = ['tundra', 'under', 'beach', 'default', 'savannah', 'tropical',
                'crater', 'island', 'mars', 'rockdes', 'deathmtn', 'arcmaze']

    print("=" * 100)
    print("MAP COLOR ANALYSIS - What Each Map Should Look Like")
    print("=" * 100)
    print()

    print(f"{'Map':<15} {'Base':<12} {'Biome':<10} {'Expected Color':<20} {'Description'}")
    print("-" * 100)

    results = []
    for map_name in key_maps:
        map_path = maps_dir / f"{map_name}.zoo"
        if map_path.exists():
            result = analyze_map(map_path)
            if result:
                results.append(result)

                terrain_name = TERRAIN_COLORS.get(result['dominant_terrain'], ("?", (0,0,0)))[0]
                biome_name = BIOME_INFO.get(result['dominant_biome'], ("?", None))[0]

                expected = get_expected_color(result['dominant_terrain'], result['dominant_biome'])
                hex_color = color_to_hex(expected)

                # Description
                if result['dominant_biome'] == 8:
                    desc = f"WHITE/SNOWY (snow over {terrain_name})"
                elif result['dominant_biome'] == 14:
                    desc = f"GRAY/URBAN (city over {terrain_name})"
                elif result['dominant_biome'] == 10:
                    desc = f"WARM TROPICAL ({terrain_name} with beach tint)"
                else:
                    desc = f"{terrain_name.upper()} (no overlay)"

                print(f"{result['name']:<15} {terrain_name:<12} {biome_name:<10} {hex_color:<20} {desc}")

    # Detailed breakdown
    print()
    print("=" * 100)
    print("DETAILED TILE BREAKDOWN")
    print("=" * 100)

    for result in results:
        print(f"\n--- {result['name'].upper()}.zoo ---")
        print(f"Size: {result['width']}x{result['height']} = {result['total']} tiles")
        print(f"Base terrain: {result['base_terrain']} ({TERRAIN_COLORS.get(result['base_terrain'], ('?', (0,0,0)))[0]})")

        print("\nTerrain distribution:")
        for t, count in sorted(result['terrain_counts'].items(), key=lambda x: -x[1])[:5]:
            pct = count / result['total'] * 100
            name = TERRAIN_COLORS.get(t, ("?", (0,0,0)))[0]
            print(f"  {t:2d} {name:<15} {count:6d} ({pct:5.1f}%)")

        print("\nBiome distribution:")
        for b, count in sorted(result['biome_counts'].items(), key=lambda x: -x[1]):
            pct = count / result['total'] * 100
            name = BIOME_INFO.get(b, ("Unknown", None))[0]
            print(f"  {b:2d} {name:<10} {count:6d} ({pct:5.1f}%)")

    # Summary of what we expect
    print()
    print("=" * 100)
    print("SUMMARY: EXPECTED VISUAL APPEARANCE")
    print("=" * 100)
    print("""
MAP          SHOULD LOOK LIKE
----------   ---------------------------------------------------
tundra       WHITE/SNOWY - Snow biome (8) makes everything white
under        GRAY/URBAN - City biome (14) makes everything gray
beach        WARM YELLOW-GREEN - Beach biome (10) adds warm tint to savannah
default      YELLOW-GREEN - Normal savannah, no biome overlay
savannah     YELLOW-GREEN - Normal savannah
tropical     DARK GREEN - Rainforest terrain
crater       Varies by terrain type placed
island       Should have water (blue) + land mix
mars         BROWN/TAN - Desert sand terrain
rockdes      GRAY - Rock/gravel terrain
deathmtn     BROWN - Dirt terrain
arcmaze      DARK GREEN - Rainforest terrain
""")

if __name__ == '__main__':
    main()
