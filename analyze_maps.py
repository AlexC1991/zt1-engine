import struct
import collections
import os
import sys

def analyze_map(path):
    print(f"--- Analyzing {path} ---")
    if not os.path.exists(path):
        print("File not found.")
        return

    with open(path, 'rb') as f:
        data = f.read()

    # 1. Dump Header ints
    print("Header uint32s:")
    for i in range(0, 40, 4):
        val = struct.unpack('<I', data[i:i+4])[0]
        print(f"  0x{i:02X}: {val}")

    # 2. Search for valid map data (heuristic)
    start_offset = 100
    stride = 10
    
    print(f"Terrain Histogram (assuming stride 10 from offset {start_offset}):")
    counts = collections.Counter()
    for i in range(start_offset, len(data), stride):
        if i+1 < len(data):
            t_id = data[i]
            elev = data[i+1]
            if t_id < 20 and elev < 50:
                 counts[t_id] += 1
    
    for t_id, count in counts.most_common(5):
        print(f"  ID {t_id}: {count}")

files = [
    r"build/Release/maps/deathmtn.zoo"
]

for p in files:
    analyze_map(p)
