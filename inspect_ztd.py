import zipfile
import sys
import os

def inspect_ztd(ztd_path):
    print(f"Inspecting {ztd_path}")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            file_list = z.namelist()
            target = "freeform/ancient.txt"
            if target in file_list:
                print(f"Reading first file: {target}")
                with z.open(target) as f:
                    content = f.read().decode('utf-8', errors='ignore')
                    print("\n--- CONTENT START ---")
                    print(content[:2000]) # Print first 2000 chars
                    print("\n--- CONTENT END ---")
            else:
                print("No .scn files found.")
                # List first 10 files just to see what's in there
                print("Sample files:", file_list[:10])

    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    inspect_ztd("freeform.ztd")
