#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║           ZOO TYCOON 1 ENGINE - STEP 1: CLEAN WORKSPACE          ║
║              (AGGRESSIVE MODE: KILLS LOCKS & ONEDRIVE)           ║
╚══════════════════════════════════════════════════════════════════╝
"""

import os
import sys
import shutil
import time
import stat

# Navigate to project root (parent of this script's folder)
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
    MAGENTA = '\033[95m'

def enable_ansi():
    if os.name == 'nt':
        os.system('')

def force_kill(proc_name):
    """Aggressively kills a process to release file locks."""
    try:
        # /F = Force, /IM = Image Name, >nul = Silence output
        ret = os.system(f"taskkill /F /IM {proc_name} >nul 2>&1")
        return ret == 0
    except:
        return False

def remove_readonly(func, path, excinfo):
    """
    Error handler for shutil.rmtree.
    If a file is Read-Only (locked by git/OneDrive), this strips the flag and retries.
    """
    try:
        os.chmod(path, stat.S_IWRITE)
        func(path)
    except Exception:
        pass

def nuke_path(path):
    """Tries to delete a file or folder with extreme prejudice."""
    if os.path.isdir(path):
        shutil.rmtree(path, onerror=remove_readonly)
    else:
        try:
            os.remove(path)
        except PermissionError:
            os.chmod(path, stat.S_IWRITE)
            os.remove(path)

def main():
    enable_ansi()
    print(f"""
{C.RED}{C.BOLD}+==================================================================+
|      ZOO TYCOON 1 ENGINE - STEP 1: FORCE CLEAN (NO MERCY)        |
+==================================================================+{C.RESET}
""")
    
    print(f"  Project Root: {C.CYAN}{ROOT_DIR}{C.RESET}")
    print(f"  Build Folder: {C.CYAN}{BUILD_DIR}{C.RESET}")
    print()

    # --- PHASE 1: KILL THE LOCKERS ---
    print(f"  {C.MAGENTA}[PHASE 1] Terminating Hostiles...{C.RESET}")
    
    # Kill the game if it's zombie
    force_kill("zt1-engine.exe")
    print(f"  {C.GREEN}   -> Game Process Killed.{C.RESET}")

    # Kill OneDrive (The usual suspect)
    force_kill("OneDrive.exe")
    print(f"  {C.GREEN}   -> OneDrive Killed (It will restart later).{C.RESET}")
    
    # Short pause to let Windows release the handles
    time.sleep(1) 
    print()

    if not os.path.exists(BUILD_DIR):
        print(f"  {C.GREEN}[OK]{C.RESET} Workspace is already clean.")
        return 0
    
    # --- PHASE 2: SURGICAL REMOVAL ---
    print(f"  {C.YELLOW}[PHASE 2] Scrubbing Files...{C.RESET}")
    print(f"  {C.YELLOW}(Preserving fonts/saves){C.RESET}")
    
    deleted_count = 0
    
    try:
        # We walk the build dir manually to preserve specific folders
        for item in os.listdir(BUILD_DIR):
            item_path = os.path.join(BUILD_DIR, item)
            
            # If we are in the Release folder, be careful
            if item == "Release" and os.path.isdir(item_path):
                for sub_item in os.listdir(item_path):
                    sub_path = os.path.join(item_path, sub_item)
                    
                    # PRESERVE THESE
                    if sub_item.lower() in ["fonts", "saves"]:
                        print(f"  {C.CYAN}   -> Preserving: Release/{sub_item}{C.RESET}")
                        continue
                    
                    # DESTROY EVERYTHING ELSE
                    nuke_path(sub_path)
                    deleted_count += 1
            else:
                # DESTROY TOP LEVEL ITEMS (obj folders, etc)
                nuke_path(item_path)
                deleted_count += 1
        
        print()
        print(f"  {C.GREEN}[SUCCESS]{C.RESET} Vaporized {deleted_count} items.")
        return 0
        
    except Exception as e:
        print(f"  {C.RED}[FAIL]{C.RESET} Even force delete failed: {e}")
        print(f"  {C.YELLOW}Verify you aren't editing a file in VS Code right now.{C.RESET}")
        return 1

if __name__ == "__main__":
    try:
        code = main()
        # No input() pause needed for automation, but keeping it if you run manually
        if len(sys.argv) == 1: 
             print()
             # input(f"  {C.CYAN}Press Enter to continue...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        sys.exit(1)