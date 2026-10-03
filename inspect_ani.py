import zipfile
import sys
import os

def hex_dump(data):
    return " ".join(f"{b:02x}" for b in data)

def inspect_ani(ztd_path, file_path):
    print(f"Inspecting {file_path} in {ztd_path}")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            with z.open(file_path) as f:
                header = f.read(256)
                print(f"First 256 bytes:")
                print(hex_dump(header))
                
                print("\nASCII interpretation:")
                print("".join(chr(b) if 32 <= b <= 126 else '.' for b in header))
                
                # Try interpreting standard header
                # 0-4: timing
                # 4-8: str_len
                if len(header) >= 8:
                    timing = int.from_bytes(header[0:4], byteorder='little')
                    timing = int.from_bytes(header[8:12], byteorder='little') >> 8
                    str_len = int.from_bytes(header[12:16], byteorder='little') >> 8
                    print(f"\nTiming (int32): {timing}")
                    print(f"Str Len (int32): {str_len}")
                    
                    # Skip header + string + 1 null byte + 4 unknown bytes
                    f.seek(0)
                    f.read(4) # FATZ
                    f.read(4) # Unknown
                    # Timing and Len were consumed by header read logic above? No, we are seeking.
                    
                    # We need to read raw bytes again because we are reprocessing
                    timing_raw = f.read(4)
                    len_raw = f.read(4)
                    
                    padding = f.read(1) # Pad
                    
                    str_len_val = int.from_bytes(len_raw, byteorder='little') >> 8
                    
                    if str_len_val > 0:
                        pal_path_bytes = f.read(str_len_val)
                        print(f"Palette Path: {pal_path_bytes.decode('utf-8', errors='ignore')}")
                    
                    f.read(4) # Skip Unknown

                    
                    # Read Frame Header
                    # Size (4)
                    frame_size_bytes = f.read(4)
                    if len(frame_size_bytes) == 4:
                        frame_size = int.from_bytes(frame_size_bytes, byteorder='little')
                        print(f"Frame 0 Size: {frame_size}")
                        
                        # Frame Data
                        # H(2), W(2), OffX(2), OffY(2), Magic(2)
                        h = int.from_bytes(f.read(2), byteorder='little')
                        w = int.from_bytes(f.read(2), byteorder='little')
                        print(f"Dimensions: {w}x{h}")
                        
                        f.read(4) # Offsets
                        f.read(2) # Magic
                        
                        # Pixel Lines
                        # For each line 0..h
                        indices = []
                        for y in range(h):
                            count = int.from_bytes(f.read(1), byteorder='little')
                            # For each instruction
                            for _ in range(count):
                                off = int.from_bytes(f.read(1), byteorder='little')
                                length = int.from_bytes(f.read(1), byteorder='little')
                                if length > 0:
                                    pixels = f.read(length)
                                    indices.extend(list(pixels))
                        
                        print(f"Sample Indices (First 20): {indices[:20]}")
                        print(f"Sample Indices (Last 20): {indices[-20:]}")
                        
                        # Histogram of indices
                        from collections import Counter
                        c = Counter(indices)
                        print(f"Top 5 Indices: {c.most_common(5)}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        inspect_ani(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python inspect_ani.py <ztd_path> <file_path_in_zip>")
