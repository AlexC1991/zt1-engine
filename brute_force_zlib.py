import zlib
import sys

def try_decompress(data):
    try:
        # Try waiting for -15 (raw deflate) or 15 (zlib)
        # ZT1 often uses standard ZLIB (checking for 78 9C or similar header)
        decompressed = zlib.decompress(data)
        return decompressed
    except zlib.error:
        try:
            # Try raw deflate (no header)
            decompressed = zlib.decompress(data, -15)
            return decompressed
        except zlib.error:
            return None

def main():
    if len(sys.argv) < 2:
        print("Usage: python brute_force_zlib.py <file.zoo>")
        return

    filepath = sys.argv[1]
    with open(filepath, 'rb') as f:
        content = f.read()

    print(f"Scanning {filepath} ({len(content)} bytes)...")

    # Scan ENTIRE file
    for i in range(len(content)):
        chunk = content[i:]
        
        # Method 1: Standard ZLIB
        try:
            dobj = zlib.decompressobj()
            res = dobj.decompress(chunk)
            if len(res) > 100:
                print(f"[OFFSET {i}] (0x{i:X}) ZLIB Stream -> {len(res)} bytes")
                if len(res) > 5000: print("   *** CANDIDATE ***")
        except:
            pass
            
        # Method 2: Raw Deflate
        try:
            dobj = zlib.decompressobj(-15)
            res = dobj.decompress(chunk)
            if len(res) > 100:
                 # Raw deflate creates many false positives on random data
                 # Only print if it's structurally significant or huge
                 if len(res) > 1000:
                    print(f"[OFFSET {i}] (0x{i:X}) Raw Deflate -> {len(res)} bytes")
                    if len(res) > 5000: print("   *** CANDIDATE ***")
        except:
            pass
    
    print("Scan complete.")

if __name__ == "__main__":
    main()
