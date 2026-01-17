import zipfile
import sys
import os

def hex_dump(data):
    print(f"\n--- HEX DUMP (First 64 bytes) ---")
    for i in range(0, min(len(data), 64), 16):
        chunk = data[i:i+16]
        hex_str = ' '.join(f'{b:02X}' for b in chunk)
        ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in chunk)
        print(f"{i:04X}  {hex_str:<48}  {ascii_str}")

def main():
    ztd_path = os.path.join("build", "release", "ui.ztd")
    
    if not os.path.exists(ztd_path):
        print(f"CRITICAL: {ztd_path} not found.")
        return

    print(f"--- OPENING: {ztd_path} ---")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            names = z.namelist()
            print(f"Total files: {len(names)}")
            
            print("\n--- FIRST 10 FILES (Checking slash format) ---")
            for n in names[:10]:
                print(f"  {n}")

            # Find ANY file ending in 'N' (case insensitive)
            target = None
            for n in names:
                # We look for files ending in /N, \N, or just N
                if n.lower().endswith("/n") or n.lower().endswith("\\n") or n.lower() == "n":
                    target = n
                    break
            
            if target:
                print(f"\n--- FOUND TARGET: {target} ---")
                data = z.read(target)
                print(f"File Size: {len(data)} bytes")
                hex_dump(data)
            else:
                print("\nCRITICAL: No 'N' file found in archive!")
                
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    main()