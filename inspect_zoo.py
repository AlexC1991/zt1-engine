import zipfile
import zlib
import sys
import os

def hex_dump(data, length=64):
    return " ".join(f"{b:02x}" for b in data[:length])

def inspect_zoo(ztd_path, file_path_in_zip):
    print(f"Inspecting {file_path_in_zip} (Source: {ztd_path})")
    try:
        raw_data = None
        if ztd_path.endswith('.zoo'):
             # Treat ztd_path as the file itself
             if os.path.exists(ztd_path):
                with open(ztd_path, 'rb') as f:
                    raw_data = f.read()
             else:
                 print(f"File not found: {ztd_path}")
                 return
        else:
            with zipfile.ZipFile(ztd_path, 'r') as z:
                with z.open(file_path_in_zip) as f:
                    raw_data = f.read()

        header = raw_data[:4]
        print(f"Header: {header}")
        
        print(f"First 128 bytes raw:")
        print(hex_dump(raw_data, 128))

        if header == b'TZFB':
            print("Found TZFB signature.")
            
            # Try Raw Deflate (wbits=-15)
            try:
                compressed_data = raw_data[4:]
                decompressed = zlib.decompress(compressed_data, -15)
                print("\n[SUCCESS] Raw Deflate successful at offset 4!")
                print(hex_dump(decompressed))
                return
            except Exception as e:
                print(f"Raw Deflate failed at offset 4: {e}")

            # Try skipping 4 bytes size then Raw Deflate
            try:
                 # The 0x21 (33) might be a version number, not size?
                compressed_data = raw_data[8:]
                decompressed = zlib.decompress(compressed_data, -15)
                print(f"[SUCCESS] Raw Deflate successful at offset 8!")
                print(hex_dump(decompressed))
                return
            except Exception as e:
                print(f"Raw Deflate failed at offset 8: {e}")

        else:
            print(f"Unknown header: {hex_dump(raw_data, 16)}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    inspect_zoo("maps/large.zoo", "large.zoo")
