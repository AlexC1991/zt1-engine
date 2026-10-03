#!/usr/bin/env python3
"""
Memory inspector for Zoo Tycoon 1 process.
Attaches to running zoo.exe and extracts runtime data about terrain mappings.

Requires: pip install psutil pymem
"""
import sys
import struct
import time
from pathlib import Path

try:
    import psutil
    import pymem
    import pymem.process
except ImportError:
    print("ERROR: Required packages not installed!")
    print("Please run: pip install psutil pymem")
    sys.exit(1)

def find_zoo_process():
    """Find the running zoo.exe process"""
    for proc in psutil.process_iter(['pid', 'name', 'exe']):
        try:
            if proc.info['name'].lower() == 'zoo.exe':
                exe_path = proc.info.get('exe', '')
                print(f"  Found: {proc.info['name']} (PID: {proc.info['pid']})")
                if exe_path:
                    print(f"  Path: {exe_path}")
                return proc.info['pid']
        except (psutil.NoSuchProcess, psutil.AccessDenied):
            continue
    return None

def read_string(pm, address, max_length=256):
    """Read a null-terminated string from memory"""
    try:
        bytes_data = pm.read_bytes(address, max_length)
        # Find null terminator
        null_pos = bytes_data.find(b'\x00')
        if null_pos != -1:
            return bytes_data[:null_pos].decode('ascii', errors='ignore')
        return bytes_data.decode('ascii', errors='ignore')
    except:
        return None

def search_for_terrain_strings(pm):
    """Search process memory for terrain-related strings"""
    print("=" * 80)
    print("SEARCHING FOR TERRAIN SPRITE PATHS IN MEMORY")
    print("=" * 80)
    print()

    # Strings we're looking for - try shorter patterns that might match
    search_strings = [
        b'terrain/icgrass',
        b'terrain/icsand',
        b'terrain/icdirt',
        b'terrain/icsnow',
        b'terrain/icwater',
        b'terrain/icgravel',
        b'terrain/icgrock',
        b'terrain/icbnrock',
        b'icgrass',
        b'icsand',
        b'icgrs_sv',
        b'icdirt',
        b'icsnow',
        b'icwater',
        b'icgravel',
        b'icgrock',
        b'icbnrock',
        b'icdpwatr',
        b'icffloor',
        b'icfflorc',
        b'icfflord',
        b'icccrete',
        b'icaphalt',
    ]

    print("Scanning ALL process memory (this may take a moment)...")
    found_addresses = {}

    try:
        # Use pymem's memory_regions to scan ALL readable memory
        regions_scanned = 0
        for region in pymem.process.enum_process_memory(pm.process_handle):
            try:
                # Only scan readable regions
                if not (region.Protect & 0x04):  # PAGE_READWRITE or PAGE_READONLY
                    continue

                base_address = region.BaseAddress
                size = region.RegionSize

                # Skip huge regions
                if size > 50 * 1024 * 1024:  # Skip > 50MB
                    continue

                regions_scanned += 1

                # Read this memory region
                data = pm.read_bytes(base_address, size)

                # Search for our strings
                for search_str in search_strings:
                    offset = 0
                    while True:
                        pos = data.find(search_str, offset)
                        if pos == -1:
                            break

                        addr = base_address + pos
                        string_val = search_str.decode('ascii')

                        if string_val not in found_addresses:
                            found_addresses[string_val] = []
                        found_addresses[string_val].append(hex(addr))

                        offset = pos + 1

            except Exception as e:
                continue

        print(f"Scanned {regions_scanned} memory regions")

    except Exception as e:
        print(f"Error scanning memory: {e}")

    if found_addresses:
        print("Found terrain-related strings in memory:")
        print()
        for string, addresses in sorted(found_addresses.items()):
            print(f"  '{string}':")
            for addr in addresses[:5]:  # Show first 5 occurrences
                print(f"    {addr}")
            if len(addresses) > 5:
                print(f"    ... and {len(addresses) - 5} more")
        print()
    else:
        print("No terrain strings found in memory.")
        print()

    return found_addresses

def search_for_terrain_array(pm):
    """Try to find an array of terrain sprite paths"""
    print("=" * 80)
    print("SEARCHING FOR TERRAIN ARRAY STRUCTURE")
    print("=" * 80)
    print()

    print("Looking for patterns that might indicate terrain ID -> sprite mapping...")
    print()

    # Try to find a sequence of pointers that might be the terrain array
    # In a typical C++ array: [ptr0, ptr1, ptr2, ...] where each ptr points to a string

    try:
        for region in pm.list_modules():
            try:
                base_address = region.lpBaseOfDll
                size = region.SizeOfImage

                # Only check main executable regions
                if size > 5 * 1024 * 1024:  # Skip if > 5MB
                    continue

                data = pm.read_bytes(base_address, size)

                # Look for sequences of 16 consecutive 4-byte values that might be pointers
                for i in range(0, len(data) - 64, 4):
                    # Read 16 potential pointers
                    pointers = []
                    valid = True

                    for j in range(16):
                        offset = i + (j * 4)
                        if offset + 4 > len(data):
                            valid = False
                            break

                        ptr = struct.unpack('<I', data[offset:offset+4])[0]

                        # Check if this looks like a valid pointer (reasonable range)
                        if ptr < 0x00400000 or ptr > 0x10000000:
                            valid = False
                            break

                        pointers.append(ptr)

                    if not valid:
                        continue

                    # Try to read strings from these pointers
                    strings = []
                    for ptr in pointers:
                        try:
                            s = read_string(pm, ptr, 64)
                            if s and 'terrain' in s.lower():
                                strings.append(s)
                        except:
                            pass

                    # If we found multiple terrain strings, this might be the array!
                    if len(strings) >= 8:
                        print(f"POTENTIAL TERRAIN ARRAY FOUND at {hex(base_address + i)}:")
                        print()
                        for idx, s in enumerate(strings):
                            print(f"  [{idx:2d}] {s}")
                        print()
                        return base_address + i

            except Exception as e:
                continue

    except Exception as e:
        print(f"Error searching for array: {e}")

    print("Could not find terrain array structure.")
    print()
    return None

def main():
    print("=" * 80)
    print("ZOO TYCOON 1 PROCESS INSPECTOR")
    print("=" * 80)
    print()

    # Find zoo.exe process
    print("Looking for zoo.exe process...")
    pid = find_zoo_process()

    if not pid:
        print("ERROR: zoo.exe is not running!")
        print()
        print("Please start Zoo Tycoon 1 first, then run this script.")
        print("The game should be running and showing a map.")
        sys.exit(1)

    print(f"Found zoo.exe (PID: {pid})")
    print()

    try:
        # Attach to process
        print("Attaching to process...")
        pm = pymem.Pymem()
        pm.open_process_from_id(pid)
        print("Successfully attached!")
        print()

        # Search for terrain strings
        found_strings = search_for_terrain_strings(pm)

        # Try to find terrain array
        array_addr = search_for_terrain_array(pm)

        # If nothing found, try a broader search
        if not found_strings and not array_addr:
            print("=" * 80)
            print("BROAD SEARCH: Looking for ANY 'terrain' references")
            print("=" * 80)
            print()

            try:
                terrain_refs = []
                for region in pymem.process.enum_process_memory(pm.process_handle):
                    try:
                        if not (region.Protect & 0x04):
                            continue
                        if region.RegionSize > 10 * 1024 * 1024:
                            continue

                        data = pm.read_bytes(region.BaseAddress, region.RegionSize)

                        # Look for "terrain" (case insensitive)
                        for match_pattern in [b'terrain', b'TERRAIN', b'Terrain']:
                            pos = data.find(match_pattern)
                            if pos != -1:
                                # Try to read surrounding context
                                start = max(0, pos - 20)
                                end = min(len(data), pos + 50)
                                context = data[start:end].decode('ascii', errors='replace')
                                terrain_refs.append((hex(region.BaseAddress + pos), context))
                                if len(terrain_refs) > 20:  # Limit output
                                    break
                    except:
                        continue

                if terrain_refs:
                    print(f"Found {len(terrain_refs)} references to 'terrain':")
                    for addr, context in terrain_refs[:20]:
                        print(f"  {addr}: {repr(context)}")
                    print()
                else:
                    print("No 'terrain' references found anywhere in memory.")
                    print()
            except Exception as e:
                print(f"Error in broad search: {e}")

        print("=" * 80)
        print("INSPECTION COMPLETE")
        print("=" * 80)
        print()

        if found_strings or array_addr:
            print("Results saved! Use this data to determine correct terrain mappings.")
        else:
            print("No terrain data found. The game might need to load a map first.")
            print("Try: Load a freeform map in ZT1, then run this script again.")
            print()
            print("Alternative: The game might store terrain data differently.")
            print("Please make sure:")
            print("  1. A map is fully loaded and visible on screen")
            print("  2. You've moved the camera around the map")
            print("  3. The game has been running for at least 30 seconds")

    except Exception as e:
        print(f"ERROR: {e}")
        print()
        print("Make sure you have administrator privileges.")
        sys.exit(1)

if __name__ == '__main__':
    main()
