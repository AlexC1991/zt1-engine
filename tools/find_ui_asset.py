import os
import zipfile

PROJECT_ROOT = "c:\\Users\\batty\\OneDrive\\Documents\\GitHub\\zt1-engine"
RELEASE_DIR = os.path.join(PROJECT_ROOT, "build", "Release")

def search_ztd(ztd_path, query):
    if not os.path.exists(ztd_path):
        return
    
    try:
        with zipfile.ZipFile(ztd_path, 'r') as zf:
            for info in zf.infolist():
                if query.lower() in info.filename.lower():
                    print(f"[{os.path.basename(ztd_path)}] Found: {info.filename} ({info.file_size} bytes)")
    except zipfile.BadZipFile:
        print(f"Skipping {os.path.basename(ztd_path)}: Bad Zip File")

def main():
    print(f"Searching for 'spin' in ZTD files in {RELEASE_DIR}...")
    
    for filename in os.listdir(RELEASE_DIR):
        if filename.lower().endswith(".ztd"):
            search_ztd(os.path.join(RELEASE_DIR, filename), "spin")

if __name__ == "__main__":
    main()
