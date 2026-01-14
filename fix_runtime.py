import os
import shutil

# --- CONFIG ---
ROOT_DIR = os.path.dirname(os.path.abspath(__file__))
RELEASE_DIR = os.path.join(ROOT_DIR, "build", "Release")
INI_FILE = os.path.join(RELEASE_DIR, "zoo.ini")

def fix_ini_file():
    print(f"[1/2] Rebuilding {INI_FILE}...")
    
    # This config combines the RESOURCE PATHS (to fix the crash)
    # with the VIDEO SETTINGS (to fix the black screen).
    
    new_ini_content = """[Paths]
path=.
ai=ai
anim=anim
maps=maps
scenarios=scenarios
saves=saves
zoo=zoo

[lib]
res=res0.dll
lang=lang0.dll

[user]
fullscreen=0
screenwidth=1280
screenheight=720
colourdepth=32
msaa=0
"""
    
    try:
        with open(INI_FILE, "w", encoding="utf-8") as f:
            f.write(new_ini_content)
        print("   -> Success: zoo.ini restored with correct paths AND video settings.")
    except Exception as e:
        print(f"   -> Error writing ini: {e}")

def create_fallback_dlls():
    print(f"\n[2/2] Creating Fallback DLLs...")
    # The engine might default to 'res.dll' if config fails. 
    # You have 'res0.dll'. Let's make a copy so both exist.
    
    copys = [
        ("res0.dll", "res.dll"),
        ("lang0.dll", "lang.dll")
    ]
    
    for src, dst in copys:
        src_path = os.path.join(RELEASE_DIR, src)
        dst_path = os.path.join(RELEASE_DIR, dst)
        
        if os.path.exists(src_path):
            if not os.path.exists(dst_path):
                try:
                    shutil.copy2(src_path, dst_path)
                    print(f"   -> Created safety copy: {dst} (from {src})")
                except Exception as e:
                    print(f"   -> Failed to copy {src}: {e}")
            else:
                print(f"   -> {dst} already exists. Good.")
        else:
            print(f"   -> WARNING: Source file {src} is missing! Did import_assets.py run?")

if __name__ == "__main__":
    if os.path.exists(RELEASE_DIR):
        fix_ini_file()
        create_fallback_dlls()
        print("\nFix Complete. Try launching 'zt1-engine.exe' now.")
    else:
        print(f"[ERROR] Release folder not found: {RELEASE_DIR}")
    
    input("\nPress Enter to exit...")