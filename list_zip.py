import zipfile
import sys

try:
    with zipfile.ZipFile(sys.argv[1], 'r') as z:
        print(f"Contents of {sys.argv[1]}:")
        for info in z.infolist():
            print(f"{info.filename} ({info.file_size} bytes)")
except Exception as e:
    print(f"Error: {e}")
