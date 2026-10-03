#!/usr/bin/env python3
"""
ZT1 Map Rendering Inspector
===========================
Extract tile dimensions, grid layout, colors, and rendering parameters.
"""
import sys
import struct
import zipfile
from pathlib import Path

# Fix encoding for Windows console
sys.stdout.reconfigure(encoding='utf-8', errors='replace')

try:
    import psutil
    import pymem
except ImportError:
    print("Run: pip install psutil pymem")
    sys.exit(1)

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

def find_zoo():
    for p in psutil.process_iter(['pid', 'name']):
        try:
            if p.info['name'] and p.info['name'].lower() == 'zoo.exe':
                return p.info['pid']
        except:
            pass
    return None

def search_memory(pm, patterns):
    """Search modules for patterns"""
    results = {}
    for m in pm.list_modules():
        if m.SizeOfImage > 10_000_000:
            continue
        try:
            data = pm.read_bytes(m.lpBaseOfDll, m.SizeOfImage)
            for pattern, name in patterns:
                if isinstance(pattern, str):
                    pattern = pattern.encode()
                pos = 0
                while True:
                    pos = data.find(pattern, pos)
                    if pos == -1:
                        break
                    start = max(0, pos - 10)
                    end = min(len(data), pos + len(pattern) + 80)
                    ctx = data[start:end]
                    text = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ctx)
                    if name not in results:
                        results[name] = []
                    results[name].append({
                        'module': m.name,
                        'addr': m.lpBaseOfDll + pos,
                        'context': text
                    })
                    pos += 1
                    if len(results.get(name, [])) >= 5:
                        break
        except:
            pass
    return results

def extract_terrain_config():
    """Extract terrain tiletex.cfg"""
    print("\n" + "=" * 70)
    print("TERRAIN TYPE CONFIG (tiletex.cfg)")
    print("=" * 70)

    terrain_ztd = ZT_PATH / "terrain.ztd"
    if terrain_ztd.exists():
        with zipfile.ZipFile(terrain_ztd, 'r') as zf:
            try:
                content = zf.read('terrain/tiletex.cfg').decode('utf-8', errors='ignore')
                print(content)
            except:
                print("Could not read tiletex.cfg")

def extract_ui_config():
    """Extract UI/rendering config from ui.ztd"""
    print("\n" + "=" * 70)
    print("UI CONFIG FILES")
    print("=" * 70)

    ui_ztd = ZT_PATH / "ui.ztd"
    if ui_ztd.exists():
        with zipfile.ZipFile(ui_ztd, 'r') as zf:
            for name in zf.namelist():
                nl = name.lower()
                if nl.endswith('.cfg') or nl.endswith('.ini'):
                    try:
                        content = zf.read(name).decode('utf-8', errors='ignore')
                        if len(content) < 2000:
                            print(f"\n--- {name} ---")
                            print(content)
                    except:
                        pass

def analyze_tile_sprites():
    """Analyze terrain sprite dimensions from N files"""
    print("\n" + "=" * 70)
    print("TERRAIN SPRITE ANALYSIS (from terrain.ztd)")
    print("=" * 70)

    terrain_ztd = ZT_PATH / "terrain.ztd"
    if not terrain_ztd.exists():
        print("terrain.ztd not found")
        return

    with zipfile.ZipFile(terrain_ztd, 'r') as zf:
        # Find all N files (sprite data)
        n_files = [f for f in zf.namelist() if f.endswith('/N')]

        print(f"\nFound {len(n_files)} terrain sprite files\n")

        for nf in sorted(n_files):
            data = zf.read(nf)

            # Parse FATZ header
            if data[:4] == b'FATZ':
                print(f"{nf}:")
                print(f"  Magic: FATZ")
                print(f"  Size: {len(data)} bytes")

                # Header structure analysis
                # Offset 0x30-0x3F typically has sprite metadata
                if len(data) >= 0x40:
                    # Width and Height at different offsets
                    w1 = struct.unpack('<H', data[0x34:0x36])[0]
                    h1 = struct.unpack('<H', data[0x36:0x38])[0]
                    w2 = struct.unpack('<H', data[0x38:0x3A])[0]
                    h2 = struct.unpack('<H', data[0x3A:0x3C])[0]

                    print(f"  Offset 0x34: {w1} x {h1}")
                    print(f"  Offset 0x38: {w2} x {h2}")

                    # The x0,y0,x1,y1 from .ani files suggest 44x32 bounding box
                    # -22 to 22 = 44, -16 to 16 = 32

                # Find palette path
                pal_start = data.find(b'terrain/')
                if pal_start != -1:
                    pal_end = data.find(b'\x00', pal_start)
                    pal_path = data[pal_start:pal_end].decode('ascii', errors='ignore')
                    print(f"  Palette: {pal_path}")

                print()

        # Also check .ani files for sprite bounds
        print("\n" + "-" * 50)
        print("SPRITE BOUNDS FROM .ani FILES:")
        print("-" * 50)

        ani_files = [f for f in zf.namelist() if f.endswith('.ani')]
        for ani in sorted(ani_files)[:5]:
            content = zf.read(ani).decode('utf-8', errors='ignore')
            print(f"\n{ani}:")
            print(content)

def analyze_map_files():
    """Analyze .zoo map file structure"""
    print("\n" + "=" * 70)
    print("MAP FILE ANALYSIS")
    print("=" * 70)

    maps_dir = ZT_PATH / "maps"
    if not maps_dir.exists():
        print("maps directory not found")
        return

    zoo_files = list(maps_dir.glob("*.zoo"))
    print(f"\nFound {len(zoo_files)} map files\n")

    # Terrain type names
    terrain_names = {
        0: "Grass", 1: "Savannah", 2: "Sand", 3: "Dirt",
        4: "Rainforest", 5: "Brown Rock", 6: "Gray Rock", 7: "Gravel",
        8: "Snow", 9: "Fresh Water", 10: "Salt Water", 11: "Deciduous",
        12: "Waterfall", 13: "Coniferous", 14: "Concrete", 15: "Asphalt"
    }

    for zoo_file in sorted(zoo_files)[:8]:
        with open(zoo_file, 'rb') as f:
            data = f.read()

        print(f"{zoo_file.name}:")

        # Parse header
        magic = data[0:4]
        print(f"  Magic: {magic}")

        # Dimensions at offset 0x0C and 0x10
        width = struct.unpack('<I', data[0x0C:0x10])[0]
        height = struct.unpack('<I', data[0x10:0x14])[0]
        print(f"  Map size: {width} x {height} tiles")

        # Base terrain
        base_terrain = data[0x20]
        base_type = base_terrain & 0x0F
        base_flags = (base_terrain >> 4) & 0x0F
        terrain_name = terrain_names.get(base_type, "Unknown")
        print(f"  Base terrain: {base_terrain} -> type {base_type} ({terrain_name}), flags 0x{base_flags:X}")

        # Calculate bytes per tile
        header_size = 100
        tile_data_size = len(data) - header_size
        if width > 0 and height > 0:
            bytes_per_tile = tile_data_size // (width * height)
            print(f"  Bytes per tile: {bytes_per_tile}")

        # Sample first few tiles
        print(f"  Sample tiles (first 5):")
        for i in range(min(5, width * height)):
            offset = header_size + i * 10  # 10 bytes per tile
            if offset + 10 <= len(data):
                tile = data[offset:offset+10]
                terrain_byte = tile[0]
                elevation = tile[1] & 0x1F
                terrain_type = terrain_byte & 0x0F
                terrain_flags = (terrain_byte >> 4) & 0x0F
                t_name = terrain_names.get(terrain_type, "?")
                print(f"    Tile {i}: terrain={terrain_type}({t_name}) elev={elevation} flags=0x{terrain_flags:X} raw={tile.hex()}")

        print()

def analyze_zoo_ini():
    """Read zoo.ini for map/rendering settings"""
    print("\n" + "=" * 70)
    print("ZOO.INI SETTINGS")
    print("=" * 70)

    ini_path = ZT_PATH / "zoo.ini"
    if ini_path.exists():
        content = ini_path.read_text(errors='ignore')
        # Show relevant sections
        for line in content.split('\n'):
            line = line.strip()
            if line and not line.startswith(';'):
                print(f"  {line}")

def main():
    print("=" * 70)
    print("ZT1 MAP & RENDERING DATA EXTRACTION")
    print("=" * 70)

    # Static file analysis
    analyze_zoo_ini()
    extract_terrain_config()
    analyze_tile_sprites()
    analyze_map_files()

    # Memory scan if game is running
    pid = find_zoo()
    if not pid:
        print("\nzoo.exe not running - skipping memory scan")
        print("\n" + "=" * 70)
        print("DONE")
        print("=" * 70)
        return

    print(f"\n{'='*70}")
    print(f"MEMORY SCAN (zoo.exe PID: {pid})")
    print("=" * 70)

    pm = pymem.Pymem(pid)

    # Search for map-related strings
    patterns = [
        (b'mapX=', 'mapX'),
        (b'mapY=', 'mapY'),
        (b'Map.', 'Map config'),
        (b'tile', 'tile'),
        (b'Tile', 'Tile'),
        (b'grid', 'grid'),
        (b'Grid', 'Grid'),
        (b'scroll', 'scroll'),
        (b'Scroll', 'Scroll'),
        (b'camera', 'camera'),
        (b'Camera', 'Camera'),
        (b'zoom', 'zoom'),
        (b'Zoom', 'Zoom'),
        (b'elevation', 'elevation'),
        (b'Elevation', 'Elevation'),
        (b'screen', 'screen'),
        (b'Screen', 'Screen'),
        (b'width', 'width'),
        (b'height', 'height'),
        (b'Width', 'Width'),
        (b'Height', 'Height'),
    ]

    results = search_memory(pm, patterns)

    for name, matches in sorted(results.items()):
        print(f"\n[{name}]:")
        for m in matches[:3]:
            ctx = m['context'][:70]
            print(f"  {m['module']:15s} 0x{m['addr']:08X}: {ctx}")

    # Look for specific tile dimension values
    print(f"\n{'='*70}")
    print("SEARCHING FOR TILE SIZE CONSTANTS (64x32, etc)")
    print("=" * 70)

    found_dims = []
    for m in pm.list_modules():
        if 'zoo' not in m.name.lower():
            continue
        try:
            data = pm.read_bytes(m.lpBaseOfDll, m.SizeOfImage)

            # Search for common isometric tile dimensions
            # 64x32 is the standard
            for i in range(0, len(data) - 8, 2):
                v1 = struct.unpack('<H', data[i:i+2])[0]
                v2 = struct.unpack('<H', data[i+2:i+4])[0]

                # 64x32 pattern
                if v1 == 64 and v2 == 32:
                    addr = m.lpBaseOfDll + i
                    if addr not in [f[0] for f in found_dims]:
                        found_dims.append((addr, 64, 32))
                        if len(found_dims) <= 10:
                            ctx = ' '.join(f'{b:02X}' for b in data[max(0,i-4):i+12])
                            print(f"  64x32 at 0x{addr:08X}: {ctx}")

                # 32x16 pattern
                if v1 == 32 and v2 == 16:
                    addr = m.lpBaseOfDll + i
                    if len(found_dims) <= 15:
                        ctx = ' '.join(f'{b:02X}' for b in data[max(0,i-4):i+12])
                        print(f"  32x16 at 0x{addr:08X}: {ctx}")

        except:
            pass

    print(f"\n{'='*70}")
    print("DONE")
    print("=" * 70)

if __name__ == '__main__':
    main()
