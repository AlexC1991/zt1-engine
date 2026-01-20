import sys
import math

def calculate_entropy(data):
    if not data:
        return 0
    entropy = 0
    for x in range(256):
        p_x = float(data.count(x))/len(data)
        if p_x > 0:
            entropy += - p_x*math.log(p_x, 2)
    return entropy

def main():
    if len(sys.argv) < 2:
        print("Usage: python analyze_zoo.py <file.zoo>")
        return

    filepath = sys.argv[1]
    with open(filepath, 'rb') as f:
        content = f.read()

    print(f"File: {filepath}")
    print(f"Size: {len(content)} bytes")
    
    # Analyze header
    print(f"Header: {content[:4].decode('ascii', errors='ignore')}")
    
    # Analyze entropy of 1KB chunks
    print("\n--- Entropy Analysis (High > 7.0 = Compressed/Random) ---")
    chunk_size = 1024
    for i in range(0, len(content), chunk_size):
        chunk = content[i:i+chunk_size]
        ent = calculate_entropy(chunk)
        print(f"Offset {i:6d} (0x{i:4X}): Entropy = {ent:.2f}")

    # Look for repeating patterns (simple RLE check)
    print("\n--- Pattern Search ---")
    # Take a sample from the middle
    mid = len(content) // 2
    sample = content[mid:mid+64]
    print(f"Sample at {mid}: {sample.hex()}")

if __name__ == "__main__":
    main()
