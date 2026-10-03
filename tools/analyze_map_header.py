#!/usr/bin/env python3
"""
Deep analysis of .zoo map file headers to find biome/theme data.
We need to understand what makes tundra look snowy and under look like a city.
"""
import struct
from pathlib import Path
import sys

sys.stdout.reconfigure(encoding='utf-8', errors='replace')

ZT_PATH = Path(r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")

def hex_dump(data, start=0, length=None):
    """Pretty hex dump"""
    if length:
        data = data[start:start+length]
    else:
        data = data[start:]

    lines = []
    for i in range(0, len(data), 16):
        chunk = data[i:i+16]
        hex_part = ' '.join(f'{b:02X}' for b in chunk)
        ascii_part = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        lines.append(f"  {start+i:04X}: {hex_part:<48} {ascii_part}")
    return '\n'.join(lines)

def analyze_header(map_path):
    """Deep analysis of map header"""
    with open(map_path, 'rb') as f:
        data = f.read()

    name = map_path.stem

    print(f"\n{'='*80}")
    print(f"MAP: {name}.zoo ({len(data)} bytes)")
    print(f"{'='*80}")

    # Full header dump (first 128 bytes)
    print("\nHEADER HEX DUMP (first 128 bytes):")
    print(hex_dump(data, 0, 128))

    # Parse known fields
    print("\nKNOWN FIELDS:")
    print(f"  0x00-0x03: Magic = {data[0:4]} ({data[0:4].hex()})")

    # Various 4-byte values in header
    for offset in range(4, 64, 4):
        val = struct.unpack('<I', data[offset:offset+4])[0]
        val_signed = struct.unpack('<i', data[offset:offset+4])[0]
        if val != 0:
            print(f"  0x{offset:02X}-0x{offset+3:02X}: {val:10d} (0x{val:08X}) signed={val_signed}")

    # Specific important offsets
    width = struct.unpack('<I', data[0x0C:0x10])[0]
    height = struct.unpack('<I', data[0x10:0x14])[0]
    print(f"\n  Map Size: {width} x {height}")

    base_terrain = data[0x20]
    print(f"  Base Terrain (0x20): {base_terrain} (type {base_terrain & 0x0F})")

    # Look for map type / biome indicator
    # Check bytes around 0x24-0x30 which might have theme data
    print("\nPOTENTIAL THEME/BIOME DATA (0x20-0x40):")
    for i in range(0x20, min(0x40, len(data))):
        val = data[i]
        if val != 0:
            print(f"  0x{i:02X}: {val:3d} (0x{val:02X})")

    # Check if there's string data after header
    print("\nSEARCHING FOR EMBEDDED STRINGS:")
    for i in range(0, min(500, len(data))):
        # Look for printable string sequences
        if data[i:i+4] in [b'tund', b'city', b'snow', b'urba', b'biom', b'them']:
            end = i
            while end < len(data) and 32 <= data[end] < 127:
                end += 1
            s = data[i:end].decode('ascii', errors='ignore')
            print(f"  Found at 0x{i:04X}: '{s}'")

    return data

def compare_maps():
    """Compare headers of different themed maps"""
    maps_to_compare = [
        'tundra',    # Snow theme
        'under',     # City theme
        'default',   # Normal grass
        'beach',     # Beach theme
        'mars',      # Desert/mars theme
        'arcmaze',   # Rainforest
    ]

    maps_dir = ZT_PATH / "maps"

    headers = {}
    for map_name in maps_to_compare:
        map_path = maps_dir / f"{map_name}.zoo"
        if map_path.exists():
            headers[map_name] = analyze_header(map_path)

    # Compare specific bytes across maps
    print("\n" + "="*80)
    print("BYTE-BY-BYTE COMPARISON OF KEY OFFSETS")
    print("="*80)

    print(f"\n{'Offset':<8}", end='')
    for name in headers.keys():
        print(f"{name:<12}", end='')
    print()
    print("-" * 80)

    # Compare bytes 0x00-0x40
    for offset in range(0x00, 0x44, 4):
        print(f"0x{offset:02X}    ", end='')
        for name, data in headers.items():
            if offset + 4 <= len(data):
                val = struct.unpack('<I', data[offset:offset+4])[0]
                if val < 1000:
                    print(f"{val:<12}", end='')
                else:
                    print(f"0x{val:08X}  ", end='')
            else:
                print(f"{'N/A':<12}", end='')
        print()

    # Look for the map type field (might indicate theme)
    print("\n" + "="*80)
    print("LOOKING FOR MAP TYPE / THEME INDICATOR")
    print("="*80)

    # Offset 0x24 might be map type based on earlier findings
    for name, data in headers.items():
        if len(data) >= 0x28:
            map_type = struct.unpack('<I', data[0x24:0x28])[0]
            print(f"  {name}: map_type @ 0x24 = {map_type}")

if __name__ == '__main__':
    compare_maps()
