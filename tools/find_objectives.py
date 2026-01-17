import zipfile
import struct
import os

def write_bmp(filename, width, height, pixels, palette):
    # BMPs are stored bottom-to-top
    flipped_pixels = bytearray()
    padding = (4 - (width % 4)) % 4
    for y in range(height - 1, -1, -1):
        start = y * width
        row = pixels[start : start + width]
        flipped_pixels.extend(row)
        flipped_pixels.extend(b'\x00' * padding)

    file_size = 54 + 1024 + len(flipped_pixels)
    offset = 54 + 1024
    
    bmp_header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, offset)
    dib_header = struct.pack('<IIIHHIIIIII', 40, width, height, 1, 8, 0, len(flipped_pixels), 2835, 2835, 256, 0)
    
    bmp_palette = bytearray()
    for r, g, b in palette:
        bmp_palette.extend([b, g, r, 0])
        
    with open(filename, 'wb') as f:
        f.write(bmp_header)
        f.write(dib_header)
        f.write(bmp_palette)
        f.write(flipped_pixels)

def extract_zt1_image(z, raw_path, pal_path, output_name):
    if raw_path not in z.namelist():
        print(f"[!] Missing: {raw_path}")
        return

    # 1. Load Palette
    palette = [(i, i, i) for i in range(256)]
    if pal_path in z.namelist():
        pal_data = z.read(pal_path)
        palette = []
        for i in range(0, min(len(pal_data), 1024), 4):
            palette.append((pal_data[i], pal_data[i+1], pal_data[i+2]))
        while len(palette) < 256: palette.append((0,0,0))

    # 2. Load Raw Data and Detect Header
    data = z.read(raw_path)
    
    # ZT1 logic: If it starts with 'FAZT' (0x5A544146), skip 4 bytes
    offset = 0
    if data[0:4] == b'FAZT':
        offset = 4
        print(f"[*] Detected 'FAZT' signature in {raw_path}, skipping 4 bytes.")

    # Read Width and Height (u16)
    width = struct.unpack('<H', data[offset:offset+2])[0]
    height = struct.unpack('<H', data[offset+2:offset+4])[0]
    pixels = data[offset+4:]

    print(f"[OK] {output_name}: {width}x{height}")
    write_bmp(output_name, width, height, pixels, palette)

def main():
    ztd_path = os.path.join("build", "Release", "ui.ztd")
    if not os.path.exists(ztd_path):
        print(f"Error: {ztd_path} not found.")
        return

    with zipfile.ZipFile(ztd_path, 'r') as z:
        # Comparison 1: A working scenario map (Tutorial 1)
        # Assuming path matches your scenario.cfg stem logic
        extract_zt1_image(z, "ui/tutorial/tut1/N", "ui/tutorial/tut1/tut1.pal", "compare_map.bmp")

        # Comparison 2: The Locked Crate
        extract_zt1_image(z, "ui/scenario/lock/N", "ui/scenario/lock/lock.pal", "compare_lock.bmp")

if __name__ == "__main__":
    main()