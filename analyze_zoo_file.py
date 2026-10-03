import sys
import struct
from collections import Counter
import os

def analyze_zoo_file(zoo_path):
    print(f"Analyzing {zoo_path}...")
    try:
        with open(zoo_path, 'rb') as f:
            data = f.read()
            
            if len(data) < 100:
                print("Error: File too small.")
                return

            # Header Parsing
            magic, version = struct.unpack('<II', data[0:8])
            w, h = struct.unpack('<II', data[12:20])
            base_id, map_type = struct.unpack('<II', data[32:40])
            
            print(f"Header Info:")
            print(f"  Dimensions: {w}x{h} ({w*h} tiles)")
            print(f"  Base Terrain ID: {base_id}")
            print(f"  Map Type: {map_type}")
            
            # Tile Data (Starts at 100)
            # Stride 10
            header_size = 100
            tile_stride = 10
            expected_size = header_size + (w * h * tile_stride)
            
            if len(data) < expected_size:
                print(f"Error: Data size {len(data)} < Expected {expected_size}")
                # Analyze available data anyway
                # return
            
            terrain_counts = Counter()
            
            # Scan tiles
            limit = min(w*h, (len(data) - header_size) // tile_stride)
            
            # First pass for counts
            for i in range(limit):
                offset = header_size + (i * tile_stride)
                # Byte 0 is TerrainID
                tid = data[offset]
                terrain_counts[tid] += 1
            
            # Second pass for flags logic (implied in original purpose)
            # Actually I can just do it in one pass if I init counters earlier?
            # Let's fix the structure.
            
            print("\nTerrain ID Distribution:")
            total_tiles = limit
            for tid, count in terrain_counts.most_common(10):
                pct = (count / total_tiles) * 100
                print(f"  ID {tid}: {count} tiles ({pct:.1f}%)")
            
            # Count distinct flags
            flag_counts = Counter()
            for i in range(limit):
                offset = header_size + (i * tile_stride)
                flag = data[offset + 2]
                flag_counts[flag] += 1

            
            print("\nFlag Analysis (Sample of first 100 tiles):")
            for i in range(min(100, limit)):
                 offset = header_size + (i * tile_stride)
                 tid = data[offset]
                 flags = data[offset+2]
                 if flags != 0:
                     print(f"  Tile {i}: ID={tid} Flags=0x{flags:02X}")
                     break # Just find one to see if they exist
            
            # Count distinct flags
            flag_counts = Counter()
            for i in range(limit):
                flag = data[header_size + i*tile_stride + 2]
                flag_counts[flag] += 1
            
            print("\nFlag Distribution:")
            for f, c in flag_counts.most_common(10):
                 print(f"  Flag 0x{f:02X}: {c} ({c/total_tiles*100:.1f}%)")

    except FileNotFoundError:
        print(f"File {zoo_path} not found.")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        analyze_zoo_file(sys.argv[1])
    else:
        print("Usage: python analyze_zoo_file.py <zoo_path>")
