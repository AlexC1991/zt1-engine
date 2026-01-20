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
                    str_len = int.from_bytes(header[4:8], byteorder='little')
                    print(f"\nTiming (int32): {timing}")
                    print(f"Str Len (int32): {str_len}")
                    
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    inspect_ani("terrain.ztd", "terrain/icgrass/N")
