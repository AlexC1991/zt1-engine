import zipfile
import sys
import glob
import os

def find_file(directory, target_file):
    ztds = glob.glob(os.path.join(directory, "*.ztd"))
    print(f"Scanning {len(ztds)} ZTD files for '{target_file}'...")
    
    found = False
    for ztd_path in ztds:
        try:
            with zipfile.ZipFile(ztd_path, 'r') as z:
                # Case insensitive search?
                # ZT1 paths uses forward slashes.
                # Lowercase match
                target_lower = target_file.lower()
                for name in z.namelist():
                    if name.lower() == target_lower:
                        print(f"FOUND: {target_file} in {ztd_path}")
                        found = True
                    # Also partial match?
                    elif target_file.lower() in name.lower() and not found:
                         # just debug
                         pass
        except:
            pass
            
    if not found:
        print("Not found.")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        find_file(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python find_ztd_file.py <dir> <filename>")
