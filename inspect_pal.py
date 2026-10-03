import zipfile
import sys
import struct

def inspect_pal(ztd_path, file_path):
    print(f"Inspecting {file_path} in {ztd_path}")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            with z.open(file_path) as f:
                # PAL format:
                # 0-4: Color count (uint32)
                # 4-end: Colors (uint32: R G B A?) - Wait, usually ZT1 pals are just RGB or RGBA
                # Let's read header
                header = f.read(4)
                count = struct.unpack('<I', header)[0]
                print(f"Color Count: {count}")
                
                data = f.read()
                # Read first 16 colors
                for i in range(count):
                    offset = i * 4
                    if offset + 4 > len(data): break
                    c_data = data[offset:offset+4]
                    # ZT1 palettes are often just R, G, B, Reserved (or A)
                    # Let's try to interpret
                    r, g, b, a = struct.unpack('BBBB', c_data)
                    print(f"Color {i}: ({r}, {g}, {b}) [Hex: {r:02X}{g:02X}{b:02X}] A={a}")
                    
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        inspect_pal(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python inspect_pal.py <ztd_path> <file_path>")
