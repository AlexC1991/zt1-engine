#!/usr/bin/env python3
"""
Zoo Tycoon 1 - Map Size Analyzer
Explores .scn files to understand map dimensions and categorization.
"""

import zipfile
import os
import re
import struct

def analyze_scn_file(ztd_path, scn_path):
    """Read a .scn file from ZTD and extract map info."""
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            # Try to find the file (case-insensitive)
            actual_name = None
            for name in z.namelist():
                if name.lower() == scn_path.lower():
                    actual_name = name
                    break
            
            if not actual_name:
                print(f"  File not found: {scn_path}")
                return None
            
            data = z.read(actual_name).decode('latin-1', errors='ignore')
            
            # Parse as INI-style
            info = {}
            current_section = None
            
            for line in data.split('\n'):
                line = line.strip()
                if not line or line.startswith(';'):
                    continue
                
                # Section header
                if line.startswith('[') and line.endswith(']'):
                    current_section = line[1:-1].lower()
                    continue
                
                # Key=value
                if '=' in line and current_section:
                    key, _, value = line.partition('=')
                    key = key.strip().lower()
                    value = value.strip()
                    info[f"{current_section}.{key}"] = value
            
            return info
    except Exception as e:
        print(f"  Error reading {scn_path}: {e}")
        return None

def get_map_size_category(width, height):
    """Categorize map based on dimensions."""
    # Original game categories (approximate):
    # Small: up to ~40x40
    # Medium: ~50x50 to ~70x70  
    # Large: 80x80+
    
    max_dim = max(width, height)
    
    if max_dim <= 45:
        return "Small"
    elif max_dim <= 75:
        return "Medium"
    else:
        return "Large"

def main():
    # Look for freeform.ztd in build/Release
    ztd_path = "freeform.ztd"
    
    if not os.path.exists(ztd_path):
        print(f"Error: {ztd_path} not found. Run from build/Release directory.")
        return
    
    print(f"Analyzing maps in {ztd_path}...\n")
    
    # List all .scn files
    with zipfile.ZipFile(ztd_path, 'r') as z:
        scn_files = [f for f in z.namelist() if f.lower().endswith('.scn')]
    
    print(f"Found {len(scn_files)} .scn files\n")
    print("-" * 70)
    
    results = []
    
    for scn_file in sorted(scn_files):
        info = analyze_scn_file(ztd_path, scn_file)
        if not info:
            continue
        
        # Try to get dimensions
        width = info.get('map.width', info.get('world.width', '?'))
        height = info.get('map.height', info.get('world.height', '?'))
        
        # Also check for size directly
        size_direct = info.get('map.size', '')
        
        # Get map name if available
        name = os.path.basename(scn_file).replace('.scn', '')
        
        try:
            w = int(width)
            h = int(height)
            category = get_map_size_category(w, h)
        except:
            w, h = '?', '?'
            category = "Unknown"
        
        results.append({
            'file': scn_file,
            'name': name,
            'width': w,
            'height': h,
            'category': category,
            'size_field': size_direct
        })
        
        print(f"{name:30} | {str(w):>4} x {str(h):<4} | {category:8} | size={size_direct}")
    
    print("-" * 70)
    print(f"\nTotal maps analyzed: {len(results)}")
    
    # Summary by category
    categories = {}
    for r in results:
        cat = r['category']
        categories[cat] = categories.get(cat, 0) + 1
    
    print("\nBy category:")
    for cat, count in sorted(categories.items()):
        print(f"  {cat}: {count}")
    
    # Show raw keys found (for debugging)
    print("\n\nSample of keys found in first .scn file:")
    if results:
        first_info = analyze_scn_file(ztd_path, scn_files[0])
        if first_info:
            for key in sorted(first_info.keys())[:20]:
                print(f"  {key} = {first_info[key][:50] if len(first_info[key]) > 50 else first_info[key]}")

if __name__ == "__main__":
    main()
