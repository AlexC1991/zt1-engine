#!/usr/bin/env python3
"""
Zoo Tycoon 1 Inspector - Simple & Robust
========================================
Attach to running zoo.exe and extract terrain/sprite data.

Usage:
  1. Start Zoo Tycoon 1 and load any map
  2. Run: python inspect_zt1.py

Requires: pip install psutil pymem
"""
import sys
import struct
import ctypes

try:
    import psutil
    import pymem
    import pymem.process
except ImportError:
    print("Install dependencies: pip install psutil pymem")
    sys.exit(1)


def find_zoo():
    """Find zoo.exe process"""
    for p in psutil.process_iter(['pid', 'name', 'exe']):
        try:
            if p.info['name'] and p.info['name'].lower() == 'zoo.exe':
                return p.info['pid'], p.info.get('exe', 'unknown')
        except:
            pass
    return None, None


def safe_read(pm, addr, size):
    """Safely read memory"""
    try:
        return pm.read_bytes(addr, size)
    except:
        return None


def read_string(pm, addr, maxlen=256):
    """Read null-terminated string"""
    data = safe_read(pm, addr, maxlen)
    if not data:
        return None
    try:
        end = data.find(b'\x00')
        if end > 0:
            return data[:end].decode('ascii', errors='ignore')
    except:
        pass
    return None


def scan_memory(pm):
    """Scan all readable memory for terrain strings"""
    print("\n[*] Scanning memory for terrain data...")

    search_patterns = [
        b'terrain/',
        b'terrain\\',
        b'icgrass',
        b'icsand',
        b'icdirt',
        b'icsnow',
        b'icwater',
        b'icgravel',
        b'icgrock',
        b'icbnrock',
        b'icffloor',
        b'icgrs_sv',
        b'icdpwatr',
        b'icfflorc',
        b'icfflord',
        b'icccrete',
        b'icaphalt',
        b'.ani',
        b'.pal',
    ]

    results = {}
    total_scanned = 0

    try:
        regions = list(pymem.process.enum_process_memory(pm.process_handle))
        print(f"    Found {len(regions)} memory regions")

        for region in regions:
            try:
                # Skip non-committed or huge regions
                if region.State != 0x1000:  # MEM_COMMIT
                    continue
                if region.RegionSize > 100_000_000:
                    continue

                data = pm.read_bytes(region.BaseAddress, region.RegionSize)
                total_scanned += len(data)

                for pattern in search_patterns:
                    offset = 0
                    while True:
                        pos = data.find(pattern, offset)
                        if pos == -1:
                            break

                        # Extract surrounding context
                        start = max(0, pos - 20)
                        end = min(len(data), pos + 80)
                        context = data[start:end]

                        # Try to extract full string
                        str_start = pos
                        while str_start > 0 and data[str_start-1] >= 32 and data[str_start-1] < 127:
                            str_start -= 1
                        str_end = pos
                        while str_end < len(data) and data[str_end] >= 32 and data[str_end] < 127:
                            str_end += 1

                        full_str = data[str_start:str_end].decode('ascii', errors='ignore')

                        addr = region.BaseAddress + pos
                        key = pattern.decode('ascii', errors='ignore')

                        if key not in results:
                            results[key] = []
                        if len(results[key]) < 5:  # Limit per pattern
                            results[key].append({
                                'addr': addr,
                                'string': full_str,
                                'context': context
                            })

                        offset = pos + 1

            except Exception as e:
                continue

    except Exception as e:
        print(f"    Error scanning: {e}")

    print(f"    Scanned {total_scanned / 1024 / 1024:.1f} MB")
    return results


def find_pointer_arrays(pm):
    """Find arrays of pointers to strings (potential sprite tables)"""
    print("\n[*] Searching for sprite pointer arrays...")

    found_tables = []

    for module in pm.list_modules():
        try:
            name = module.name
            base = module.lpBaseOfDll
            size = module.SizeOfImage

            # Focus on zoo.exe and related DLLs
            if size > 5_000_000:
                continue

            data = pm.read_bytes(base, size)

            # Look for sequences of 4-byte values that could be pointers
            for i in range(0, len(data) - 64, 4):
                ptrs = []
                for j in range(16):
                    val = struct.unpack('<I', data[i + j*4 : i + j*4 + 4])[0]
                    ptrs.append(val)

                # Check if they look like valid pointers
                valid = sum(1 for p in ptrs if 0x400000 <= p <= 0x7FFFFFFF)
                if valid < 10:
                    continue

                # Try to read strings
                strings = []
                for ptr in ptrs:
                    s = read_string(pm, ptr, 64)
                    if s and len(s) > 2:
                        strings.append(s)

                # Check for terrain-related strings
                terrain_count = sum(1 for s in strings if
                    'terrain' in s.lower() or
                    s.lower().startswith('ic') or
                    'grass' in s.lower() or
                    'sand' in s.lower())

                if terrain_count >= 3:
                    found_tables.append({
                        'module': name,
                        'addr': base + i,
                        'strings': strings[:16]
                    })

        except:
            continue

    return found_tables


def extract_all_paths(pm):
    """Extract all file path strings"""
    print("\n[*] Extracting asset paths...")

    paths = set()

    for region in pymem.process.enum_process_memory(pm.process_handle):
        try:
            if region.State != 0x1000:
                continue
            if region.RegionSize > 50_000_000:
                continue

            data = pm.read_bytes(region.BaseAddress, region.RegionSize)

            # Find printable strings
            current = b''
            for byte in data:
                if 32 <= byte < 127:
                    current += bytes([byte])
                else:
                    if len(current) >= 6:
                        s = current.decode('ascii', errors='ignore')
                        # Check if it's a path
                        if ('/' in s or '\\' in s) and any(ext in s.lower() for ext in ['.ani', '.pal', '.tga', '.wav', '.ztd']):
                            paths.add(s)
                        elif s.lower().startswith('ic') and len(s) < 20:
                            paths.add(s)
                        elif 'terrain' in s.lower():
                            paths.add(s)
                    current = b''

        except:
            continue

    return sorted(paths)


def list_modules(pm):
    """List loaded modules"""
    print("\n[*] Loaded modules:")

    modules = []
    for m in pm.list_modules():
        try:
            modules.append({
                'name': m.name,
                'base': m.lpBaseOfDll,
                'size': m.SizeOfImage
            })
        except:
            pass

    modules.sort(key=lambda x: x['base'])

    for m in modules:
        size_kb = m['size'] // 1024
        print(f"    0x{m['base']:08X}  {size_kb:6d} KB  {m['name']}")

    return modules


def main():
    print("=" * 60)
    print("ZOO TYCOON 1 INSPECTOR")
    print("=" * 60)

    # Check admin
    try:
        admin = ctypes.windll.shell32.IsUserAnAdmin()
    except:
        admin = False

    if not admin:
        print("\n[!] WARNING: Run as Administrator for full access!")

    # Find process
    pid, exe = find_zoo()

    if not pid:
        print("\n[!] zoo.exe not found!")
        print("    Start Zoo Tycoon 1 and load a map first.")
        sys.exit(1)

    print(f"\n[+] Found zoo.exe (PID: {pid})")
    print(f"    Path: {exe}")

    # Attach
    try:
        pm = pymem.Pymem()
        pm.open_process_from_id(pid)
        print("[+] Attached successfully")
    except Exception as e:
        print(f"[!] Failed to attach: {e}")
        sys.exit(1)

    # List modules
    modules = list_modules(pm)

    # Scan for terrain data
    results = scan_memory(pm)

    if results:
        print("\n[+] Found terrain-related strings:")
        for pattern, matches in sorted(results.items()):
            print(f"\n    Pattern: '{pattern}'")
            for m in matches:
                s = m['string'][:60] if len(m['string']) > 60 else m['string']
                print(f"      0x{m['addr']:08X}: {s}")
    else:
        print("\n[-] No terrain strings found in memory scan")

    # Find pointer tables
    tables = find_pointer_arrays(pm)

    if tables:
        print("\n[+] Found potential sprite tables:")
        for t in tables:
            print(f"\n    Module: {t['module']} @ 0x{t['addr']:08X}")
            for idx, s in enumerate(t['strings']):
                print(f"      [{idx:2d}] {s}")
    else:
        print("\n[-] No sprite pointer tables found")

    # Extract paths
    paths = extract_all_paths(pm)

    terrain_paths = [p for p in paths if 'terrain' in p.lower() or p.lower().startswith('ic')]

    if terrain_paths:
        print(f"\n[+] Terrain-related paths ({len(terrain_paths)}):")
        for p in terrain_paths[:30]:
            print(f"    {p}")
        if len(terrain_paths) > 30:
            print(f"    ... and {len(terrain_paths) - 30} more")

    other_paths = [p for p in paths if p not in terrain_paths][:20]
    if other_paths:
        print(f"\n[+] Other asset paths ({len(other_paths)} shown):")
        for p in other_paths:
            print(f"    {p}")

    print("\n" + "=" * 60)
    print("INSPECTION COMPLETE")
    print("=" * 60)


if __name__ == '__main__':
    main()
