import sys
import struct

def main():
    if len(sys.argv) < 3:
        print("Usage: python analyze_grid.py <file.zoo> <width>")
        return

    filepath = sys.argv[1]
    width = int(sys.argv[2])
    
    # Hypothesis: 10 bytes per tile
    TILE_SIZE = 10
    row_bytes = width * TILE_SIZE
    
    print(f"Scanning {filepath}")
    print(f"Assuming {width}xN map, {TILE_SIZE} bytes/tile -> Row Stride: {row_bytes} bytes")
    
    with open(filepath, 'rb') as f:
        content = f.read()
        
    # Moving average or score of "zeroness"
    # We look for a start offset S such that content[S : S + row_bytes] looks like a row
    # and content[S + row_bytes : ...] looks like the next row.
    
    best_start = -1
    max_score = -1
    
    # Only scan first 100kb
    scan_limit = min(len(content) - row_bytes * 10, 100000)
    
    for start in range(0, scan_limit, 1): # Scan every byte? Speed might be okay for 100KB
        score = 0
        matches = 0
        
        # Check 5 rows
        for r in range(5):
            offset = start + r * row_bytes
            row = content[offset : offset + row_bytes]
            
            # Simple heuristic: Valid row has lots of zeros?
            # Or valid row has structured zeros?
            # Let's count zeros.
            zeros = row.count(0)
            if zeros > row_bytes * 0.5: # More than 50% empty
                matches += 1
                score += zeros
        
        if matches >= 3 and score > max_score:
            max_score = score
            best_start = start
            
    print(f"Best Candidate Start Offset: {best_start} (0x{best_start:X})")
    
    if best_start != -1:
        print(f"Inspecting tiles at offset {best_start} (Stride {row_bytes})")
        # Print first 20 non-zero tiles
        count = 0
        for i in range(width * 20): # Scan first 20 rows
            offset = best_start + i * TILE_SIZE
            if offset + TILE_SIZE > len(content): break
            
            tile_bytes = content[offset : offset + TILE_SIZE]
            # If tile is not all zeros
            if any(b != 0 for b in tile_bytes):
                print(f"Tile {i} (Row {i//width}, Col {i%width}): {tile_bytes.hex()}")
                count += 1
                if count > 20: break

if __name__ == "__main__":
    main()
