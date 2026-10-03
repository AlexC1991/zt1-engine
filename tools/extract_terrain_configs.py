#!/usr/bin/env python3
"""
Extract and display terrain configuration files from terrain.ztd
"""
import zipfile
from pathlib import Path

ZT_PATH = r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon"

def main():
    terrain_ztd = Path(ZT_PATH) / "terrain.ztd"

    with zipfile.ZipFile(terrain_ztd, 'r') as zf:
        # Extract the main config files
        print("=" * 70)
        print("terrain/tiletex.cfg")
        print("=" * 70)
        print(zf.read('terrain/tiletex.cfg').decode('utf-8', errors='ignore'))

        print("\n" + "=" * 70)
        print("terrain/tilevar.cfg")
        print("=" * 70)
        print(zf.read('terrain/tilevar.cfg').decode('utf-8', errors='ignore'))

        # Show sample .ani files
        print("\n" + "=" * 70)
        print("SAMPLE .ani FILES (these are text config files!)")
        print("=" * 70)

        ani_files = [f for f in zf.namelist() if f.endswith('.ani')]
        for ani in sorted(ani_files)[:5]:
            print(f"\n--- {ani} ---")
            print(zf.read(ani).decode('utf-8', errors='ignore'))

        # Show the N files (actual sprite data)
        print("\n" + "=" * 70)
        print("SAMPLE 'N' FILES (sprite data)")
        print("=" * 70)

        n_files = [f for f in zf.namelist() if f.endswith('/N')]
        for nf in sorted(n_files)[:3]:
            print(f"\n--- {nf} ({zf.getinfo(nf).file_size} bytes) ---")
            data = zf.read(nf)
            # Show first 128 bytes as hex
            for i in range(0, min(128, len(data)), 16):
                hex_str = ' '.join(f'{b:02X}' for b in data[i:i+16])
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
                print(f"  {i:04X}: {hex_str:48s} {ascii_str}")


if __name__ == '__main__':
    main()
