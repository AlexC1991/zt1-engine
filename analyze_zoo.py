import zipfile
import sys
import struct
from collections import Counter

def analyze_zoo(ztd_path, zoo_filename):
    print(f"Analyzing {zoo_filename} in {ztd_path}...")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            with z.open(zoo_filename) as f:
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
                    return
                
                terrain_counts = Counter()
                
                # Scan tiles
                for i in range(w * h):
                    offset = header_size + (i * tile_stride)
                    # Byte 0 is TerrainID
                    tid = data[offset]
                    terrain_counts[tid] += 1
                
                print("\nTerrain ID Distribution:")
                total_tiles = w * h
                for tid, count in terrain_counts.most_common(10):
                    pct = (count / total_tiles) * 100
                    print(f"  ID {tid}: {count} tiles ({pct:.1f}%)")

    except KeyError:
        print(f"File {zoo_filename} not found in {ztd_path}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        analyze_zoo(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python analyze_zoo.py <ztd_path> <zoo_filename>")
