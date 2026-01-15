#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║          ZOO TYCOON 1 ENGINE - STEP 5: SETUP RUNTIME             ║
╚══════════════════════════════════════════════════════════════════╝
"""

import os
import sys
import shutil

# Navigate to project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")

# Colors
class C:
    RESET = '\033[0m'
    BOLD = '\033[1m'
    RED = '\033[91m'
    GREEN = '\033[92m'
    YELLOW = '\033[93m'
    CYAN = '\033[96m'
    DIM = '\033[2m'

def enable_ansi():
    if os.name == 'nt':
        os.system('')

def setup_fonts():
    """Copy fonts from root fonts folder to Release."""
    fonts_src = os.path.join(ROOT_DIR, "fonts")
    fonts_dst = os.path.join(REL_DIR, "fonts")
    
    required_fonts = ["Aileron-Black.otf", "Aileron-Bold.otf", "Aileron-Regular.otf"]
    
    # Create source folder if missing
    if not os.path.exists(fonts_src):
        os.makedirs(fonts_src, exist_ok=True)
    
    # Check which fonts exist
    fonts_present = [f for f in required_fonts if os.path.exists(os.path.join(fonts_src, f))]
    fonts_missing = [f for f in required_fonts if f not in fonts_present]
    
    if fonts_missing:
        print(f"  {C.YELLOW}⚠ Missing fonts in {fonts_src}:{C.RESET}")
        for f in fonts_missing:
            print(f"    {C.RED}✗{C.RESET} {f}")
        print(f"  {C.DIM}Download from: https://www.fontsquirrel.com/fonts/aileron{C.RESET}")
        return False
    
    # Copy fonts to Release
    os.makedirs(fonts_dst, exist_ok=True)
    for font in required_fonts:
        src = os.path.join(fonts_src, font)
        dst = os.path.join(fonts_dst, font)
        shutil.copy2(src, dst)
    
    print(f"  {C.GREEN}✓{C.RESET} Fonts installed")
    return True

def setup_zoo_ini():
    """Create the zoo.ini configuration file."""
    ini_content = """[resource]
path=.

[ui]
noMenuMusic=0
menuMusic=sounds/mainmenu.wav
progressRed=255
progressGreen=200
progressBlue=0
progressLeft=229
progressTop=417
progressRight=583
progressBottom=440

[Paths]
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
    
    ini_path = os.path.join(REL_DIR, "zoo.ini")
    with open(ini_path, "w", encoding="utf-8") as f:
        f.write(ini_content)
    
    print(f"  {C.GREEN}✓{C.RESET} zoo.ini created")
    return True

def setup_folders():
    """Create required game folders."""
    folders = [
        "dlupdate", "updates", "xpack1", "xpack2", 
        "dupdate", "zupdate", "zupdate1", "loc",
        "xpack1/loc", "xpack2/loc",
        "saves", "scenarios", "maps"
    ]
    
    for folder in folders:
        os.makedirs(os.path.join(REL_DIR, folder), exist_ok=True)
    
    print(f"  {C.GREEN}✓{C.RESET} Game folders created")
    return True

def setup_dlls():
    """Copy DLLs and create fallback copies."""
    dll_count = 0
    
    # Copy any DLLs from build subfolders
    for root, _, files in os.walk(BUILD_DIR):
        if "Release" in root:
            continue
        for f in files:
            if f.endswith(".dll"):
                src = os.path.join(root, f)
                dst = os.path.join(REL_DIR, f)
                if not os.path.exists(dst):
                    try:
                        shutil.copy2(src, dst)
                        dll_count += 1
                    except:
                        pass
    
    # Create fallback DLL copies
    fallbacks = [
        ("res0.dll", "res.dll"),
        ("lang0.dll", "lang.dll")
    ]
    
    for src_name, dst_name in fallbacks:
        src = os.path.join(REL_DIR, src_name)
        dst = os.path.join(REL_DIR, dst_name)
        if os.path.exists(src) and not os.path.exists(dst):
            shutil.copy2(src, dst)
    
    print(f"  {C.GREEN}✓{C.RESET} DLLs configured")
    return True

def verify_build():
    """Check if the build was successful."""
    exe_path = os.path.join(REL_DIR, "zt1-engine.exe")
    if not os.path.exists(exe_path):
        print(f"  {C.RED}✗{C.RESET} zt1-engine.exe not found!")
        print(f"  {C.YELLOW}Did Step 3 (Build) complete successfully?{C.RESET}")
        return False
    
    print(f"  {C.GREEN}✓{C.RESET} zt1-engine.exe found")
    return True

def main():
    enable_ansi()
    print(f"""
{C.CYAN}{C.BOLD}╔══════════════════════════════════════════════════════════════════╗
║          ZOO TYCOON 1 ENGINE - STEP 5: SETUP RUNTIME             ║
╚══════════════════════════════════════════════════════════════════╝{C.RESET}
""")
    
    print(f"  Release Folder: {C.CYAN}{REL_DIR}{C.RESET}")
    print()
    
    os.makedirs(REL_DIR, exist_ok=True)
    
    # Run all setup steps
    all_ok = True
    
    print(f"  {C.CYAN}Setting up runtime environment...{C.RESET}")
    print()
    
    if not verify_build():
        all_ok = False
    
    if not setup_fonts():
        all_ok = False
    
    setup_zoo_ini()
    setup_folders()
    setup_dlls()
    
    # Final summary
    print()
    if all_ok:
        print(f"  {C.GREEN}{C.BOLD}╔════════════════════════════════════════╗{C.RESET}")
        print(f"  {C.GREEN}{C.BOLD}║      RUNTIME SETUP COMPLETE! 🎮        ║{C.RESET}")
        print(f"  {C.GREEN}{C.BOLD}╚════════════════════════════════════════╝{C.RESET}")
        print()
        print(f"  You can now run: {C.CYAN}zt1-engine.exe{C.RESET}")
        print(f"  Location: {C.DIM}{REL_DIR}{C.RESET}")
        
        # Offer to launch
        print()
        response = input(f"  {C.CYAN}Launch game now? [Y/n]:{C.RESET} ").strip().lower()
        if response in ['', 'y', 'yes']:
            exe_path = os.path.join(REL_DIR, "zt1-engine.exe")
            print(f"\n  {C.GREEN}🚀 Launching Zoo Tycoon...{C.RESET}")
            import subprocess
            subprocess.Popen([exe_path], cwd=REL_DIR)
    else:
        print(f"  {C.YELLOW}Setup completed with warnings{C.RESET}")
        print(f"  {C.DIM}Review the messages above{C.RESET}")
    
    return 0 if all_ok else 1

if __name__ == "__main__":
    try:
        code = main()
        print()
        input(f"  {C.CYAN}Press Enter to exit...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled{C.RESET}")
        sys.exit(1)