#!/usr/bin/env python3
"""
Zoo Tycoon 1 Live Inspector
===========================
Monitor the running ZT1 game to understand terrain rendering, sprite loading,
and internal data structures.

Features:
1. Deep memory scanning with pattern matching
2. Module/DLL analysis
3. Heap inspection
4. String table extraction
5. Pointer chain following

Requires: pip install psutil pymem

Run as Administrator!
"""
import sys
import os
import struct
import time
import ctypes
from pathlib import Path
from collections import defaultdict

try:
    import psutil
    import pymem
    import pymem.process
    from pymem import pattern
except ImportError:
    print("ERROR: Required packages not installed!")
    print("Run: pip install psutil pymem")
    sys.exit(1)

# Windows API constants
PROCESS_QUERY_INFORMATION = 0x0400
PROCESS_VM_READ = 0x0010
MEM_COMMIT = 0x1000
PAGE_READABLE = 0x02 | 0x04 | 0x20 | 0x40  # Various readable page types


def find_zoo_process():
    """Find zoo.exe process"""
    for proc in psutil.process_iter(['pid', 'name', 'exe']):
        try:
            name = proc.info['name'].lower()
            if name == 'zoo.exe':
                return proc.info['pid'], proc.info.get('exe', '')
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return None, None


def read_cstring(pm, addr, max_len=512):
    """Read null-terminated C string"""
    try:
        data = pm.read_bytes(addr, max_len)
        null_idx = data.find(b'\x00')
        if null_idx > 0:
            return data[:null_idx].decode('ascii', errors='ignore')
    except:
        pass
    return None


def is_valid_pointer(val, min_addr=0x10000, max_addr=0x7FFFFFFF):
    """Check if value looks like a valid 32-bit pointer"""
    return min_addr <= val <= max_addr


def scan_for_strings(pm, search_terms, show_context=True):
    """Scan all memory for specific strings"""
    print("\n" + "=" * 70)
    print("STRING SEARCH")
    print("=" * 70)

    results = defaultdict(list)
    regions_scanned = 0
    bytes_scanned = 0

    for region in pymem.process.enum_process_memory(pm.process_handle):
        try:
            # Skip non-readable or too large regions
            if region.RegionSize > 100 * 1024 * 1024:
                continue
            if region.State != MEM_COMMIT:
                continue

            regions_scanned += 1
            data = pm.read_bytes(region.BaseAddress, region.RegionSize)
            bytes_scanned += len(data)

            for term in search_terms:
                if isinstance(term, str):
                    term = term.encode('ascii')

                offset = 0
                while True:
                    pos = data.find(term, offset)
                    if pos == -1:
                        break

                    addr = region.BaseAddress + pos

                    # Get context around the match
                    context_start = max(0, pos - 16)
                    context_end = min(len(data), pos + len(term) + 48)
                    context = data[context_start:context_end]

                    # Clean up context for display
                    context_str = ""
                    for b in context:
                        if 32 <= b < 127:
                            context_str += chr(b)
                        else:
                            context_str += "."

                    results[term.decode('ascii', errors='ignore')].append({
                        'addr': addr,
                        'context': context_str
                    })

                    offset = pos + 1

        except Exception as e:
            continue

    print(f"Scanned {regions_scanned} regions ({bytes_scanned / 1024 / 1024:.1f} MB)")
    print()

    for term, matches in sorted(results.items()):
        print(f"  '{term}' - {len(matches)} matches:")
        for m in matches[:3]:  # Show first 3
            print(f"    0x{m['addr']:08X}: {m['context'][:60]}")
        if len(matches) > 3:
            print(f"    ... and {len(matches) - 3} more")
        print()

    return results


def find_string_tables(pm):
    """Find tables of consecutive string pointers"""
    print("\n" + "=" * 70)
    print("STRING TABLE SEARCH")
    print("=" * 70)

    tables_found = []

    for module in pm.list_modules():
        try:
            base = module.lpBaseOfDll
            size = module.SizeOfImage
            name = module.name

            if size > 10 * 1024 * 1024:  # Skip huge modules
                continue

            print(f"  Scanning {name} ({size // 1024} KB)...")

            data = pm.read_bytes(base, size)

            # Look for arrays of pointers to strings
            for i in range(0, len(data) - 64, 4):
                # Read 16 consecutive potential pointers
                ptrs = []
                for j in range(16):
                    val = struct.unpack('<I', data[i + j*4:i + j*4 + 4])[0]
                    ptrs.append(val)

                # Check if they look like valid pointers
                valid_ptrs = sum(1 for p in ptrs if is_valid_pointer(p))
                if valid_ptrs < 12:
                    continue

                # Try to read strings from these pointers
                strings = []
                for ptr in ptrs:
                    s = read_cstring(pm, ptr, 128)
                    if s and len(s) > 2:
                        strings.append(s)

                # If we got good strings, this might be a string table
                if len(strings) >= 8:
                    # Check if terrain-related
                    terrain_count = sum(1 for s in strings if 'terrain' in s.lower() or 'ic' in s.lower())
                    if terrain_count >= 4:
                        print(f"\n  POTENTIAL TERRAIN TABLE at 0x{base + i:08X}:")
                        for idx, s in enumerate(strings[:16]):
                            print(f"    [{idx:2d}] {s}")
                        tables_found.append({
                            'addr': base + i,
                            'strings': strings
                        })

        except Exception as e:
            continue

    return tables_found


def scan_data_sections(pm):
    """Scan .data and .rdata sections for terrain data"""
    print("\n" + "=" * 70)
    print("DATA SECTION ANALYSIS")
    print("=" * 70)

    for module in pm.list_modules():
        try:
            if 'zoo' not in module.name.lower():
                continue

            base = module.lpBaseOfDll
            print(f"\nAnalyzing {module.name}...")

            # Read PE header to find sections
            dos_header = pm.read_bytes(base, 64)
            if dos_header[:2] != b'MZ':
                continue

            pe_offset = struct.unpack('<I', dos_header[0x3C:0x40])[0]
            pe_header = pm.read_bytes(base + pe_offset, 256)

            if pe_header[:4] != b'PE\x00\x00':
                continue

            # Parse PE header
            num_sections = struct.unpack('<H', pe_header[6:8])[0]
            optional_header_size = struct.unpack('<H', pe_header[20:22])[0]
            section_offset = pe_offset + 24 + optional_header_size

            print(f"  PE sections: {num_sections}")

            for i in range(num_sections):
                section_data = pm.read_bytes(base + section_offset + i * 40, 40)
                sec_name = section_data[:8].rstrip(b'\x00').decode('ascii', errors='ignore')
                sec_vsize = struct.unpack('<I', section_data[8:12])[0]
                sec_vaddr = struct.unpack('<I', section_data[12:16])[0]
                sec_rawsize = struct.unpack('<I', section_data[16:20])[0]

                print(f"    {sec_name:8s} VA=0x{sec_vaddr:08X} Size={sec_vsize}")

                # Look for data in .data or .rdata sections
                if sec_name in ['.data', '.rdata', 'DATA', 'RDATA']:
                    section_bytes = pm.read_bytes(base + sec_vaddr, min(sec_vsize, 1024*1024))

                    # Search for terrain patterns
                    patterns = [b'terrain', b'icgrass', b'icsand', b'icdirt', b'icsnow',
                               b'grass', b'sand', b'dirt', b'snow', b'water']

                    for pat in patterns:
                        pos = section_bytes.find(pat)
                        if pos != -1:
                            addr = base + sec_vaddr + pos
                            context = section_bytes[max(0,pos-8):pos+64]
                            print(f"      Found '{pat.decode()}' at 0x{addr:08X}")
                            # Show readable context
                            ctx_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in context)
                            print(f"        Context: {ctx_str}")

        except Exception as e:
            print(f"  Error: {e}")
            continue


def find_terrain_constants(pm):
    """Look for terrain type constants (0-15 mappings)"""
    print("\n" + "=" * 70)
    print("TERRAIN CONSTANT SEARCH")
    print("=" * 70)

    # Look for the sequence 0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15 as DWORDs
    # This often indicates an enum or lookup table

    seq_dword = b''.join(struct.pack('<I', i) for i in range(16))
    seq_byte = bytes(range(16))

    found = []

    for module in pm.list_modules():
        try:
            base = module.lpBaseOfDll
            size = module.SizeOfImage

            if size > 10 * 1024 * 1024:
                continue

            data = pm.read_bytes(base, size)

            # Look for DWORD sequence
            pos = data.find(seq_dword)
            if pos != -1:
                print(f"  Found DWORD sequence 0-15 in {module.name} at 0x{base+pos:08X}")
                found.append(('dword', base + pos, module.name))

            # Look for byte sequence
            pos = data.find(seq_byte)
            if pos != -1:
                print(f"  Found BYTE sequence 0-15 in {module.name} at 0x{base+pos:08X}")
                found.append(('byte', base + pos, module.name))

        except:
            continue

    return found


def dump_loaded_dlls(pm):
    """List all loaded DLLs and their exports"""
    print("\n" + "=" * 70)
    print("LOADED MODULES")
    print("=" * 70)

    modules = []
    for module in pm.list_modules():
        try:
            modules.append({
                'name': module.name,
                'base': module.lpBaseOfDll,
                'size': module.SizeOfImage
            })
        except:
            continue

    modules.sort(key=lambda m: m['base'])

    for m in modules:
        print(f"  0x{m['base']:08X} - 0x{m['base']+m['size']:08X}  {m['name']}")

    return modules


def extract_all_strings(pm, min_len=4):
    """Extract all printable strings from memory"""
    print("\n" + "=" * 70)
    print("EXTRACTING ALL STRINGS (terrain/sprite related)")
    print("=" * 70)

    terrain_strings = set()
    sprite_strings = set()
    path_strings = set()

    for region in pymem.process.enum_process_memory(pm.process_handle):
        try:
            if region.RegionSize > 50 * 1024 * 1024:
                continue
            if region.State != MEM_COMMIT:
                continue

            data = pm.read_bytes(region.BaseAddress, region.RegionSize)

            # Find printable strings
            current_str = ""
            for b in data:
                if 32 <= b < 127:
                    current_str += chr(b)
                else:
                    if len(current_str) >= min_len:
                        s_lower = current_str.lower()

                        if 'terrain' in s_lower:
                            terrain_strings.add(current_str)
                        elif s_lower.startswith('ic') and len(current_str) < 20:
                            sprite_strings.add(current_str)
                        elif '.ani' in s_lower or '.pal' in s_lower or '.tga' in s_lower:
                            path_strings.add(current_str)
                        elif '/' in current_str and len(current_str) > 5:
                            # Might be a file path
                            if any(x in s_lower for x in ['animal', 'object', 'scenery', 'building']):
                                path_strings.add(current_str)

                    current_str = ""

        except:
            continue

    print(f"\nTerrain-related strings ({len(terrain_strings)}):")
    for s in sorted(terrain_strings)[:30]:
        print(f"  {s}")

    print(f"\nSprite names starting with 'ic' ({len(sprite_strings)}):")
    for s in sorted(sprite_strings)[:30]:
        print(f"  {s}")

    print(f"\nAsset paths ({len(path_strings)}):")
    for s in sorted(path_strings)[:30]:
        print(f"  {s}")

    return terrain_strings, sprite_strings, path_strings


def monitor_memory_changes(pm, duration=10):
    """Monitor memory for changes (useful for watching what happens when you paint terrain)"""
    print("\n" + "=" * 70)
    print(f"MEMORY CHANGE MONITOR ({duration} seconds)")
    print("=" * 70)
    print("Paint some terrain in the game to see what memory changes...")
    print()

    # Take initial snapshot of interesting regions
    snapshots = {}

    for module in pm.list_modules():
        try:
            if 'zoo' not in module.name.lower():
                continue
            base = module.lpBaseOfDll
            size = min(module.SizeOfImage, 2 * 1024 * 1024)  # Max 2MB
            data = pm.read_bytes(base, size)
            snapshots[module.name] = {'base': base, 'data': data}
            print(f"  Monitoring {module.name}...")
        except:
            continue

    time.sleep(duration)

    print(f"\nChecking for changes after {duration} seconds...")

    for name, snap in snapshots.items():
        try:
            new_data = pm.read_bytes(snap['base'], len(snap['data']))
            changes = []

            for i in range(0, len(snap['data']), 4):
                if snap['data'][i:i+4] != new_data[i:i+4]:
                    old_val = struct.unpack('<I', snap['data'][i:i+4])[0]
                    new_val = struct.unpack('<I', new_data[i:i+4])[0]
                    changes.append((snap['base'] + i, old_val, new_val))

            if changes:
                print(f"\n  {name}: {len(changes)} changed locations")
                for addr, old, new in changes[:10]:
                    print(f"    0x{addr:08X}: {old} -> {new}")

        except:
            continue


def main():
    print("=" * 70)
    print("ZOO TYCOON 1 LIVE INSPECTOR")
    print("=" * 70)
    print()

    # Check for admin
    try:
        is_admin = ctypes.windll.shell32.IsUserAnAdmin()
    except:
        is_admin = False

    if not is_admin:
        print("WARNING: Not running as Administrator!")
        print("Some memory regions may not be accessible.")
        print()

    # Find process
    pid, exe_path = find_zoo_process()

    if not pid:
        print("ERROR: zoo.exe not running!")
        print()
        print("Please:")
        print("  1. Start Zoo Tycoon 1")
        print("  2. Load a map (freeform or scenario)")
        print("  3. Make sure the map is visible on screen")
        print("  4. Run this script again")
        sys.exit(1)

    print(f"Found zoo.exe (PID: {pid})")
    if exe_path:
        print(f"Path: {exe_path}")
    print()

    # Attach
    try:
        pm = pymem.Pymem()
        pm.open_process_from_id(pid)
        print("Attached to process successfully!")
    except Exception as e:
        print(f"ERROR: Could not attach: {e}")
        print("Try running as Administrator.")
        sys.exit(1)

    # Menu
    while True:
        print("\n" + "=" * 70)
        print("INSPECTION OPTIONS")
        print("=" * 70)
        print("  1. Search for terrain strings")
        print("  2. Find string tables")
        print("  3. Scan data sections")
        print("  4. Find terrain constants (0-15)")
        print("  5. List loaded DLLs")
        print("  6. Extract all asset strings")
        print("  7. Monitor memory changes (10 sec)")
        print("  8. Run ALL inspections")
        print("  0. Exit")
        print()

        choice = input("Select option: ").strip()

        if choice == '1':
            terms = [
                'terrain', 'icgrass', 'icsand', 'icdirt', 'icsnow', 'icwater',
                'icgravel', 'icgrock', 'icbnrock', 'icffloor', 'icfflorc',
                'icfflord', 'icccrete', 'icaphalt', 'icdpwatr', 'icgrs_sv',
                'grass', 'sand', 'dirt', 'snow', 'water', 'gravel'
            ]
            scan_for_strings(pm, terms)

        elif choice == '2':
            find_string_tables(pm)

        elif choice == '3':
            scan_data_sections(pm)

        elif choice == '4':
            find_terrain_constants(pm)

        elif choice == '5':
            dump_loaded_dlls(pm)

        elif choice == '6':
            extract_all_strings(pm)

        elif choice == '7':
            monitor_memory_changes(pm, 10)

        elif choice == '8':
            print("\nRunning all inspections...\n")
            dump_loaded_dlls(pm)
            terms = ['terrain', 'icgrass', 'icsand', 'grass', 'sand', 'dirt']
            scan_for_strings(pm, terms)
            scan_data_sections(pm)
            find_terrain_constants(pm)
            extract_all_strings(pm)

        elif choice == '0':
            print("Exiting...")
            break
        else:
            print("Invalid option")


if __name__ == '__main__':
    main()
