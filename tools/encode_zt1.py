import struct
import os
import glob

PROJECT_ROOT = "c:\\Users\\batty\\OneDrive\\Desktop\\Lua\\zt1-engine"
SPINUP_DIR = os.path.join(PROJECT_ROOT, "build", "Release", "ui", "sharedui", "spinup")
SPINDWN_DIR = os.path.join(PROJECT_ROOT, "build", "Release", "ui", "sharedui", "spindwn")

def read_bmp_pixels(bmp_path):
    with open(bmp_path, 'rb') as f:
        data = f.read()
    
    pixel_offset = struct.unpack_from('<I', data, 10)[0]
    width = struct.unpack_from('<i', data, 18)[0]
    height = struct.unpack_from('<i', data, 22)[0]
    bit_count = struct.unpack_from('<H', data, 28)[0] # Offset 28 (0x1C)
    
    if bit_count != 8:
        print(f"Error: {bmp_path} is {bit_count}-bit. Expected 8-bit.")
        return 0,0,[]
        
    # BMP Row Padding
    row_padded = (width + 3) & (~3)
    
    pixels = []
    for y in range(height):
        # Bottom-Up
        file_y = height - 1 - y
        row_start = pixel_offset + file_y * row_padded
        row_data = data[row_start : row_start + width]
        pixels.extend(row_data)
        
    return width, height, pixels

def encode_frame(width, height, pixels, pal_path):
    lines_data = bytearray()
    
    for y in range(height):
        row_pixels = pixels[y * width : (y + 1) * width]
        
        instructions = []
        current_x = 0
        
        while current_x < width:
            # Skip transparent (Index 0)
            skip = 0
            while current_x + skip < width and row_pixels[current_x + skip] == 0:
                skip += 1
            
            if current_x + skip >= width:
                break
                
            # Run opaque
            run = 0
            while current_x + skip + run < width and row_pixels[current_x + skip + run] != 0 and run < 255:
                run += 1
                
            if run > 0:
                color_data = row_pixels[current_x + skip : current_x + skip + run]
                instructions.append((skip, color_data))
                current_x += skip + run
            else:
                current_x += skip
        
        # Write Line
        lines_data.append(len(instructions))
        for skip, colors in instructions:
            lines_data.append(skip)
            lines_data.append(len(colors))
            lines_data.extend(colors)
            
    header = bytearray()
    header.extend(struct.pack('<I', 1000))
    
    pal_bytes = pal_path.encode('ascii') + b'\x00'
    header.extend(struct.pack('<I', len(pal_bytes)))
    header.extend(pal_bytes)
    
    header.extend(struct.pack('<I', 0))
    header.extend(struct.pack('<I', len(lines_data)))
    
    # Engine reads Height then Width
    header.extend(struct.pack('<H', height))
    header.extend(struct.pack('<H', width))
    
    header.extend(struct.pack('<h', 0)) # OffsetX
    header.extend(struct.pack('<h', 0)) # OffsetY
    
    header.extend(struct.pack('<H', 0)) # Mystery
    
    return header + lines_data

def process_dir(directory, folder_name):
    print(f"Encoding in {directory}...")
    
    bmp_files = glob.glob(os.path.join(directory, "*.bmp"))
    if not bmp_files:
        print("No BMP files found (did you extract them?)")
        return

    # Use first BMP to verify palette? We assume N.bmp has correct indices.
    
    w, h = 0, 0
    
    for bmp in bmp_files:
        name = os.path.splitext(os.path.basename(bmp))[0]
        # Skip if name is not N, H, S, G
        if name not in ['N', 'H', 'S', 'G']: continue
        
        w, h, p = read_bmp_pixels(bmp)
        if w == 0: continue
        
        pal_path = f"ui/sharedui/{folder_name}/{folder_name}.pal"
        
        encoded = encode_frame(w, h, p, pal_path)
        
        out_path = os.path.join(directory, name) # Overwrite or same name no ext?
        # ZT1 frames have no extension usually.
        # But wait, conflicting with directory "N"? No, N is file.
        # Wait, previous extraction created N.bmp.
        # I should output to `N` (no extension).
        
        with open(out_path, 'wb') as f:
            f.write(encoded)
        print(f"Encoded {name}")

    if w > 0:
        ani_path = os.path.join(directory, f"{folder_name}.ani")
        with open(ani_path, 'w') as f:
            f.write(f"[animation]\n")
            f.write(f"dir0 = ui\n")
            f.write(f"dir1 = sharedui\n")
            f.write(f"dir2 = {folder_name}\n")
            f.write(f"animation = N\n")
            # f.write(f"animation = H\n")
            # f.write(f"animation = S\n")
            # f.write(f"animation = G\n")
            f.write(f"x0 = 0\n")
            f.write(f"y0 = 0\n")
            f.write(f"x1 = {w}\n")
            f.write(f"y1 = {h}\n")
        print(f"Updated {folder_name}.ani")

def main():
    process_dir(SPINUP_DIR, "spinup")
    process_dir(SPINDWN_DIR, "spindwn")

if __name__ == "__main__":
    main()
