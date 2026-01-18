#!/usr/bin/env python3
"""
Debug script to check the mapselec.lyt layout and verify:
1. Spinner button animations exist
2. Starting Cash text element positions
3. What's causing the duplicate image
"""

import zipfile
import os

def check_animations():
    """Check if spinner animations exist in ui.ztd."""
    ztd_path = "ui.ztd"
    
    if not os.path.exists(ztd_path):
        print(f"ERROR: {ztd_path} not found")
        return
    
    with zipfile.ZipFile(ztd_path, 'r') as z:
        files = z.namelist()
        
        # Check for spinner animations
        print("=== Checking Spinner Animations ===")
        spinup = [f for f in files if 'spinup' in f.lower()]
        spindwn = [f for f in files if 'spindwn' in f.lower() or 'spindn' in f.lower()]
        
        print(f"\nSpinUp files found: {len(spinup)}")
        for f in spinup[:10]:
            print(f"  {f}")
        
        print(f"\nSpinDown files found: {len(spindwn)}")
        for f in spindwn[:10]:
            print(f"  {f}")
            
        # Check shared ui folder
        print("\n=== SharedUI Folder Contents ===")
        sharedui = [f for f in files if 'sharedui' in f.lower()]
        for f in sorted(sharedui)[:20]:
            print(f"  {f}")
        if len(sharedui) > 20:
            print(f"  ... and {len(sharedui) - 20} more")

def analyze_layout():
    """Parse mapselec.lyt to understand element IDs and positions."""
    lyt_path = "ui/mapselec.lyt"
    
    if os.path.exists(lyt_path):
        with open(lyt_path, 'r') as f:
            content = f.read()
    else:
        print(f"ERROR: {lyt_path} not found")
        return
    
    print("\n=== Layout Elements at y > 500 (bottom area) ===")
    
    current_section = None
    current_y = None
    current_type = None
    current_id = None
    
    for line in content.split('\n'):
        line = line.strip()
        
        if line.startswith('[') and line.endswith(']'):
            if current_section and current_y and int(current_y) > 500:
                print(f"  [{current_section}] type={current_type} id={current_id} y={current_y}")
            current_section = line[1:-1]
            current_y = None
            current_type = None
            current_id = None
            continue
        
        if '=' in line:
            key, val = line.split('=', 1)
            key = key.strip().lower()
            val = val.strip()
            
            if key == 'type':
                current_type = val
            elif key == 'id':
                current_id = val
            elif key == 'y':
                current_y = val
    
    # Print last section
    if current_section and current_y and int(current_y) > 500:
        print(f"  [{current_section}] type={current_type} id={current_id} y={current_y}")

def main():
    print("Zoo Tycoon Freeform Menu Debug Script\n")
    check_animations()
    analyze_layout()

if __name__ == "__main__":
    main()
