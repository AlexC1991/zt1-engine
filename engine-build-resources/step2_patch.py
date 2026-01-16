#!/usr/bin/env python3
"""
╔══════════════════════════════════════════════════════════════════╗
║          ZOO TYCOON 1 ENGINE - STEP 2: PATCH RUNNER              ║
║        (Automatically applies scripts from /patches folder)      ║
║                    + Auto-Archive Completed Patches              ║
╚══════════════════════════════════════════════════════════════════╝
"""

import os
import sys
import shutil
import importlib.util

# Navigate to project root
SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
PATCHES_DIR = os.path.join(SCRIPT_DIR, "patches")
ARCHIVE_DIR = os.path.join(PATCHES_DIR, "archive")
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
SRC_DIR = os.path.join(ROOT_DIR, "src")

class C:
    RESET = '\033[0m'
    GREEN = '\033[92m'
    CYAN = '\033[96m'
    YELLOW = '\033[93m'
    RED = '\033[91m'
    DIM = '\033[2m'
    BOLD = '\033[1m'

# Patch categories for documentation
PATCH_CATEGORIES = {
    "Core Build Fixes (01-08)": [
        "01_", "02_", "03_", "04_", "05_", "06_", "07_", "08_"
    ],
    "Input System (09)": [
        "09_"
    ],
    "UI ListBox Feature (10, 13-14)": [
        "10_", "13_", "14_"
    ],
    "Scenario System (11, 15-17)": [
        "11_", "15_", "16_", "17_"
    ]
}

def enable_ansi():
    if os.name == 'nt': os.system('')

def get_patch_category(filename):
    """Get the category for a patch file"""
    for cat, prefixes in PATCH_CATEGORIES.items():
        for prefix in prefixes:
            if filename.startswith(prefix):
                return cat
    return "Other"

def get_active_patches():
    """Get list of active patch files"""
    if not os.path.exists(PATCHES_DIR):
        return []
    return sorted([f for f in os.listdir(PATCHES_DIR) 
                   if f.endswith(".py") and not f.startswith("_") 
                   and os.path.isfile(os.path.join(PATCHES_DIR, f))])

def get_archived_patches():
    """Get list of archived patch files"""
    if not os.path.exists(ARCHIVE_DIR):
        return []
    return sorted([f for f in os.listdir(ARCHIVE_DIR) if f.endswith(".py")])

def archive_patch_file(patch_name):
    """Move a patch to the archive folder"""
    os.makedirs(ARCHIVE_DIR, exist_ok=True)
    src = os.path.join(PATCHES_DIR, patch_name)
    dst = os.path.join(ARCHIVE_DIR, patch_name)
    if os.path.exists(src):
        shutil.move(src, dst)
        return True
    return False

def run_patches(auto_archive=False):
    """Run all active patches and return count of applied patches"""
    if not os.path.exists(PATCHES_DIR):
        print(f"  {C.YELLOW}⚠ 'patches' folder not found at: {PATCHES_DIR}{C.RESET}")
        return -1, []

    patch_files = get_active_patches()
    
    if not patch_files:
        print(f"  {C.YELLOW}No patch scripts found.{C.RESET}")
        return 0, []

    patches_applied = 0
    patches_skipped = []  # Patches that didn't apply (already done)
    patches_with_errors = []
    
    for p_file in patch_files:
        p_path = os.path.join(PATCHES_DIR, p_file)
        spec = importlib.util.spec_from_file_location("patch_module", p_path)
        module = importlib.util.module_from_spec(spec)
        
        try:
            spec.loader.exec_module(module)
            if hasattr(module, "apply"):
                result = module.apply(SRC_DIR, ROOT_DIR)
                if result:
                    patches_applied += 1
                else:
                    # Patch returned False = already applied / no changes needed
                    patches_skipped.append(p_file)
                
        except Exception as e:
            print(f"  {C.YELLOW}⚠ Error in {p_file}: {e}{C.RESET}")
            patches_with_errors.append(p_file)

    return patches_applied, patches_skipped

def display_patches():
    """Display all patches organized by category"""
    active = get_active_patches()
    archived = get_archived_patches()
    
    print(f"\n  {C.BOLD}ACTIVE PATCHES ({len(active)}):{C.RESET}")
    print(f"  {C.DIM}{'─' * 50}{C.RESET}")
    
    if active:
        # Group by category
        by_category = {}
        for p in active:
            cat = get_patch_category(p)
            if cat not in by_category:
                by_category[cat] = []
            by_category[cat].append(p)
        
        for cat in PATCH_CATEGORIES.keys():
            if cat in by_category:
                print(f"  {C.CYAN}{cat}:{C.RESET}")
                for p in by_category[cat]:
                    print(f"    {C.GREEN}●{C.RESET} {p}")
        
        # Show uncategorized
        if "Other" in by_category:
            print(f"  {C.CYAN}Other:{C.RESET}")
            for p in by_category["Other"]:
                print(f"    {C.GREEN}●{C.RESET} {p}")
    else:
        print(f"  {C.DIM}(none){C.RESET}")
    
    if archived:
        print(f"\n  {C.BOLD}ARCHIVED PATCHES ({len(archived)}):{C.RESET}")
        print(f"  {C.DIM}{'─' * 50}{C.RESET}")
        for p in archived:
            print(f"    {C.DIM}○ {p}{C.RESET}")

def archive_patch():
    """Archive a patch (move to archive folder)"""
    active = get_active_patches()
    if not active:
        print(f"  {C.YELLOW}No active patches to archive.{C.RESET}")
        return
    
    print(f"\n  {C.BOLD}Select patch to archive:{C.RESET}")
    for i, p in enumerate(active, 1):
        cat = get_patch_category(p)
        print(f"    {i}. {p} {C.DIM}[{cat}]{C.RESET}")
    print(f"    {C.DIM}0. Cancel{C.RESET}")
    
    try:
        choice = input(f"\n  Enter number: ").strip()
        if choice == "0" or not choice:
            return
        idx = int(choice) - 1
        if 0 <= idx < len(active):
            patch = active[idx]
            if archive_patch_file(patch):
                print(f"  {C.GREEN}✓ Archived: {patch}{C.RESET}")
        else:
            print(f"  {C.YELLOW}Invalid selection.{C.RESET}")
    except ValueError:
        print(f"  {C.YELLOW}Invalid input.{C.RESET}")

def restore_patch():
    """Restore a patch from archive"""
    archived = get_archived_patches()
    if not archived:
        print(f"  {C.YELLOW}No archived patches to restore.{C.RESET}")
        return
    
    print(f"\n  {C.BOLD}Select patch to restore:{C.RESET}")
    for i, p in enumerate(archived, 1):
        print(f"    {i}. {p}")
    print(f"    {C.DIM}0. Cancel{C.RESET}")
    
    try:
        choice = input(f"\n  Enter number: ").strip()
        if choice == "0" or not choice:
            return
        idx = int(choice) - 1
        if 0 <= idx < len(archived):
            patch = archived[idx]
            src = os.path.join(ARCHIVE_DIR, patch)
            dst = os.path.join(PATCHES_DIR, patch)
            shutil.move(src, dst)
            print(f"  {C.GREEN}✓ Restored: {patch}{C.RESET}")
        else:
            print(f"  {C.YELLOW}Invalid selection.{C.RESET}")
    except ValueError:
        print(f"  {C.YELLOW}Invalid input.{C.RESET}")

def archive_range():
    """Archive multiple patches by range (e.g., 01-08)"""
    active = get_active_patches()
    if not active:
        print(f"  {C.YELLOW}No active patches to archive.{C.RESET}")
        return
    
    print(f"\n  {C.BOLD}Archive by prefix range{C.RESET}")
    print(f"  {C.DIM}Example: '01-08' archives patches 01_xxx through 08_xxx{C.RESET}")
    
    range_input = input(f"\n  Enter range (e.g., 01-08): ").strip()
    if not range_input or '-' not in range_input:
        print(f"  {C.YELLOW}Invalid range format.{C.RESET}")
        return
    
    try:
        start, end = range_input.split('-')
        start_num = int(start)
        end_num = int(end)
        
        archived_count = 0
        for p in active:
            if '_' in p:
                prefix = p.split('_')[0]
                try:
                    num = int(prefix)
                    if start_num <= num <= end_num:
                        if archive_patch_file(p):
                            print(f"    {C.GREEN}✓{C.RESET} Archived: {p}")
                            archived_count += 1
                except ValueError:
                    pass
        
        if archived_count > 0:
            print(f"\n  {C.GREEN}Archived {archived_count} patches.{C.RESET}")
        else:
            print(f"  {C.YELLOW}No patches matched the range.{C.RESET}")
            
    except ValueError:
        print(f"  {C.YELLOW}Invalid range format.{C.RESET}")

def restore_all():
    """Restore all archived patches"""
    archived = get_archived_patches()
    if not archived:
        print(f"  {C.YELLOW}No archived patches to restore.{C.RESET}")
        return
    
    confirm = input(f"\n  Restore all {len(archived)} archived patches? [y/N]: ").strip().lower()
    if confirm != 'y':
        return
    
    restored = 0
    for p in archived:
        src = os.path.join(ARCHIVE_DIR, p)
        dst = os.path.join(PATCHES_DIR, p)
        shutil.move(src, dst)
        print(f"    {C.GREEN}✓{C.RESET} Restored: {p}")
        restored += 1
    
    print(f"\n  {C.GREEN}Restored {restored} patches.{C.RESET}")

def patch_manager_menu():
    """Interactive patch manager menu"""
    while True:
        print(f"\n  {C.BOLD}╔════════════════════════════════════╗{C.RESET}")
        print(f"  {C.BOLD}║       PATCH MANAGER OPTIONS        ║{C.RESET}")
        print(f"  {C.BOLD}╚════════════════════════════════════╝{C.RESET}")
        
        display_patches()
        
        print(f"\n  {C.BOLD}Options:{C.RESET}")
        print(f"    1. Archive a single patch")
        print(f"    2. Archive patches by range (e.g., 01-08)")
        print(f"    3. Restore a single patch")
        print(f"    4. Restore ALL archived patches")
        print(f"    5. Exit patch manager")
        
        choice = input(f"\n  Select option (1-5): ").strip()
        
        if choice == "1":
            archive_patch()
        elif choice == "2":
            archive_range()
        elif choice == "3":
            restore_patch()
        elif choice == "4":
            restore_all()
        elif choice == "5" or not choice:
            break
        else:
            print(f"  {C.YELLOW}Invalid option.{C.RESET}")

def main():
    enable_ansi()
    
    # Check if running in standalone mode or as part of build
    standalone = len(sys.argv) == 1
    
    print(f"\n{C.CYAN}=== STEP 2: APPLYING MODULAR PATCHES ==={C.RESET}")
    
    # Run patches
    patches_applied, patches_skipped = run_patches()
    
    if patches_applied < 0:
        return 1
    elif patches_applied == 0 and not patches_skipped:
        print(f"  {C.GREEN}All patches already applied - source is up to date.{C.RESET}")
    else:
        if patches_applied > 0:
            print(f"  {C.GREEN}Applied updates from {patches_applied} patch(es).{C.RESET}")
        
        # Auto-archive patches that didn't need to apply
        if patches_skipped:
            print(f"\n  {C.DIM}{'─' * 50}{C.RESET}")
            print(f"  {C.CYAN}Auto-archiving {len(patches_skipped)} completed patch(es):{C.RESET}")
            
            for p in patches_skipped:
                if archive_patch_file(p):
                    print(f"    {C.DIM}→ Archived: {p}{C.RESET}")
            
            print(f"  {C.DIM}(These patches are already applied to source){C.RESET}")
    
    # If running standalone, offer patch management
    if standalone:
        print(f"\n  {C.DIM}{'─' * 50}{C.RESET}")
        manage = input(f"\n  Open patch manager? [y/N]: ").strip().lower()
        if manage == 'y':
            patch_manager_menu()
    
    return 0

if __name__ == "__main__":
    try:
        code = main()
        if len(sys.argv) == 1:
            print()
    except KeyboardInterrupt:
        sys.exit(1)