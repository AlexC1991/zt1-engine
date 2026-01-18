import zipfile
import struct
import sys

def debug_asset():
    ztd_path = 'build/Release/ui.ztd'
    asset_path = 'ui/sharedui/spinup/N'
    
    try:
        zf = zipfile.ZipFile(ztd_path, 'r')
        data = zf.read(asset_path)
    except Exception as e:
        print(f"Failed to load: {e}")
        return

    print(f"Loaded {len(data)} bytes")
    print("Hex Dump First 64 bytes:")
    print(data[:64].hex(' '))
    
    pos = 0
    duration = struct.unpack_from('<I', data, pos)[0]; pos+=4
    path_len = struct.unpack_from('<I', data, pos)[0]; pos+=4
    pos += path_len # path
    pos += 4 # unknown
    data_size = struct.unpack_from('<I', data, pos)[0]; pos+=4
    
    width = struct.unpack_from('<H', data, pos)[0]; pos+=2
    height = struct.unpack_from('<H', data, pos)[0]; pos+=2
    
    off_x = struct.unpack_from('<h', data, pos)[0]; pos+=2
    off_y = struct.unpack_from('<h', data, pos)[0]; pos+=2
    
    pos += 4 # flags
    
    print(f"Header: {width}x{height} offset({off_x},{off_y}) data_size={data_size}")
    
    pixel_start = pos
    pixel_end = pos + data_size
    
    total_pixels = 0
    non_zero_pixels = 0
    
    for y in range(height):
        if pos >= pixel_end: break
        instr_count = data[pos]; pos += 1
        print(f"Row {y}: {instr_count} instructions")
        
        x = 0
        for i in range(instr_count):
            if pos+1 >= len(data): break
            skip = data[pos]
            run = data[pos+1]
            pos += 2
            
            x += skip
            
            print(f"  Instr {i}: skip={skip} run={run}")
            
            for r in range(run):
                if pos < len(data):
                    col = data[pos]
                    if col != 0: non_zero_pixels += 1
                    pos += 1
                    total_pixels += 1
            x += run

    print(f"Total Pixels Written: {total_pixels}")
    print(f"Non-Zero Pixels: {non_zero_pixels}")

if __name__ == '__main__':
    debug_asset()
