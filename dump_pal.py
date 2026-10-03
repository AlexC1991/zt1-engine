import zipfile
import sys
import struct

def dump_pal(ztd_path, pal_filename):
    print(f"Dumping {pal_filename} from {ztd_path}...")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            with z.open(pal_filename) as f:
                data = f.read()
                
                if len(data) < 4:
                     print("Too small")
                     return
                
                count = struct.unpack('<I', data[0:4])[0]
                print(f"Color Count: {count}")
                
                # Colors start at 4
                # Each is 4 bytes (uint32)
                # But what is the format?
                # User Log said 0xFF3D6E9A for Brown.
                # Memory: 9A 6E 3D FF. (R G B A)
                
                # SDL_ReadLE32 reads 4 bytes.
                
                print("First 5 Colors (Hex):")
                for i in range(min(count, 5)):
                    offset = 4 + i*4
                    r, g, b, a = struct.unpack('BBBB', data[offset:offset+4])
                    # Assuming RGBA memory order
                    print(f"  #{i}: [{r:02X} {g:02X} {b:02X} {a:02X}] -> Hex #{r:02X}{g:02X}{b:02X}")

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        dump_pal(sys.argv[1], sys.argv[2])
    else:
         print("Usage: python dump_pal.py <ztd> <file>")
