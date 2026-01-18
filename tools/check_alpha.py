import zipfile
import struct

def check():
    # 1. Read Palette
    with zipfile.ZipFile('build/Release/ui.ztd') as zf:
        pal_data = zf.read('ui/sharedui/spinup/spinup.pal')
    
    # 2. Read BMP Indices
    with open('build/Release/ui/sharedui/spinup/N.bmp', 'rb') as f:
        bmp_data = f.read()
    
    offset = struct.unpack_from('<I', bmp_data, 10)[0]
    pixels = list(bmp_data[offset:])
    used_indices = set([b for b in pixels if b != 0])
    
    # 3. Check Alpha
    # Skip 4 bytes count
    colors_data = pal_data[4:]
    
    print(f"Checking {len(used_indices)} indices...")
    
    transparent_indices = []
    
    for idx in used_indices:
        # PalletManager reads 4 bytes per color
        start = idx * 4
        if start+4 > len(colors_data):
            print(f"Index {idx} out of bounds!")
            continue
            
        color_bytes = colors_data[start:start+4]
        # Assume Little Endian uint32 read: Byte 0, 1, 2, 3.
        # If file is RGBA. A is byte 3.
        alpha = color_bytes[3]
        
        if alpha == 0:
            transparent_indices.append(idx)
            
    print(f"Found {len(transparent_indices)} transparent indices out of {len(used_indices)}.")
    if transparent_indices:
        print(f"Example transparent indices: {transparent_indices[:10]}")
        print(f"Color Data for {transparent_indices[0]}: {colors_data[transparent_indices[0]*4 : transparent_indices[0]*4+4].hex(' ')}")

if __name__ == '__main__':
    check()
