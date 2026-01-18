import zipfile
import binascii

ZTD_PATH = "build/Release/ui.ztd"
FILE_PATH = "ui/sharedui/spinup/N"

try:
    with zipfile.ZipFile(ZTD_PATH, 'r') as zf:
        with zf.open(FILE_PATH) as f:
            data = f.read(64)
            print(f"Hex: {binascii.hexlify(data)}")
            print(f"Text: {data}")
except Exception as e:
    print(f"Error: {e}")
