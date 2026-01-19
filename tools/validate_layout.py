#!/usr/bin/env python3
"""
Layout Validation Script
Parses .lyt files and verifies all referenced assets exist.
"""

import os
import sys
import configparser
from pathlib import Path


def parse_layout(lyt_path):
    """Parse a .lyt file and extract all asset references."""
    assets = []
    
    if not os.path.exists(lyt_path):
        print(f"ERROR: Layout file not found: {lyt_path}")
        return assets
    
    # Read the file
    with open(lyt_path, 'r', encoding='utf-8', errors='ignore') as f:
        content = f.read()
    
    # Use configparser to parse INI-style format
    config = configparser.ConfigParser(strict=False)
    config.read_string(content)
    
    for section in config.sections():
        # Check for 'normal' key (image path)
        if config.has_option(section, 'normal'):
            path = config.get(section, 'normal').strip()
            if path:
                assets.append({
                    'section': section,
                    'key': 'normal',
                    'path': path,
                    'type': config.get(section, 'type', fallback='unknown')
                })
        
        # Check for 'animation' key
        if config.has_option(section, 'animation'):
            path = config.get(section, 'animation').strip()
            if path:
                assets.append({
                    'section': section,
                    'key': 'animation',
                    'path': path,
                    'type': config.get(section, 'type', fallback='unknown')
                })
    
    return assets


def check_asset_exists(base_dir, asset_path):
    """Check if an asset exists in the build directory."""
    # Try the exact path
    full_path = os.path.join(base_dir, asset_path)
    if os.path.exists(full_path):
        return True, full_path
    
    # Try with different extensions
    base, ext = os.path.splitext(full_path)
    for alt_ext in ['.tga', '.png', '.bmp', '.ani']:
        alt_path = base + alt_ext
        if os.path.exists(alt_path):
            return True, alt_path
    
    # Try as directory (for animation folders)
    if os.path.isdir(full_path):
        return True, full_path
    
    return False, None


def validate_layout(lyt_path, build_dir):
    """Validate a layout file and check all assets."""
    print(f"\n{'='*60}")
    print(f"Validating: {os.path.basename(lyt_path)}")
    print(f"{'='*60}")
    
    # Check layout file exists
    if not os.path.exists(lyt_path):
        print(f"  ERROR: Layout file not found!")
        return False
    
    # Get file size
    size = os.path.getsize(lyt_path)
    print(f"  File size: {size} bytes")
    
    # Parse layout
    assets = parse_layout(lyt_path)
    print(f"  Found {len(assets)} asset references")
    
    # Check each asset
    all_ok = True
    for asset in assets:
        exists, found_path = check_asset_exists(build_dir, asset['path'])
        status = "OK" if exists else "MISSING"
        symbol = "[OK]" if exists else "[X]"
        
        print(f"  {symbol} [{asset['section']}] {asset['path']}")
        if not exists:
            all_ok = False
            print(f"       -> Asset not found in build directory!")
        else:
            # Get file size
            if os.path.isfile(found_path):
                asset_size = os.path.getsize(found_path)
                print(f"       -> Found: {found_path} ({asset_size:,} bytes)")
    
    return all_ok


def compare_layouts(lyt1_path, lyt2_path):
    """Compare two layout files to find differences."""
    print(f"\n{'='*60}")
    print(f"Comparing layouts:")
    print(f"  Layout 1: {os.path.basename(lyt1_path)}")
    print(f"  Layout 2: {os.path.basename(lyt2_path)}")
    print(f"{'='*60}")
    
    # Read both files
    with open(lyt1_path, 'r', encoding='utf-8', errors='ignore') as f:
        lines1 = f.readlines()
    with open(lyt2_path, 'r', encoding='utf-8', errors='ignore') as f:
        lines2 = f.readlines()
    
    # Parse both
    config1 = configparser.ConfigParser(strict=False)
    config1.read(lyt1_path)
    config2 = configparser.ConfigParser(strict=False)
    config2.read(lyt2_path)
    
    # Compare LayoutInfo sections
    print("\n[LayoutInfo] comparison:")
    for key in ['x', 'y', 'dx', 'dy', 'id', 'layer']:
        val1 = config1.get('LayoutInfo', key, fallback='N/A')
        val2 = config2.get('LayoutInfo', key, fallback='N/A')
        match = "=" if val1 == val2 else "!="
        print(f"  {key}: {val1} {match} {val2}")
    
    # Compare Background sections
    print("\n[Background] comparison:")
    for key in ['type', 'id', 'x', 'y', 'dx', 'dy', 'normal', 'layer', 'transparent']:
        val1 = config1.get('Background', key, fallback='N/A')
        val2 = config2.get('Background', key, fallback='N/A')
        match = "=" if val1 == val2 else "!="
        print(f"  {key}: {val1} {match} {val2}")


def main():
    # Determine paths
    script_dir = Path(__file__).parent
    project_root = script_dir.parent
    build_dir = project_root / "build" / "Release"
    pc_sync_dir = project_root / "pc-sync"
    
    print("Zoo Tycoon Layout Validator")
    print(f"Build directory: {build_dir}")
    print(f"PC-Sync directory: {pc_sync_dir}")
    
    # Layouts to check
    layouts = [
        ("Startup (Main Menu)", build_dir / "ui" / "startup.lyt"),
        ("Scenario Selection", build_dir / "ui" / "scenario.lyt"),
        ("Freeform Map Selection", build_dir / "ui" / "mapselec.lyt"),
    ]
    
    # Validate each layout
    results = {}
    for name, path in layouts:
        results[name] = validate_layout(str(path), str(build_dir))
    
    # Compare scenario vs mapselec (since mapselec works but scenario doesn't)
    scenario_path = build_dir / "ui" / "scenario.lyt"
    mapselec_path = build_dir / "ui" / "mapselec.lyt"
    if scenario_path.exists() and mapselec_path.exists():
        compare_layouts(str(scenario_path), str(mapselec_path))
    
    # Summary
    print(f"\n{'='*60}")
    print("SUMMARY")
    print(f"{'='*60}")
    for name, ok in results.items():
        status = "PASS" if ok else "FAIL"
        symbol = "[OK]" if ok else "[X]"
        print(f"  {symbol} {name}: {status}")


if __name__ == "__main__":
    main()
