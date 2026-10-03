import zipfile
import sys

def list_ztd_detailed(ztd_path):
    print(f"Listing {ztd_path}...")
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            for info in z.infolist():
                mode = "DIR" if info.is_dir() else "FILE"
                print(f"{mode:4} {info.file_size:8} {info.filename}")
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 1:
        list_ztd_detailed(sys.argv[1])
    else:
        print("Usage: python list_ztd_detailed.py <ztd_path>")
