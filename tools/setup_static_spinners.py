
import os

BASE_DIR = r"c:\Users\batty\OneDrive\Desktop\Lua\zt1-engine\build\Release\ui\sharedui"

def setup_folder(folder_name, base_name):
    # Folders are nested: sharedui/spinup
    path = os.path.join(BASE_DIR, folder_name)
    if not os.path.exists(path):
        print(f"Path not found: {path} (Trying parent?)")
        # Check if they are loose in sharedui?
        # fix_spinners.py wrote to: build\Release\ui\sharedui\spinup
        # So path should be correct.
        return

    print(f"Processing {path}...")

    # Delete ANI
    ani_path = os.path.join(path, f"{base_name}.ani")
    if os.path.exists(ani_path):
        os.remove(ani_path)
        print(f"  [DELETE] {base_name}.ani")
    else:
        print(f"  [INFO] {base_name}.ani not found (already deleted?)")

    # Rename BMPs
    # Mapping: Original -> New
    # spinup: N.bmp -> spinup_N.bmp
    mapping = {
        "N.bmp": f"{base_name}_N.bmp",
        "H.bmp": f"{base_name}_H.bmp",
        "S.bmp": f"{base_name}_S.bmp", 
        "G.bmp": f"{base_name}_G.bmp",  
        # D (Down) is usually Same as Selected or different. I don't have D extracted?
        # fix_spinners extracted N, H, S, G.
    }
    
    for src, dst in mapping.items():
        src_path = os.path.join(path, src)
        dst_path = os.path.join(path, dst)
        
        if os.path.exists(src_path):
            if os.path.exists(dst_path):
                os.remove(dst_path) # Overwrite existing
                print(f"  [OVERWRITE] {dst}")
            os.rename(src_path, dst_path)
            print(f"  [RENAME] {src} -> {dst}")
        elif os.path.exists(dst_path):
             print(f"  [INFO] {dst} already exists.")
        else:
             print(f"  [MISSING] {src}")

if __name__ == "__main__":
    setup_folder("spinup", "spinup")
    setup_folder("spindwn", "spindwn")
