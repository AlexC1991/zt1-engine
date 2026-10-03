#!/usr/bin/env python3
"""
Simple ZT1 Inspector - Deep scan version
"""
import sys
import struct

try:
    import psutil
    import pymem
except ImportError:
    print("Run: pip install psutil pymem")
    sys.exit(1)

# Find zoo.exe
pid = None
for p in psutil.process_iter(['pid', 'name']):
    try:
        if p.info['name'] and p.info['name'].lower() == 'zoo.exe':
            pid = p.info['pid']
            break
    except:
        pass

if not pid:
    print("ERROR: zoo.exe not running! Start the game first.")
    sys.exit(1)

print(f"Found zoo.exe PID: {pid}")

# Attach
try:
    pm = pymem.Pymem(pid)
    print("Attached OK\n")
except Exception as e:
    print(f"Attach failed: {e}")
    sys.exit(1)

# List all modules
print("=" * 70)
print("LOADED MODULES")
print("=" * 70)
modules = []
for m in pm.list_modules():
    modules.append((m.name, m.lpBaseOfDll, m.SizeOfImage))
    print(f"  0x{m.lpBaseOfDll:08X}  {m.SizeOfImage//1024:6d} KB  {m.name}")

# Search patterns
searches = [
    # Terrain
    (b'terrain', "terrain paths"),
    (b'tiletex', "tile texture config"),
    (b'tilevar', "tile variant config"),
    (b'ttGrass', "terrain type Grass"),
    (b'ttSand', "terrain type Sand"),
    (b'ttWater', "terrain type Water"),
    # Sprites
    (b'.ani', "animation files"),
    (b'.pal', "palette files"),
    (b'.tga', "texture files"),
    (b'.ztd', "archive files"),
    # Classes
    (b'ZTWorldMgr', "world manager"),
    (b'ZTAdvTerrainMgr', "adv terrain manager"),
    (b'ZTSimpleTerrainMgr', "simple terrain manager"),
    (b'BFGame', "Blue Fang game class"),
    # Map
    (b'.zoo', "map files"),
    (b'.scn', "scenario files"),
    (b'freeform', "freeform maps"),
    # Entity types
    (b'animal', "animals"),
    (b'guest', "guests"),
    (b'keeper', "zookeepers"),
    (b'exhibit', "exhibits"),
    # File paths
    (b'animals/', "animal path"),
    (b'objects/', "object path"),
    (b'scenery/', "scenery path"),
    (b'ui/', "UI path"),
]

print("\n" + "=" * 70)
print("MEMORY SEARCH RESULTS")
print("=" * 70)

for pattern, desc in searches:
    found = []
    for name, base, size in modules:
        if size > 10_000_000:
            continue
        try:
            data = pm.read_bytes(base, size)
            pos = 0
            while True:
                pos = data.find(pattern, pos)
                if pos == -1:
                    break
                # Get context
                start = max(0, pos - 8)
                end = min(len(data), pos + len(pattern) + 50)
                ctx = data[start:end]
                text = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ctx)
                found.append((name, base + pos, text))
                pos += 1
                if len(found) >= 10:
                    break
        except:
            pass
        if len(found) >= 10:
            break

    if found:
        print(f"\n[{desc}] '{pattern.decode()}' - {len(found)} found:")
        for name, addr, text in found[:5]:
            print(f"  {name:20s} 0x{addr:08X}: {text[:60]}")
        if len(found) > 5:
            print(f"  ... +{len(found)-5} more")

# Look for numeric terrain IDs (0-16 in sequence)
print("\n" + "=" * 70)
print("SEARCHING FOR TERRAIN ID ARRAYS")
print("=" * 70)

for name, base, size in modules:
    if 'zoo' not in name.lower():
        continue
    try:
        data = pm.read_bytes(base, size)

        # Look for "type=0" through "type=15"
        for i in range(16):
            pattern = f"type={i}".encode()
            pos = data.find(pattern)
            if pos != -1:
                start = max(0, pos - 20)
                end = min(len(data), pos + 60)
                ctx = data[start:end]
                text = ''.join(chr(b) if 32 <= b < 127 else '.' for b in ctx)
                print(f"  Found 'type={i}' at 0x{base+pos:08X}: {text}")
    except:
        pass

# Search for specific file paths being loaded
print("\n" + "=" * 70)
print("INTERESTING FILE PATHS IN MEMORY")
print("=" * 70)

interesting = set()
for name, base, size in modules:
    if size > 10_000_000:
        continue
    try:
        data = pm.read_bytes(base, size)

        # Find strings with path separators
        i = 0
        while i < len(data) - 10:
            # Look for printable string
            if 32 <= data[i] < 127:
                end = i
                while end < len(data) and 32 <= data[end] < 127:
                    end += 1
                s = data[i:end].decode('ascii', errors='ignore')

                # Filter for interesting paths
                if len(s) > 8 and ('/' in s or '\\' in s):
                    sl = s.lower()
                    if any(x in sl for x in ['.ani', '.pal', '.tga', '.zoo', '.ztd', 'terrain', 'animal', 'object']):
                        interesting.add(s)

                i = end
            else:
                i += 1
    except:
        pass

for path in sorted(interesting)[:50]:
    print(f"  {path}")

if len(interesting) > 50:
    print(f"  ... +{len(interesting)-50} more paths")

print("\n" + "=" * 70)
print("DONE")
print("=" * 70)
