#!/usr/bin/env python3
"""
Dump contents of terrain.ztd to understand sprite structure.
ZTD files are just ZIP archives.
"""
import zipfile
import sys
from pathlib import Path
from collections import defaultdict

ZT_PATH = r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon"

def dump_ztd(ztd_path):
    """List all files in a ZTD archive"""
    print(f"\n{'='*60}")
    print(f"CONTENTS OF: {Path(ztd_path).name}")
    print(f"{'='*60}\n")

    try:
        with zipfile.ZipFile(ztd_path, 'r') as zf:
            files = zf.namelist()
            print(f"Total files: {len(files)}\n")

            # Group by directory
            by_dir = defaultdict(list)
            for f in files:
                parts = f.replace('\\', '/').split('/')
                if len(parts) > 1:
                    dir_name = parts[0]
                else:
                    dir_name = '.'
                by_dir[dir_name].append(f)

            # Show structure
            for dir_name in sorted(by_dir.keys()):
                dir_files = by_dir[dir_name]
                print(f"\n[{dir_name}/] ({len(dir_files)} files)")

                # Show files by extension
                by_ext = defaultdict(list)
                for f in dir_files:
                    ext = Path(f).suffix.lower()
                    by_ext[ext].append(f)

                for ext in sorted(by_ext.keys()):
                    ext_files = by_ext[ext]
                    print(f"  {ext or '(no ext)'}: {len(ext_files)} files")
                    # Show first few
                    for f in ext_files[:5]:
                        info = zf.getinfo(f)
                        print(f"    {f} ({info.file_size} bytes)")
                    if len(ext_files) > 5:
                        print(f"    ... and {len(ext_files) - 5} more")

            # Show all terrain subdirectories in detail
            print(f"\n{'='*60}")
            print("TERRAIN SUBDIRECTORIES IN DETAIL")
            print(f"{'='*60}")

            terrain_dirs = [f for f in files if f.startswith('terrain/') or f.startswith('terrain\\')]

            # Group by subdirectory
            subdirs = defaultdict(list)
            for f in terrain_dirs:
                parts = f.replace('\\', '/').split('/')
                if len(parts) >= 2:
                    subdir = parts[1]
                    subdirs[subdir].append(f)

            for subdir in sorted(subdirs.keys()):
                subdir_files = subdirs[subdir]
                print(f"\n  terrain/{subdir}/")
                for f in sorted(subdir_files):
                    info = zf.getinfo(f)
                    fname = Path(f).name
                    print(f"    {fname:20s} {info.file_size:8d} bytes")

            return files

    except Exception as e:
        print(f"Error: {e}")
        return []


def extract_config_files(ztd_path, output_dir):
    """Extract .cfg and .ini files for inspection"""
    print(f"\nExtracting config files...")

    try:
        with zipfile.ZipFile(ztd_path, 'r') as zf:
            for f in zf.namelist():
                if f.lower().endswith(('.cfg', '.ini', '.txt')):
                    print(f"  Extracting: {f}")
                    zf.extract(f, output_dir)
    except Exception as e:
        print(f"Error: {e}")


def analyze_ani_file(ztd_path, ani_path):
    """Read and analyze an .ani file structure"""
    print(f"\n{'='*60}")
    print(f"ANALYZING: {ani_path}")
    print(f"{'='*60}")

    try:
        with zipfile.ZipFile(ztd_path, 'r') as zf:
            data = zf.read(ani_path)
            print(f"File size: {len(data)} bytes")
            print(f"\nFirst 64 bytes (hex):")

            for i in range(0, min(64, len(data)), 16):
                hex_str = ' '.join(f'{b:02X}' for b in data[i:i+16])
                ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
                print(f"  {i:04X}: {hex_str:48s} {ascii_str}")

            # Try to parse header
            if len(data) >= 16:
                print(f"\nPotential header values:")
                for i in range(0, 16, 4):
                    val = int.from_bytes(data[i:i+4], 'little')
                    print(f"  Offset {i:2d}: {val:10d} (0x{val:08X})")

    except Exception as e:
        print(f"Error: {e}")


def main():
    terrain_ztd = Path(ZT_PATH) / "terrain.ztd"

    if not terrain_ztd.exists():
        print(f"Not found: {terrain_ztd}")
        sys.exit(1)

    # Dump contents
    files = dump_ztd(terrain_ztd)

    # Find and analyze a sample .ani file
    ani_files = [f for f in files if f.lower().endswith('.ani')]
    if ani_files:
        print(f"\n\nFound {len(ani_files)} .ani files")
        # Analyze first terrain ani
        terrain_ani = [f for f in ani_files if 'icgrass' in f.lower()]
        if terrain_ani:
            analyze_ani_file(terrain_ztd, terrain_ani[0])
        elif ani_files:
            analyze_ani_file(terrain_ztd, ani_files[0])

    # Also check other ZTDs for terrain config
    print(f"\n\n{'='*60}")
    print("CHECKING OTHER ZTDS FOR TERRAIN CONFIG")
    print(f"{'='*60}")

    for ztd_name in ['global.ztd', 'config.ztd', 'ai.ztd']:
        ztd_path = Path(ZT_PATH) / ztd_name
        if ztd_path.exists():
            try:
                with zipfile.ZipFile(ztd_path, 'r') as zf:
                    terrain_files = [f for f in zf.namelist() if 'terrain' in f.lower()]
                    if terrain_files:
                        print(f"\n{ztd_name} has terrain files:")
                        for f in terrain_files[:10]:
                            print(f"  {f}")
            except:
                pass


if __name__ == '__main__':
    main()
