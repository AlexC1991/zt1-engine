import zipfile
import sys

try:
    with zipfile.ZipFile(sys.argv[1], 'r') as z:
        print(f"Contents of {sys.argv[1]}:")
        for f in z.namelist():
            if f.endswith('.ani'):
                print(f)
except Exception as e:
    print(f"Error: {e}")
