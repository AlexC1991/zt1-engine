import os
import struct
import zipfile
import io

PROJECT_ROOT = "c:\\Users\\batty\\OneDrive\\Desktop\\Lua\\zt1-engine"
BUILD_UI_DIR = os.path.join(PROJECT_ROOT, "build", "Release", "ui")
ZTD_PATH = os.path.join(PROJECT_ROOT, "build", "Release", "ui.ztd")

def ensure_dir(path):
    if not os.path.exists(path):
        os.makedirs(path)

def write_bmp(path, width, height, pixels, palette):
    """Write 8-bit BMP."""
    # Palette: List of (r,g,b,a)
    # BMP expects BGR0 in palette
    bmp_pal = bytearray()
    for r,g,b,a in palette:
        bmp_pal.extend([b, g, r, 0])
    
    # Pad rows to 4 bytes
    row_padding = (4 - (width % 4)) % 4
    file_size = 14 + 40 + 1024 + (width + row_padding) * height
    offset = 14 + 40 + 1024
    
    # Header
    # BM, Size, Res, Res, Offset
    header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, offset)
    
    # Info Header
    # Size, W, H, Planes, BitCount, Comp, SizeImage, XPels, YPels, ClrUsed, ClrImp
    info = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 8, 0, 0, 0, 0, 256, 0)
    
    with open(path, 'wb') as f:
        f.write(header)
        f.write(info)
        f.write(bmp_pal)
        
        # BMP is bottom-up
        for y in range(height - 1, -1, -1):
            row_start = y * width
            row = pixels[row_start : row_start + width]
            f.write(bytearray(row))
            f.write(b'\x00' * row_padding)

def read_palette(zf, path):
    try:
        data = zf.read(path)
        colors = []
        for i in range(256):
            if i*3+2 < len(data):
                colors.append((data[i*3], data[i*3+1], data[i*3+2], 255))
            else:
                colors.append((0,0,0,0))
        # Index 0 is transparent
        colors[0] = (colors[0][0], colors[0][1], colors[0][2], 0)
        return colors
    except KeyError:
        print(f"Palette not found: {path}")
        return [(i,i,i,255) for i in range(256)]

def decode_image(data, palette_lookup):
    # Header Parsing
    pos = 0
    duration = struct.unpack_from('<I', data, pos)[0]; pos+=4
    path_len = struct.unpack_from('<I', data, pos)[0]; pos+=4
    pal_path = data[pos : pos+path_len].replace(b'\x00', b'').decode('utf-8')
    pos += path_len
    
    pos += 4 # unknown
    data_size = struct.unpack_from('<I', data, pos)[0]; pos+=4
    
    # AniFile reads Height then Width
    height = struct.unpack_from('<H', data, pos)[0]; pos+=2
    width = struct.unpack_from('<H', data, pos)[0]; pos+=2
    
    off_x = struct.unpack_from('<h', data, pos)[0]; pos+=2
    off_y = struct.unpack_from('<h', data, pos)[0]; pos+=2
    
    pos += 2 # mystery bytes (frame.mystery in AniFile)
    
    print(f"Decoding {width}x{height} (Palette: {pal_path})")
    
    # Pixel Data
    pixels = [0] * (width * height) # Initialize valid size buffer
    pixel_end = pos + data_size
    
    for y in range(height):
        if pos >= pixel_end: break
        if pos >= len(data): break
        
        instruction_count = data[pos]; pos += 1
        
        x = 0
        for _ in range(instruction_count):
            if pos + 1 >= len(data): break
            skip = data[pos]
            run = data[pos + 1]
            pos += 2
            
            x += skip
            
            for _ in range(run):
                if pos >= len(data): break
                if x >= width: 
                    pos += 1 # Consume byte even if OOB
                    continue
                    
                color_idx = data[pos]; pos += 1
                idx = y * width + x
                pixels[idx] = color_idx
                x += 1
                
    return pixels, width, height, pal_path

def process_anim(zf, base_path, output_dir):
    ensure_dir(output_dir)
    
    # Process states
    states = ['N', 'H', 'S', 'G']
    palette = None
    
    # 1. Decode N to get palette path
    try:
        n_data = zf.read(f"{base_path}/N")
        pixels, w, h, pal_path = decode_image(n_data, None)
        
        # Load Palette
        palette = read_palette(zf, pal_path)
        
        # Write N
        write_bmp(os.path.join(output_dir, "N.bmp"), w, h, pixels, palette)
        print(f"Saved {output_dir}/N.bmp")
        
    except KeyError as e:
        print(f"Missing N frame for {base_path}: {e}")
        return

    # Process other states
    for state in states:
        if state == 'N': continue # Done
        try:
            data = zf.read(f"{base_path}/{state}")
            pixels, w, h, _ = decode_image(data, None)
            write_bmp(os.path.join(output_dir, f"{state}.bmp"), w, h, pixels, palette)
            print(f"Saved {output_dir}/{state}.bmp")
        except KeyError:
            print(f"Missing {state} frame (skipping)")
            
    # Extract .ani file
    ani_name = os.path.basename(base_path) + ".ani"
    try:
        ani_data = zf.read(f"{base_path}/{ani_name}")
        with open(os.path.join(output_dir, ani_name), 'wb') as f:
            f.write(ani_data)
        print(f"Extracted {ani_name}")
    except KeyError:
        print(f"Missing {ani_name}")

def main():
    try:
        with zipfile.ZipFile(ZTD_PATH, 'r') as zf:
            print("Processing spinup...")
            process_anim(zf, "ui/sharedui/spinup", os.path.join(BUILD_UI_DIR, "sharedui", "spinup"))
            
            print("\nProcessing spindwn...")
            process_anim(zf, "ui/sharedui/spindwn", os.path.join(BUILD_UI_DIR, "sharedui", "spindwn"))
            
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()
