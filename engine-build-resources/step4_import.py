#!/usr/bin/env python3
"""
+==================================================================+
|           ZOO TYCOON 1 ENGINE - STEP 4: IMPORT ASSETS            |
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

# Shared game-folder discovery + the pc-sync override rules (see zt1_paths.py
# for why only some pc-sync UI files may be installed loose).
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

def check_assets_present():
    """Check if essential game assets are already present."""
    required = ["animals.ztd", "ui.ztd", "scenery.ztd"]
    present = sum(1 for f in required if os.path.exists(os.path.join(REL_DIR, f)))
    return present >= 2  # At least 2 of 3 required files

def import_from_folder(source_dir):
    """Import ALL game files and folders from source directory."""
    files_copied = 0
    dirs_copied = 0
    
    # Files/folders to skip (engine-specific, not needed)
    skip_names = {'desktop.ini', 'thumbs.db', '.ds_store'}
    
    print(f"  {C.CYAN}Importing from: {source_dir}{C.RESET}")
    
    for item in os.listdir(source_dir):
        if item.lower() in skip_names:
            continue
            
        src_path = os.path.join(source_dir, item)
        dst_path = os.path.join(REL_DIR, item)
        
        try:
            if os.path.isfile(src_path):
                # Skip if file already exists and is same size
                if os.path.exists(dst_path):
                    if os.path.getsize(src_path) == os.path.getsize(dst_path):
                        continue
                
                print(f"    {C.DIM}Copying file: {item}{C.RESET}")
                shutil.copy2(src_path, dst_path)
                files_copied += 1
                
            elif os.path.isdir(src_path):
                # Copy entire directory tree
                if os.path.exists(dst_path):
                    # Merge: copy contents into existing dir
                    for root, dirs, files in os.walk(src_path):
                        rel_path = os.path.relpath(root, src_path)
                        dst_root = os.path.join(dst_path, rel_path) if rel_path != '.' else dst_path
                        os.makedirs(dst_root, exist_ok=True)
                        
                        for file in files:
                            if file.lower() in skip_names:
                                continue
                            src_file = os.path.join(root, file)
                            dst_file = os.path.join(dst_root, file)
                            if not os.path.exists(dst_file):
                                shutil.copy2(src_file, dst_file)
                                files_copied += 1
                else:
                    print(f"    {C.DIM}Copying folder: {item}{C.RESET}")
                    shutil.copytree(src_path, dst_path)
                    dirs_copied += 1
                    
        except Exception as e:
            print(f"    {C.RED}Failed: {item} - {e}{C.RESET}")
    
    print(f"  {C.GREEN}[OK]{C.RESET} Copied {files_copied} files, {dirs_copied} folders{C.RESET}")
    return files_copied + dirs_copied

def main():
    enable_ansi()
    print(f"""
{C.CYAN}{C.BOLD}+==================================================================+
|           ZOO TYCOON 1 ENGINE - STEP 4: IMPORT ASSETS            |
+==================================================================+{C.RESET}
""")
    
    print(f"  Destination: {C.CYAN}{REL_DIR}{C.RESET}")
    print()
    
    # Create Release folder if needed
    os.makedirs(REL_DIR, exist_ok=True)
    
    # Check if assets already present
    if check_assets_present():
        print(f"  {C.GREEN}[OK]{C.RESET} Game assets already present")
        print(f"  {C.DIM}Skipping import (delete .ztd files to re-import){C.RESET}")
        # The Marine Mania layouts still have to be there: on a rebuild from a
        # clean build\ the assets survive but ui/ may not.
        check_pc_sync_assets()
        return 0
    
    # Where are the game files?
    #   1. ZT1_SOURCE_DIR (set by start.bat when the user picks a folder)
    #   2. a folder remembered from a previous run
    #   3. the usual retail / digital install locations
    #   4. ask, with a folder-picker dialog
    source_dir = zt1_paths.find_game_dir()

    if source_dir:
        print(f"  {C.CYAN}Found Zoo Tycoon folder: {source_dir}{C.RESET}")
    else:
        print(f"  {C.YELLOW}Game assets not found. Import required.{C.RESET}")
        print()
        print(f"  Please choose your original Zoo Tycoon installation folder")
        print(f"  {C.DIM}(the one containing animals.ztd and zoo.exe){C.RESET}")
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

    if not source_dir or not os.path.isdir(source_dir):
        print(f"  {C.RED}[X]{C.RESET} Invalid path{C.RESET}")
        return 1

    if zt1_paths.is_engine_dir(source_dir):
        print(f"  {C.RED}[X]{C.RESET} That's this engine's own folder - pick the folder "
              f"Zoo Tycoon is installed in. Nothing imported.")
        return 1
    if not zt1_paths.looks_like_game_dir(source_dir):
        print(f"  {C.RED}[X]{C.RESET} That folder has no animals.ztd / ui.ztd / scenery.ztd "
              f"(it needs two) - nothing imported.")
        return 1

    # Remember it so the next run (and start.bat) does not ask again
    zt1_paths.save_game_dir(source_dir)

    expansions = zt1_paths.missing_expansions(source_dir)
    if expansions:
        print(f"  {C.YELLOW}[!]{C.RESET} Dinosaur Digs / Marine Mania content may be "
              f"missing ({', '.join(expansions)}).")
        print(f"  {C.DIM}    Install both expansions over the base game and re-run to "
              f"enable their maps and the buy-panel filter.{C.RESET}")

    print()
    print(f"  Source: {C.CYAN}{source_dir}{C.RESET}")
    print()
    
    # Import files
    files_copied = import_from_folder(source_dir)
    
    print()
    if files_copied > 0:
        print(f"  {C.GREEN}[OK]{C.RESET} Imported {files_copied} files{C.RESET}")
    else:
        print(f"  {C.YELLOW}No new files to import{C.RESET}")

    # pc-sync assets are installed AFTER the import on purpose:
    # extract_marine_mania_ui() reads XPACK2\ui6.ztd out of the imported game
    # files, so running it before the import would find no archive on a fresh
    # clone and silently skip the 20 Marine Mania UI layouts.
    check_pc_sync_assets()

    return 0

def copy_folder(src, dst):
    """Copy a folder if it exists."""
    if os.path.exists(src):
        # Remove destination if it exists
        if os.path.exists(dst):
            try:
                shutil.rmtree(dst)
            except Exception as e:
                print(f"    {C.RED}Failed to remove existing {dst}: {e}{C.RESET}")
                return False
        
        try:
            print(f"    {C.DIM}Copying folder: {os.path.basename(src)}{C.RESET}")
            shutil.copytree(src, dst)
            return True
        except Exception as e:
            print(f"    {C.RED}Failed to copy {src}: {e}{C.RESET}")
            return False
    return False

def check_pc_sync_assets():
    """Copy assets from pc-sync if present.

    UI layouts are handled by zt1_paths.install_pc_sync_ui(), which installs
    ONLY the few pc-sync files that genuinely override an archive. Copying the
    whole pc-sync/ui folder here would put 76 base UI.ZTD layouts loose and
    hide the patched ones (no expansion filter dropdown in the buy panels).
    """
    pc_sync_dir = os.path.join(ROOT_DIR, "pc-sync")
    if not os.path.exists(pc_sync_dir):
        return 0

    print(f"  {C.CYAN}Checking pc-sync for assets...{C.RESET}")

    # Sub-folders of pc-sync/ui that are installed to the Release ROOT (not to
    # Release/ui). These hold textures the UI needs, and they are expensive to
    # lose: without them panels come up blank or incomplete.
    # (not xpack1 / xpack2: copy_folder deletes the folder first, and on
    # Windows that would be the player's imported Dinosaur Digs / Marine
    # Mania)
    folders = ["startup", "freeroam", "scenario"]
    copied = 0

    for folder in folders:
        src = os.path.join(pc_sync_dir, folder)
        if not os.path.exists(src):
            # pc-sync keeps these under ui/
            src_ui = os.path.join(pc_sync_dir, "ui", folder)
            if os.path.exists(src_ui):
                src = src_ui
        dst = os.path.join(REL_DIR, folder)
        if copy_folder(src, dst):
            copied += 1

    # The nested folders inside ui/ keep their location (Release/ui/...)
    for folder in ("ui/scenario",):
        src = os.path.join(pc_sync_dir, *folder.split("/"))
        dst = os.path.join(REL_DIR, *folder.split("/"))
        if copy_folder(src, dst):
            copied += 1

    # Only the real overrides, never the loose base layouts
    zt1_paths.install_pc_sync_ui(REL_DIR, pc_sync_dir)
    
    # [PATCH] Extract Marine Mania UI from XPACK2
    extract_marine_mania_ui()
    
    return copied


def extract_marine_mania_ui():
    """Extract Marine Mania themed UI from XPACK2/ui6.ztd."""
    import zipfile
    
    ui6_path = os.path.join(REL_DIR, "XPACK2", "ui6.ztd")
    
    # Also check uppercase path
    if not os.path.exists(ui6_path):
        ui6_path = os.path.join(REL_DIR, "xpack2", "ui6.ztd")
    
    if not os.path.exists(ui6_path):
        return
    
    print(f"  {C.CYAN}Extracting Marine Mania UI from ui6.ztd...{C.RESET}")
    
    # Files to extract from ui6.ztd
    files_to_extract = [
        "ui/scenario.lyt",
        "ui/mapselec.lyt",
        "ui/scenario/aquascen.tga",
        "ui/freeform/aquaffrm.tga",
    ]
    
    try:
        z = zipfile.ZipFile(ui6_path, 'r')
        extracted = 0
        
        for file_path in z.namelist():
            # Skip directory entries (they come through as "ui/freeform/")
            if file_path.endswith("/") or file_path.endswith("\\"):
                continue
            # Check if file is in interesting folders
            lower_path = file_path.lower()
            if (lower_path.startswith("ui/startup/") or 
                lower_path.startswith("ui/scenario/") or
                lower_path.startswith("ui/freeform/") or
                lower_path.endswith(".lyt")):
                
                try:
                    # Create output path
                    out_path = os.path.join(REL_DIR, file_path.replace('/', os.sep))
                    out_dir = os.path.dirname(out_path)
                    os.makedirs(out_dir, exist_ok=True)
                    
                    # Extract file
                    data = z.read(file_path)
                    with open(out_path, 'wb') as f:
                        f.write(data)
                    
                    extracted += 1
                except Exception as e:
                    print(f"    {C.RED}Failed to extract {file_path}: {e}{C.RESET}")
        
        z.close()

        
        if extracted > 0:
            print(f"  {C.GREEN}✓ Extracted {extracted} Marine Mania UI files{C.RESET}")
            
    except Exception as e:
        print(f"  {C.RED}Failed to open ui6.ztd: {e}{C.RESET}")

if __name__ == "__main__":
    try:
        code = main()
        print()
        input(f"  {C.CYAN}Press Enter to continue...{C.RESET}")
        sys.exit(code)
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled{C.RESET}")
        sys.exit(1)