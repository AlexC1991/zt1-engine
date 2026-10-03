#!/usr/bin/env python3
"""
List contents of a .ztd archive file
ZTD format is basically a simple archive with a directory header
"""
import sys
import struct

def list_ztd_contents(filepath):
    """List all files in a ZTD archive"""
    with open(filepath, 'rb') as f:
        data = f.read()

    print(f"=== {filepath} ===")
    print(f"File size: {len(data):,} bytes\n")

    # ZTD header starts with file count (4 bytes)
    if len(data) < 4:
        print("ERROR: File too small")
        return

    file_count = struct.unpack('<I', data[0:4])[0]
    print(f"File count: {file_count}\n")

    if file_count == 0 or file_count > 10000:
        print(f"WARNING: Suspicious file count ({file_count})")
        print("Trying alternate header format...\n")

        # Try reading as directory entries
        # Format might be: [offset][size][name_len][name]
        offset = 4
        entries = []

        for i in range(min(100, file_count)):  # Limit to prevent runaway
            if offset + 8 > len(data):
                break

            file_offset = struct.unpack('<I', data[offset:offset+4])[0]
            file_size = struct.unpack('<I', data[offset+4:offset+8])[0]
            offset += 8

            if file_size == 0 or file_size > len(data):
                break

            # Read filename (null-terminated or length-prefixed)
            name_start = offset
            name_end = data.find(b'\x00', name_start)
            if name_end == -1 or name_end > name_start + 256:
                # Try length-prefixed
                if offset < len(data):
                    name_len = data[offset]
                    if name_len > 0 and name_len < 128:
                        offset += 1
                        filename = data[offset:offset+name_len].decode('ascii', errors='ignore')
                        offset += name_len
                    else:
                        filename = f"<unknown_{i}>"
                        offset += 1
            else:
                filename = data[name_start:name_end].decode('ascii', errors='ignore')
                offset = name_end + 1

            entries.append((file_offset, file_size, filename))

        if entries:
            print(f"Found {len(entries)} file entries:\n")
            print(f"{'#':>4}  {'Offset':>10}  {'Size':>10}  {'Filename'}")
            print("-" * 80)
            for i, (off, size, name) in enumerate(entries):
                print(f"{i:4d}  0x{off:08X}  {size:10,}  {name}")
            return

    # Try simpler format: just list of filenames followed by data
    print("Attempting to scan for .ani file signatures...\n")

    # .ANI files typically start with specific headers
    ani_positions = []
    pos = 0
    while pos < len(data) - 100:
        # Look for patterns that might indicate file starts
        # Check for reasonable frame counts (1-1000) at various offsets
        if pos + 20 < len(data):
            # Check if this could be an ANI header
            val1 = struct.unpack('<I', data[pos:pos+4])[0]
            val2 = struct.unpack('<I', data[pos+4:pos+8])[0]

            # Heuristic: ANI files have frame count < 1000 and reasonable dimensions
            if 1 <= val1 <= 1000 and 1 <= val2 <= 500:
                ani_positions.append(pos)
                pos += 4  # Small skip to avoid overlapping matches
            else:
                pos += 1
        else:
            break

    if ani_positions:
        print(f"Found {len(ani_positions)} possible .ani file positions:")
        for i, pos in enumerate(ani_positions[:50]):  # Show first 50
            print(f"  Position 0x{pos:08X}")
    else:
        print("No .ani file signatures found")
        print("\nShowing first 512 bytes as hex:")
        for i in range(0, min(512, len(data)), 16):
            hex_str = ' '.join(f'{b:02X}' for b in data[i:i+16])
            ascii_str = ''.join(chr(b) if 32 <= b < 127 else '.' for b in data[i:i+16])
            print(f"  {i:04X}: {hex_str:48s}  {ascii_str}")

if __name__ == '__main__':
    if len(sys.argv) < 2:
        print("Usage: python list_ztd_contents.py <ztd_file>")
        sys.exit(1)

    list_ztd_contents(sys.argv[1])
