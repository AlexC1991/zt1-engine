#!/usr/bin/env python3
"""
Zoo Tycoon 1 - Map File Explorer
Explores what files exist for each freeform map.
"""

import zipfile
import os
import struct

def explore_ztd(ztd_path):
    """List all files in a ZTD and find map-related ones."""
    print(f"Exploring {ztd_path}...\n")
    
    with zipfile.ZipFile(ztd_path, 'r') as z:
        files = sorted(z.namelist())
        
        # Group by folder
        folders = {}
        for f in files:
            parts = f.split('/')
            if len(parts) > 1:
                folder = parts[0]
            else:
                folder = '(root)'
            
            if folder not in folders:
                folders[folder] = []
            folders[folder].append(f)
        
        for folder, file_list in sorted(folders.items()):
            print(f"\n{folder}/ ({len(file_list)} files)")
            # Show first few files
            for f in file_list[:5]:
                info = z.getinfo(f)
                print(f"  {os.path.basename(f):30} {info.file_size:>10} bytes")
            if len(file_list) > 5:
                print(f"  ... and {len(file_list) - 5} more")

def analyze_map_binary(ztd_path, map_folder):
    """Try to read binary map data to find dimensions."""
    print(f"\nAnalyzing binary data for: {map_folder}")
    
    with zipfile.ZipFile(ztd_path, 'r') as z:
        # Look for common map file names
        possible_names = [
            f"{map_folder}/map.zzz",
            f"{map_folder}/data.zzz", 
            f"{map_folder}/{map_folder}.zzz",
            f"{map_folder}/terrain.dat",
            f"{map_folder}/map"
        ]
        
        found_files = [f for f in z.namelist() if f.startswith(map_folder + '/')]
        print(f"  Files in folder: {found_files}")
        
        # Check each file for dimension-like data in header
        for f in found_files:
            try:
                data = z.read(f)
                if len(data) < 8:
                    continue
                
                # Try reading as little-endian integers at start
                if len(data) >= 8:
                    w1, h1 = struct.unpack('<II', data[:8])
                    w2, h2 = struct.unpack('<HH', data[:4])
                    
                    # Check if these look like reasonable dimensions
                    if 10 < w1 < 200 and 10 < h1 < 200:
                        print(f"  {f}: Possible dims (32-bit): {w1}x{h1}")
                    if 10 < w2 < 200 and 10 < h2 < 200:
                        print(f"  {f}: Possible dims (16-bit): {w2}x{h2}")
                        
            except Exception as e:
                pass

def check_freeform_cfg():
    """Check freeform.cfg for any size hints."""
    cfg_ztds = ['freeform.ztd', 'zoo.ztd', 'config.ztd']
    
    for ztd in cfg_ztds:
        if not os.path.exists(ztd):
            continue
            
        try:
            with zipfile.ZipFile(ztd, 'r') as z:
                for f in z.namelist():
                    if 'freeform' in f.lower() and f.endswith('.cfg'):
                        print(f"\n=== {ztd}/{f} ===")
                        data = z.read(f).decode('latin-1', errors='ignore')
                        print(data[:2000])
                        return
        except:
            pass

def main():
    # Check config first
    print("Looking for freeform.cfg...")
    check_freeform_cfg()
    
    # Then explore the ZTD
    if os.path.exists('freeform.ztd'):
        explore_ztd('freeform.ztd')
        
        # Analyze a specific map
        print("\n" + "="*60)
        analyze_map_binary('freeform.ztd', 'freeform/ff01')
        analyze_map_binary('freeform.ztd', 'freeform/beach')

if __name__ == "__main__":
    main()
