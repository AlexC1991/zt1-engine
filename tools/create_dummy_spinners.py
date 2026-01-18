import os
import struct

PROJECT_ROOT = "c:\\Users\\batty\\OneDrive\\Desktop\\Lua\\zt1-engine"
SPINUP_DIR = os.path.join(PROJECT_ROOT, "build", "Release", "ui", "sharedui", "spinup")
SPINDWN_DIR = os.path.join(PROJECT_ROOT, "build", "Release", "ui", "sharedui", "spindwn")

def write_red_bmp(path, width, height):
    # Palette: 0=Black, 1=Red, ...
    palette_bytes = bytearray(1024)
    # Entry 1 = Red (B=0, G=0, R=255)
    palette_bytes[4] = 0
    palette_bytes[5] = 0
    palette_bytes[6] = 255
    palette_bytes[7] = 0
    
    # Pixel data (all 1)
    row_padding = (4 - (width % 4)) % 4
    pixels = bytearray([1] * width) + b'\x00' * row_padding
    
    file_size = 14 + 40 + 1024 + len(pixels) * height
    offset = 14 + 40 + 1024
    
    header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, offset)
    info = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 8, 0, 0, 0, 0, 256, 0)
    
    with open(path, 'wb') as f:
        f.write(header)
        f.write(info)
        f.write(palette_bytes)
        for _ in range(height):
            f.write(pixels)

def main():
    for d in [SPINUP_DIR, SPINDWN_DIR]:
        if not os.path.exists(d):
            os.makedirs(d)
        for name in ["N.bmp", "H.bmp", "S.bmp", "G.bmp"]:
            write_red_bmp(os.path.join(d, name), 13, 18)
            print(f"Created dummy {name} in {d}")

if __name__ == "__main__":
    main()
