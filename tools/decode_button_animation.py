#!/usr/bin/env python3
"""
Zoo Tycoon 1 Animation Decoder
Extracts button background graphics from ZTD animation files and saves as PNG.
"""

import zipfile
import struct
import os
import glob
import zipfile

def write_bmp_8bit(path, width, height, indices, palette):
    """Write raw indices to an 8-bit BMP file with the provided palette."""
    # 8-bit BMP header
    # Palette = 256 * 4 bytes (B G R X)
    
    # BMP Palette expects BGRX
    # Our palette is (R, G, B, A)
    bmp_palette = bytearray()
    for r, g, b, a in palette:
        bmp_palette.extend([b, g, r, 0])
        
    pad_bytes = (4 - width % 4) % 4
    image_size = (width + pad_bytes) * height
    
    file_size = 14 + 40 + 1024 + image_size
    offset = 14 + 40 + 1024
    
    file_header = struct.pack('<2sIHHI', b'BM', file_size, 0, 0, offset)
    
    # 8-bit header (biBitCount=8, biClrUsed=256)
    info_header = struct.pack('<IiiHHIIIIII', 40, width, height, 1, 8, 0, 0, 0, 0, 256, 0)
    
    with open(path, 'wb') as f:
        f.write(file_header)
        f.write(info_header)
        f.write(bmp_palette)
        
        # Bottom-Up
        for y in range(height - 1, -1, -1):
            row_start = y * width
            row_data = indices[row_start : row_start + width]
            f.write(bytearray(row_data))
            f.write(b'\x00' * pad_bytes)

def read_palette(ztd_path, palette_path):
    """Read a palette file from ZTD and return list of (R,G,B,A) tuples."""
    with zipfile.ZipFile(ztd_path) as z:
        try:
            data = z.read(palette_path)
        except KeyError:
            # Try without leading slash or with variations
            for name in z.namelist():
                if palette_path.lower() in name.lower():
                    data = z.read(name)
                    break
            else:
                print(f"Could not find palette: {palette_path}")
                return [(i, i, i, 255) for i in range(256)]  # Grayscale fallback
    
    colors = []
    # ZT1 palette format: 256 * 3 bytes (RGB)
    for i in range(256):
        if i * 3 + 2 < len(data):
            r = data[i * 3]
            g = data[i * 3 + 1]
            b = data[i * 3 + 2]
            # Color 0 is typically transparent
            a = 0 if i == 0 else 255
            colors.append((r, g, b, a))
        else:
            colors.append((0, 0, 0, 0))
    
    return colors

def decode_animation_frame_indices(data):
    """Decode ZT1 frame to raw indices."""
    pos = 0
    
    # Headers
    timing = struct.unpack_from('<I', data, pos)[0]; pos+=4
    str_len = struct.unpack_from('<I', data, pos)[0]; pos+=4
    pos += str_len # skip path
    pos += 4 # skip unknown
    
    frame_size = struct.unpack_from('<I', data, pos)[0]; pos+=4
    height = struct.unpack_from('<H', data, pos)[0]; pos+=2
    width = struct.unpack_from('<H', data, pos)[0]; pos+=2
    offset_x = struct.unpack_from('<h', data, pos)[0]; pos+=2
    offset_y = struct.unpack_from('<h', data, pos)[0]; pos+=2
    pos += 2 # mystery
    
    print(f"Frame: {width}x{height}")
    
    # Default index 0 (Transparent)
    indices = [0] * (width * height)
    
    pixel_end = pos + frame_size
    
    for y in range(height):
        if pos >= pixel_end: break
        instruction_count = data[pos]; pos += 1
        
        x = 0
        for _ in range(instruction_count):
            if pos + 1 >= len(data): break
            skip = data[pos]
            run = data[pos + 1]
            pos += 2
            x += skip
            
            for _ in range(run):
                if pos >= len(data) or x >= width: break
                color_idx = data[pos]; pos += 1
                idx = y * width + x
                indices[idx] = color_idx
                x += 1
                
    return width, height, indices

def main():
    # Scan for ZTD containing global palette
    global_pal_ztd = None
    global_pal_path = "ui/palette/color256.pal"
    
    print("Scanning for global palette...")
    for ztd in glob.glob("*.ztd"):
        try:
            with zipfile.ZipFile(ztd) as z:
                for name in z.namelist():
                    if global_pal_path.lower() in name.lower():
                        global_pal_ztd = ztd
                        print(f"Found global palette in: {ztd} ({name})")
                        break
        except:
            pass
        if global_pal_ztd: break
            
    # Paths
    ztd_path = "ui.ztd"
    anim_file = "ui/gameopts/textbck/N" 
    
    # Check Index 8 in Global Palette if found
    if global_pal_ztd:
         print(f"Checking Global Palette Index 8 (Expected Green)...")
         gpal = read_palette(global_pal_ztd, global_pal_path)
         r, g, b, a = gpal[8]
         print(f"Index 8 in color256.pal: ({r},{g},{b})")
         if g > r and g > b:
             print("SUCCESS: Index 8 is GREEN!")
         else:
             print("FAILURE: Index 8 is NOT Green.")

    # Check Indices in Internal Palette
    print(f"\nChecking Internal Palette (textbck.pal) Indices:")
    
    # We need to construct the full palette from the file
    ztd_file = "ui.ztd"
    internal_pal_path = "ui/gameopts/textbck/textbck.pal"
    
    try:
        ipal = read_palette(ztd_file, internal_pal_path)
        
        idx_to_check = [8, 64, 13, 19, 36]
        for idx in idx_to_check:
            r, g, b, a = ipal[idx]
            print(f"Index {idx}: R={r}, G={g}, B={b}")
            
    except Exception as e:
        print(f"Failed to read internal palette for Inspection: {e}")

    print(f"Extracting Raw Indices from {anim_file}...")
    
    # We still need A palette for the BMP, even if we plan to swap it in C++.
    # We use the internal one so the BMP is valid.
    palette = [(i, i, i, 255) for i in range(256)] # Default Grayscale
    
    with zipfile.ZipFile(ztd_path) as z:
        data = z.read(anim_file)
        
        # Read Internal Palette just to have something
        try:
            str_len = struct.unpack_from('<I', data, 4)[0]
            pal_path = data[8:8+str_len].decode('ascii', errors='ignore').rstrip('\x00')
            print(f"Internal Palette: {pal_path}")
            palette = read_palette(ztd_path, pal_path)
        except:
            pass
            
    print("Decoding indices...")
    width, height, indices = decode_animation_frame_indices(data)
    
    write_bmp_8bit("button_background_N.bmp", width, height, indices, palette)
    print("Saved 8-bit button_background_N.bmp")
    
    # Other states
    states = ['S', 'H', 'G']
    for state in states:
         try:
             with zipfile.ZipFile(ztd_path) as z:
                 s_data = z.read(f"ui/gameopts/textbck/{state}")
             w, h, idxs = decode_animation_frame_indices(s_data)
             write_bmp_8bit(f"button_background_{state}.bmp", w, h, idxs, palette)
             print(f"Saved button_background_{state}.bmp")
         except:
             print(f"Failed {state}")

if __name__ == "__main__":
    main()
