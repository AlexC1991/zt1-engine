import zipfile
import sys

def list_zip(path):
    try:
        with zipfile.ZipFile(path, 'r') as z:
            dirs = set()
            for name in z.namelist():
                parts = name.split('/')
                if len(parts) > 1:
                    dirs.add(parts[0] + "/" + parts[1])
            
            for d in sorted(dirs):
                print(d)
    except Exception as e:
        print(e)

if __name__ == "__main__":
    if len(sys.argv) > 1:
        list_zip(sys.argv[1])
    else:
        list_zip("build/Release/terrain.ztd")
