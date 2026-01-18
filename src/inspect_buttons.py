import zipfile
import struct
import os

def get_zt1_dimensions(data):
    """Reads width/height from ZT1 raw image headers"""
    if len(data) < 4: return None
    # Check for FAZT signature
    offset = 0
    if data[0:4] == b'FAZT':
        offset = 4
    
    try:
        # Standard ZT1 header is 2 bytes width, 2 bytes height
        w = struct.unpack('<H', data[offset:offset+2])[0]
        h = struct.unpack('<H', data[offset+2:offset+4])[0]
        return w, h
    except:
        return None

def scan_assets():
    ztd_path = os.path.join("build", "Release", "ui.ztd")
    loose_path = os.path.join("build", "Release")

    print(f"--- SCANNING FOR BUTTON ASSETS ---")

    # 1. Define what we are hunting for
    targets = [
        "back_n", "back_h", "ui/share/back/n", "ui/common/back/n",
        "play_n", "play_h", "play map_n", "ui/share/play/n"
    ]

    found_in_ztd = {}
    
    # 2. Check ZTD Archive
    if os.path.exists(ztd_path):
        try:
            with zipfile.ZipFile(ztd_path, 'r') as z:
                all_files = z.namelist()
                for f in all_files:
                    lower = f.lower()
                    # Check against our target list (partial matches)
                    for t in targets:
                        if t in lower and (lower.endswith("/n") or lower.endswith(".png") or lower.endswith(".bmp")):
                            # Read dimensions
                            data = z.read(f)
                            dims = get_zt1_dimensions(data)
                            if not dims and len(data) > 20: 
                                # Try PNG/BMP header basic check
                                if data[1:4] == b'PNG':
                                    w = struct.unpack('>I', data[16:20])[0]
                                    h = struct.unpack('>I', data[20:24])[0]
                                    dims = (w, h)
                            
                            found_in_ztd[t] = f"{f} \t[{dims[0]}x{dims[1]}]" if dims else f"{f} \t[Unknown Size]"
        except Exception as e:
            print(f"Error reading ZTD: {e}")

    # 3. Report Results
    print("\n[ BACK BUTTON ASSETS ]")
    found_back = False
    for t in ["back_n", "ui/share/back/n", "ui/common/back/n"]:
        if t in found_in_ztd:
            print(f"  FOUND: {found_in_ztd[t]}")
            found_back = True
    if not found_back: print("  [!] WARNING: No assets found for 'Back'")

    print("\n[ PLAY BUTTON ASSETS ]")
    found_play = False
    for t in ["play_n", "play map_n", "ui/share/play/n"]:
        if t in found_in_ztd:
            print(f"  FOUND: {found_in_ztd[t]}")
            found_play = True
    
    if not found_play: 
        print("  [!] CRITICAL: No assets found for 'Play'. This causes the dark box.")
    else:
        print("  [INFO] 'Play' assets found. Compare dimensions above.")

if __name__ == "__main__":
    scan_assets()