#!/usr/bin/env python3
"""
+==================================================================+
|          ZOO TYCOON 1 ENGINE - STEP 5: SETUP RUNTIME             |
+==================================================================+
"""

import os
import sys
import shutil

# Navigate to project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")

# Shared game-folder discovery + pc-sync override rules (see zt1_paths.py)
sys.path.insert(0, SCRIPT_DIR)
import zt1_paths

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

def setup_pc_sync_files():
    """Copy custom files from pc-sync folder to Release.

    The ui/ subfolder is special: most of its layouts are byte-identical to the
    BASE UI.ZTD ones, and installing those loose stops the patched layouts in
    zupdate / zupdate1 / xpack2 from being used (the expansion filter dropdown
    then vanishes from every buy panel). zt1_paths knows which few files are
    real overrides.
    """
    pc_sync_dir = os.path.join(ROOT_DIR, "pc-sync")
    
    if not os.path.exists(pc_sync_dir):
        print(f"  {C.DIM}No pc-sync folder found - skipping{C.RESET}")
        return True
    
    file_count = 0
    # Recursively copy all files maintaining structure
    for root, dirs, files in os.walk(pc_sync_dir):
        rel_path = os.path.relpath(root, pc_sync_dir)

        # ui/ is done separately, and only for the genuine overrides
        if rel_path == "ui" or rel_path.startswith("ui" + os.sep):
            continue

        dest_base = REL_DIR if rel_path == "." else os.path.join(REL_DIR, rel_path)
        
        os.makedirs(dest_base, exist_ok=True)
        
        for f in files:
            # pc-sync/zoo.ini has [resource] path=. - copying it over the
            # zoo.ini setup_zoo_ini() wrote drops zupdate/zupdate1/xpack1/
            # xpack2 from the search path, so only the base UI.ZTD layouts
            # load: no expansion filter dropdown, no terrain brush preview.
            if rel_path == "." and f.lower() == "zoo.ini":
                continue
            # (pc-sync's own note about what goes in it)
            if rel_path == "." and f.lower() == "readme.txt":
                continue
            src = os.path.join(root, f)
            dst = os.path.join(dest_base, f)
            shutil.copy2(src, dst)
            file_count += 1

    # pc-sync/ui/startup, ui/freeroam and ui/scenario hold textures that go to
    # the Release ROOT (startup/, freeroam/, scenario/). They are not layouts,
    # so they are copied in full - losing them leaves panels blank.
    for folder in ("startup", "freeroam", "scenario"):
        src = os.path.join(pc_sync_dir, "ui", folder)
        if os.path.isdir(src):
            file_count += copy_tree(src, os.path.join(REL_DIR, folder))
    # ...and they also keep their own location under Release/ui/
    for folder in ("startup", "freeroam", "scenario"):
        src = os.path.join(pc_sync_dir, "ui", folder)
        if os.path.isdir(src):
            file_count += copy_tree(src, os.path.join(REL_DIR, "ui", folder))

    # Only the pc-sync UI files that really override an archive
    file_count += zt1_paths.install_pc_sync_ui(REL_DIR, pc_sync_dir)
    
    print(f"  {C.GREEN}[OK]{C.RESET} Custom files installed ({file_count} files from pc-sync)")
    return True


def copy_tree(src, dst):
    """Copy every file under src into dst, keeping the structure."""
    count = 0
    for root, _, files in os.walk(src):
        rel_path = os.path.relpath(root, src)
        out = dst if rel_path == "." else os.path.join(dst, rel_path)
        os.makedirs(out, exist_ok=True)
        for f in files:
            shutil.copy2(os.path.join(root, f), os.path.join(out, f))
            count += 1
    return count

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
        print(f"  {C.YELLOW}[!] Missing fonts in {fonts_src}:{C.RESET}")
        for f in fonts_missing:
            print(f"    {C.RED}[X]{C.RESET} {f}")
        print(f"  {C.DIM}Download from: https://www.fontsquirrel.com/fonts/aileron{C.RESET}")
        return False
    
    # Copy fonts to Release
    os.makedirs(fonts_dst, exist_ok=True)
    for font in required_fonts:
        src = os.path.join(fonts_src, font)
        dst = os.path.join(fonts_dst, font)
        shutil.copy2(src, dst)
    
    print(f"  {C.GREEN}[OK]{C.RESET} Fonts installed")
    return True

RESOURCE_PATH = ("./dlupdate;./updates;./xpack2/loc;./xpack2;./dupdate;"
                 "./zupdate1;./xpack1/loc;./xpack1;./zupdate;./loc;.")


def check_resource_path():
    """zoo.ini's [resource] path must list the patch/expansion folders.

    With just 'path=.' the engine indexes only the base archives, so the
    patched layouts in zupdate/zupdate1 never load: the buy panels lose the
    expansion filter dropdown and Modify Terrain loses its brush preview.
    Rewrites zoo.ini if anything left it wrong.
    """
    ini_path = os.path.join(REL_DIR, "zoo.ini")
    current = ""
    section = ""
    if os.path.exists(ini_path):
        for line in open(ini_path, encoding="utf-8", errors="replace"):
            s = line.strip()
            if s.startswith("["):
                section = s.lower()
            elif section == "[resource]" and s.lower().startswith("path"):
                current = s.split("=", 1)[1].strip()
    if current == RESOURCE_PATH:
        print(f"  {C.GREEN}[OK]{C.RESET} zoo.ini resource path includes the patches and expansions")
        return True
    print(f"  {C.YELLOW}[!]{C.RESET} zoo.ini resource path was '{current}' - rewriting it")
    setup_zoo_ini()
    return True


def setup_zoo_ini():
    """Create the zoo.ini configuration file."""
    # Same search order as the original game's zoo.ini: patches and
    # expansions first, base game (".") last
    ini_content = """[resource]
path=./dlupdate;./updates;./xpack2/loc;./xpack2;./dupdate;./zupdate1;./xpack1/loc;./xpack1;./zupdate;./loc;.

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
    # The player's own settings ([user]: uiScale, widescreen, timedunlocks,
    # artupscale, the resolution...) are kept: only the rest is rewritten
    try:
        with open(ini_path, encoding="utf-8", errors="replace") as f:
            old = f.read()
        lines = old.splitlines()
        start = next((i for i, l in enumerate(lines) if l.strip().lower() == "[user]"), None)
        if start is not None:
            end = next((i for i in range(start + 1, len(lines)) if lines[i].strip().startswith("[")), len(lines))
            user = "\n".join(lines[start:end]).rstrip() + "\n"
            ini_content = ini_content[:ini_content.index("[user]")] + user
    except OSError:
        pass
    with open(ini_path, "w", encoding="utf-8") as f:
        f.write(ini_content)
    
    print(f"  {C.GREEN}[OK]{C.RESET} zoo.ini created")
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
    
    print(f"  {C.GREEN}[OK]{C.RESET} Game folders created")
    return True

def ensure_expansion_dlls():
    """Make sure lang100.dll / lang200.dll (Dinosaur Digs, Marine Mania) are in
    Release.

    They normally arrive with the game files, but a base-game-only import or an
    older import step can leave them out. Copy them from the source game folder
    or from known install locations when possible; otherwise just say so.
    """
    have = [d for d in zt1_paths.EXPANSION_DLLS
            if os.path.exists(os.path.join(REL_DIR, d))]
    missing = [d for d in zt1_paths.EXPANSION_DLLS if d not in have]
    if not missing:
        return True

    # Look in the remembered / discovered game folder
    game_dir = zt1_paths.find_game_dir()
    copied = 0
    if game_dir:
        for d in list(missing):
            src = os.path.join(game_dir, d)
            if os.path.isfile(src):
                try:
                    shutil.copy2(src, os.path.join(REL_DIR, d))
                    copied += 1
                    missing.remove(d)
                except OSError:
                    pass

    if copied:
        print(f"  {C.GREEN}[OK]{C.RESET} Expansion language DLLs copied ({copied})")

    if missing:
        print(f"  {C.YELLOW}[!]{C.RESET} Missing expansion language DLL(s): "
              f"{', '.join(missing)}")
        print(f"  {C.DIM}    Dinosaur Digs / Marine Mania map names and loading "
              f"screens need them.{C.RESET}")
        print(f"  {C.DIM}    Install both expansions over the base game, then re-run "
              f"this step.{C.RESET}")
    return not missing


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

    # The expansion language resources. Without these the Dinosaur Digs and
    # Marine Mania map names come out blank and the game reports
    # "Could not load content of loading screen".
    ensure_expansion_dlls()
    
    for src_name, dst_name in fallbacks:
        src = os.path.join(REL_DIR, src_name)
        dst = os.path.join(REL_DIR, dst_name)
        if os.path.exists(src) and not os.path.exists(dst):
            shutil.copy2(src, dst)
    
    print(f"  {C.GREEN}[OK]{C.RESET} DLLs configured")
    return True

def verify_build():
    """Check if the build was successful."""
    exe_path = os.path.join(REL_DIR, "zt1-engine.exe")
    if not os.path.exists(exe_path):
        print(f"  {C.RED}[X]{C.RESET} zt1-engine.exe not found!")
        print(f"  {C.YELLOW}Did Step 3 (Build) complete successfully?{C.RESET}")
        return False
    
    print(f"  {C.GREEN}[OK]{C.RESET} zt1-engine.exe found")
    return True

def setup_ui_files():
    """Extract UI layout files and set up button assets."""
    import zipfile
    import subprocess
    
    ui_ztd = os.path.join(REL_DIR, "ui.ztd")
    ui_dir = os.path.join(REL_DIR, "ui")
    tools_dir = os.path.join(ROOT_DIR, "tools")
    
    if not os.path.exists(ui_ztd):
        print(f"  {C.YELLOW}[!]{C.RESET} ui.ztd not found - skipping UI setup")
        print(f"  {C.DIM}Copy Zoo Tycoon game files to: {REL_DIR}{C.RESET}")
        return True
    
    # 1. Extract only .lyt files from ui.ztd (layout files for UI positioning).
    # Skip any layout an expansion or patch archive also provides: a loose
    # copy of the base version would override it (e.g. the Marine Mania
    # mapselec.lyt with its own background and Starting Cash box).
    try:
        os.makedirs(ui_dir, exist_ok=True)
        overridden = set()
        for sub in ["dlupdate", "updates", "xpack2", "dupdate", "zupdate1",
                    "xpack1", "zupdate"]:
            folder = os.path.join(REL_DIR, sub)
            if not os.path.isdir(folder):
                continue
            for name in os.listdir(folder):
                if not name.lower().endswith(".ztd"):
                    continue
                try:
                    with zipfile.ZipFile(os.path.join(folder, name)) as xz:
                        overridden.update(f.lower() for f in xz.namelist())
                except zipfile.BadZipFile:
                    pass
        with zipfile.ZipFile(ui_ztd, 'r') as z:
            lyt_files = [f for f in z.namelist()
                         if (f.endswith('.lyt') or f.endswith('.cfg'))
                         and f.lower() not in overridden]
            for f in lyt_files:
                # Extract just the file, preserving path
                z.extract(f, REL_DIR)
        print(f"  {C.GREEN}[OK]{C.RESET} UI layout files extracted ({len(lyt_files)} files)")
    except Exception as e:
        print(f"  {C.RED}[X]{C.RESET} Failed to extract UI files: {e}")
        return False
    
    # 2. Run button background decoder
    decoder_script = os.path.join(tools_dir, "decode_button_animation.py")
    if os.path.exists(decoder_script):
        try:
            result = subprocess.run(
                [sys.executable, decoder_script],
                cwd=REL_DIR,
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                print(f"  {C.GREEN}[OK]{C.RESET} Button backgrounds decoded")
            else:
                print(f"  {C.YELLOW}[!]{C.RESET} Button decoder had issues: {result.stderr[:100] if result.stderr else 'unknown'}")
        except Exception as e:
            print(f"  {C.YELLOW}[!]{C.RESET} Could not run button decoder: {e}")
    
    # 3. (The menu's position is the original game's: set_menu_x.py, which
    # moved it, isn't run)
    menu_script = None
    if menu_script and os.path.exists(menu_script):
        try:
            result = subprocess.run(
                [sys.executable, menu_script],
                cwd=ROOT_DIR,
                capture_output=True,
                text=True
            )
            if result.returncode == 0:
                print(f"  {C.GREEN}[OK]{C.RESET} Menu positions configured")
            else:
                print(f"  {C.YELLOW}[!]{C.RESET} Menu script had issues")
        except Exception as e:
            print(f"  {C.YELLOW}[!]{C.RESET} Could not run menu script: {e}")
    
    return True

def main():
    enable_ansi()
    print(f"""
{C.CYAN}{C.BOLD}+==================================================================+
|          ZOO TYCOON 1 ENGINE - STEP 5: SETUP RUNTIME             |
+==================================================================+{C.RESET}
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
    setup_ui_files()
    
    setup_pc_sync_files()
    if not check_resource_path():
        all_ok = False
    
    # Final summary
    print()
    if all_ok:
        print(f"  {C.GREEN}{C.BOLD}+========================================+{C.RESET}")
        print(f"  {C.GREEN}{C.BOLD}|      RUNTIME SETUP COMPLETE! 🎮        |{C.RESET}")
        print(f"  {C.GREEN}{C.BOLD}+========================================+{C.RESET}")
        print()
        print(f"  You can now run: {C.CYAN}zt1-engine.exe{C.RESET}")
        print(f"  Location: {C.DIM}{REL_DIR}{C.RESET}")
        
        # Offer to launch (skipped when start.bat is driving: it launches)
        if not os.environ.get("ZT1_NO_LAUNCH_PROMPT"):
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
        # start.bat drives this script directly and does not want to be paused
        if not os.environ.get("ZT1_NO_LAUNCH_PROMPT"):
            print()
            input(f"  {C.CYAN}Press Enter to exit...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled{C.RESET}")
        sys.exit(1)