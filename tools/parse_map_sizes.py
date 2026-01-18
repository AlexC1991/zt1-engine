#!/usr/bin/env python3
"""
Zoo Tycoon 1 - Parse freeform.cfg to extract map sizes
The config file has comments like ";SMALL MAPS" that indicate map sizes.
"""

import zipfile
import os

def parse_freeform_cfg():
    """Parse freeform.cfg and build a map of path -> size."""
    cfg_ztds = ['config.ztd', 'freeform.ztd', 'zoo.ztd']
    
    cfg_content = None
    for ztd in cfg_ztds:
        if not os.path.exists(ztd):
            continue
        try:
            with zipfile.ZipFile(ztd, 'r') as z:
                for f in z.namelist():
                    if f.lower() == 'freeform.cfg':
                        cfg_content = z.read(f).decode('latin-1', errors='ignore')
                        print(f"Found freeform.cfg in {ztd}")
                        break
        except:
            pass
        if cfg_content:
            break
    
    if not cfg_content:
        print("ERROR: Could not find freeform.cfg")
        return {}
    
    # Parse the config
    map_sizes = {}
    current_size = "Medium"  # Default
    
    for line in cfg_content.split('\n'):
        line = line.strip()
        
        # Check for size category comments
        if line.startswith(';'):
            comment = line[1:].strip().upper()
            if 'SMALL' in comment:
                current_size = "Small"
                print(f"  Found category: SMALL")
            elif 'MEDIUM' in comment:
                current_size = "Medium"
                print(f"  Found category: MEDIUM")
            elif 'LARGE' in comment:
                current_size = "Large"
                print(f"  Found category: LARGE")
            continue
        
        # Parse freeform= lines
        if line.lower().startswith('freeform='):
            path = line.split('=', 1)[1].strip()
            if path:
                map_sizes[path.lower()] = current_size
    
    return map_sizes

def load_map_names():
    """Load map names from .txt files in freeform.ztd."""
    names = {}
    
    if not os.path.exists('freeform.ztd'):
        return names
    
    with zipfile.ZipFile('freeform.ztd', 'r') as z:
        for f in z.namelist():
            if f.endswith('.txt') and 'freeform/' in f:
                try:
                    data = z.read(f).decode('latin-1', errors='ignore')
                    # Get the first line as the name
                    name = data.strip().split('\n')[0].strip()
                    # Convert path to match .scn path
                    scn_path = f.replace('.txt', '.scn').lower()
                    names[scn_path] = name
                except:
                    pass
    
    return names

def main():
    print("Parsing freeform.cfg for map sizes...\n")
    map_sizes = parse_freeform_cfg()
    
    print(f"\nLoading map names from .txt files...\n")
    map_names = load_map_names()
    
    print(f"\n{'='*70}")
    print(f"{'Map Path':<40} {'Name':<25} {'Size':<10}")
    print(f"{'='*70}")
    
    for path, size in sorted(map_sizes.items()):
        name = map_names.get(path, os.path.basename(path).replace('.scn', ''))
        # Truncate long names
        if len(name) > 24:
            name = name[:21] + "..."
        print(f"{path:<40} {name:<25} {size:<10}")
    
    print(f"{'='*70}")
    
    # Summary
    sizes = {}
    for size in map_sizes.values():
        sizes[size] = sizes.get(size, 0) + 1
    
    print(f"\nSummary:")
    for size, count in sorted(sizes.items()):
        print(f"  {size}: {count} maps")
    
    # Generate C++ code snippet
    print("\n\n// C++ map size lookup table:")
    print("static std::map<std::string, std::string> mapSizeCategories = {")
    for path, size in sorted(map_sizes.items()):
        print(f'    {{"{path}", "{size}"}},')
    print("};")

if __name__ == "__main__":
    main()
