import struct
import os
import sys

def check_preview_file(filename):
    try:
        with open(filename, 'rb') as f:
            # Check FATZ header
            header = f.read(4)
            if header != b'FATZ':
                print(f"{filename}: Not a FATZ file")
                return
            
            # Skip 12 bytes
            f.seek(12, 1)
            
            # Read palette string (null-terminated)
            palette = b''
            while True:
                byte = f.read(1)
                if byte == b'\x00' or not byte:
                    break
                palette += byte
            
            # Check for 0x01000000
            marker = struct.unpack('<I', f.read(4))[0]
            
            # Read RLE header
            rle_header = f.read(14)
            rle_size, width, height, mode = struct.unpack('<IIHH', rle_header)
            
            # Get file size
            f.seek(0, 2)
            file_size = f.tell()
            
            # Calculate data position
            data_start = 0x10 + len(palette) + 1 + 4 + 14  # header + palette + null + marker + rle_header
            data_size = file_size - data_start
            
            print(f"\n{filename}:")
            print(f"  Header: {header}")
            print(f"  Palette: {palette.decode('ascii', errors='ignore')}")
            print(f"  Marker: 0x{marker:08x} ({marker})")
            print(f"  RLE Header: {rle_size} bytes, {width}x{height}, mode={mode}")
            print(f"  File size: {file_size} bytes")
            print(f"  Data starts at: 0x{data_start:x}")
            print(f"  Expected RLE data: {rle_size} bytes")
            print(f"  Actual data size: {data_size} bytes")
            print(f"  Match: {'✓' if data_size == rle_size else '✗'} ({data_size - rle_size:+d} bytes)")
            
            # Check if dimensions are valid
            valid_dims = (width == 367 and height == 276) or (width == 152 and height == 275)
            print(f"  Valid dimensions: {'✓' if valid_dims else '✗'}")
            
            return width, height, mode, rle_size
            
    except Exception as e:
        print(f"{filename}: Error - {e}")
        return None

if __name__ == "__main__":
    if len(sys.argv) > 1:
        # Check specific files
        for filename in sys.argv[1:]:
            check_preview_file(filename)
    else:
        # Check all .N files in current directory
        for filename in os.listdir('.'):
            if filename.lower().endswith('.n'):
                check_preview_file(filename)