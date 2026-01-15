#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║           ZOO TYCOON 1 ENGINE - STEP 4: IMPORT ASSETS            ║
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

def check_assets_present():
    """Check if essential game assets are already present."""
    required = ["animals.ztd", "ui.ztd", "scenery.ztd"]
    present = sum(1 for f in required if os.path.exists(os.path.join(REL_DIR, f)))
    return present >= 2  # At least 2 of 3 required files

def import_from_folder(source_dir):
    """Import game files from source directory."""
    extensions = ['.ztd', '.dll', '.ini', '.wav', '.avi']
    files_copied = 0
    
    print(f"  {C.CYAN}Scanning source folder...{C.RESET}")
    
    for filename in os.listdir(source_dir):
        lower_name = filename.lower()
        
        if any(lower_name.endswith(ext) for ext in extensions):
            src_path = os.path.join(source_dir, filename)
            dst_path = os.path.join(REL_DIR, filename)
            
            try:
                if os.path.isfile(src_path):
                    # Skip if file already exists and is same size
                    if os.path.exists(dst_path):
                        if os.path.getsize(src_path) == os.path.getsize(dst_path):
                            continue
                    
                    print(f"    {C.DIM}Copying: {filename}{C.RESET}")
                    shutil.copy2(src_path, dst_path)
                    files_copied += 1
            except Exception as e:
                print(f"    {C.RED}Failed: {filename} - {e}{C.RESET}")
    
    return files_copied

def main():
    enable_ansi()
    print(f"""
{C.CYAN}{C.BOLD}╔══════════════════════════════════════════════════════════════════╗
║           ZOO TYCOON 1 ENGINE - STEP 4: IMPORT ASSETS            ║
╚══════════════════════════════════════════════════════════════════╝{C.RESET}
""")
    
    print(f"  Destination: {C.CYAN}{REL_DIR}{C.RESET}")
    print()
    
    # Create Release folder if needed
    os.makedirs(REL_DIR, exist_ok=True)
    
    # Check if assets already present
    if check_assets_present():
        print(f"  {C.GREEN}✓{C.RESET} Game assets already present")
        print(f"  {C.DIM}Skipping import (delete .ztd files to re-import){C.RESET}")
        return 0
    
    # Ask for source folder
    print(f"  {C.YELLOW}Game assets not found. Import required.{C.RESET}")
    print()
    print(f"  Please enter the path to your original Zoo Tycoon installation")
    print(f"  {C.DIM}(where animals.ztd and zoo.exe are located){C.RESET}")
    print()
    
    # Try to use tkinter for folder dialog
    try:
        import tkinter as tk
        from tkinter import filedialog
        
        root = tk.Tk()
        root.withdraw()
        root.attributes('-topmost', True)
        
        source_dir = filedialog.askdirectory(
            title="Select Original Zoo Tycoon Folder",
            mustexist=True
        )
        
        root.destroy()
        
        if not source_dir:
            print(f"  {C.YELLOW}No folder selected. Skipping import.{C.RESET}")
            return 0
            
    except ImportError:
        # Fallback to manual entry
        source_dir = input(f"  {C.CYAN}Path:{C.RESET} ").strip().strip('"')
    
    if not source_dir or not os.path.exists(source_dir):
        print(f"  {C.RED}✗ Invalid path{C.RESET}")
        return 1
    
    print()
    print(f"  Source: {C.CYAN}{source_dir}{C.RESET}")
    print()
    
    # Import files
    files_copied = import_from_folder(source_dir)
    
    print()
    if files_copied > 0:
        print(f"  {C.GREEN}✓ Imported {files_copied} files{C.RESET}")
    else:
        print(f"  {C.YELLOW}No new files to import{C.RESET}")
    
    return 0

if __name__ == "__main__":
    try:
        code = main()
        print()
        input(f"  {C.CYAN}Press Enter to continue...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled{C.RESET}")
        sys.exit(1)