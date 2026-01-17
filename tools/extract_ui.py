
import zipfile
import os

ztd_path = "build/Release/ui.ztd"
target_file = "ui/startup.lyt"
extract_path = "build/Release"

if not os.path.exists(ztd_path):
    print(f"Error: {ztd_path} not found.")
    exit(1)

try:
    with zipfile.ZipFile(ztd_path, 'r') as z:
        # Case insensitive search
        found = None
        for name in z.namelist():
            if name.lower().replace('\\', '/') == target_file.lower():
                found = name
                break
        
        if found:
            print(f"Found {found}. Extracting to {extract_path}...")
            z.extract(found, extract_path)
            print("Extraction complete.")
        else:
            print(f"Error: {target_file} not found in {ztd_path}")
            print("Listing first 10 files in ZTD:")
            for n in z.namelist()[:10]:
                print(f" - {n}")

except Exception as e:
    print(f"An error occurred: {e}")
