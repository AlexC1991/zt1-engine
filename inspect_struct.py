import sys
import struct

def main():
    if len(sys.argv) < 2:
        print("Usage: python inspect_struct.py <file.zoo>")
        return

    filepath = sys.argv[1]
    with open(filepath, 'rb') as f:
        content = f.read()

    print(f"File: {filepath}")
    print(f"Size: {len(content)} bytes")
    
    # Read Header dimensions
    if content[:4] == b'TZFB':
        ver = struct.unpack('<I', content[4:8])[0]
        # Heuristic from ZooReader
        if ver == 33: # scn01
             w, h = struct.unpack('<II', content[8:16])
        else:
             # Try offset 0xC ?
             w, h = struct.unpack('<II', content[12:20])
        
        print(f"Header: TZFB v{ver}")
        print(f"Dimensions: {w}x{h} ({w*h} tiles)")
        
        expected_size_2 = w * h * 2
        expected_size_4 = w * h * 4
        
        print(f"Expected Grid Size (2 bytes/tile): {expected_size_2}")
        print(f"Expected Grid Size (4 bytes/tile): {expected_size_4}")
        
    # Find runs of zeros
    print("\n--- Zero Run Analysis ---")
    in_run = False
    run_start = 0
    
    # Only check runs > 1000 bytes
    for i, byte in enumerate(content):
        if byte == 0:
            if not in_run:
                in_run = True
                run_start = i
        else:
            if in_run:
                in_run = False
                run_len = i - run_start
                if run_len > 1000:
                    print(f"Zero Run: Start {run_start} (0x{run_start:X}), Len {run_len}, End {i} (0x{i:X})")
                    if abs(run_len - expected_size_4) < 100:
                        print("   *** MATCHES 4-BYTE GRID SIZE ***")
                    if abs(run_len - expected_size_2) < 100:
                        print("   *** MATCHES 2-BYTE GRID SIZE ***")
            
    if in_run: # Ended with zeros
         run_len = len(content) - run_start
         if run_len > 1000:
            print(f"Zero Run: Start {run_start} (0x{run_start:X}), Len {run_len}, End {len(content)} (EOF)")

    # Inspect the area BEFORE the run (Header data?)
    print("\n--- Header Hex Dump (First 128 bytes) ---")
    print(content[:128].hex())

if __name__ == "__main__":
    main()
