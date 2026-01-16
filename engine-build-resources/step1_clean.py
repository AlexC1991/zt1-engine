#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║           ZOO TYCOON 1 ENGINE - STEP 1: CLEAN WORKSPACE          ║
╚══════════════════════════════════════════════════════════════════╝
"""

import os
import sys
import shutil
import time

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

def enable_ansi():
    if os.name == 'nt':
        os.system('')

def main():
    enable_ansi()
    print(f"""
{C.CYAN}{C.BOLD}╔══════════════════════════════════════════════════════════════════╗
║           ZOO TYCOON 1 ENGINE - STEP 1: CLEAN WORKSPACE          ║
╚══════════════════════════════════════════════════════════════════╝{C.RESET}
""")
    
    print(f"  Project Root: {C.CYAN}{ROOT_DIR}{C.RESET}")
    print(f"  Build Folder: {C.CYAN}{BUILD_DIR}{C.RESET}")
    print()
    
    if not os.path.exists(BUILD_DIR):
        print(f"  {C.GREEN}✓{C.RESET} Workspace is already clean (no build folder)")
        return 0
    
    print(f"  {C.YELLOW}Cleaning build folder...{C.RESET}")
    print(f"  {C.YELLOW}(Preserving fonts folder if present){C.RESET}")
    print()
    
    # Track preserved items
    preserved = []
    deleted = []
    
    try:
        for item in os.listdir(BUILD_DIR):
            item_path = os.path.join(BUILD_DIR, item)
            
            if item == "Release" and os.path.isdir(item_path):
                # Inside Release, preserve fonts folder
                for sub_item in os.listdir(item_path):
                    sub_path = os.path.join(item_path, sub_item)
                    if sub_item == "fonts":
                        preserved.append("Release/fonts")
                        continue
                    
                    if os.path.isdir(sub_path):
                        shutil.rmtree(sub_path)
                    else:
                        os.remove(sub_path)
                    deleted.append(f"Release/{sub_item}")
            else:
                if os.path.isdir(item_path):
                    shutil.rmtree(item_path)
                else:
                    os.remove(item_path)
                deleted.append(item)
        
        print(f"  {C.GREEN}✓{C.RESET} Cleaned {len(deleted)} items")
        if preserved:
            print(f"  {C.CYAN}✓{C.RESET} Preserved: {', '.join(preserved)}")
        
        return 0
        
    except PermissionError as e:
        print(f"  {C.RED}✗{C.RESET} Permission denied: {e}")
        print(f"  {C.YELLOW}Tip: Close the game and any file explorers in the build folder{C.RESET}")
        return 1
    except Exception as e:
        print(f"  {C.RED}✗{C.RESET} Error: {e}")
        return 1

if __name__ == "__main__":
    try:
        code = main()
        print()
        input(f"  {C.CYAN}Press Enter to continue...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled{C.RESET}")
        sys.exit(1)