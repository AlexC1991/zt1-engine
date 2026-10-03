import zipfile
import sys

def cat_ztd(ztd_path, file_path):
    try:
        with zipfile.ZipFile(ztd_path, 'r') as z:
            with z.open(file_path) as f:
                content = f.read()
                try:
                    print(content.decode('utf-8'))
                except:
                    print(content.decode('latin-1'))
    except Exception as e:
        print(f"Error: {e}")

if __name__ == "__main__":
    if len(sys.argv) > 2:
        cat_ztd(sys.argv[1], sys.argv[2])
    else:
        print("Usage: python cat_ztd.py <ztd_path> <file_path>")
