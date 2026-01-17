import argparse
import binascii
import struct
import zipfile


def find_entry_case_insensitive(z: zipfile.ZipFile, wanted: str) -> str | None:
    wanted_low = wanted.lower().replace("\\", "/")
    for name in z.namelist():
        if name.lower().replace("\\", "/") == wanted_low:
            return name
    return None


def hexdump(data: bytes, n: int = 512) -> str:
    return binascii.hexlify(data[:n]).decode("ascii")


def parse_main_header(data: bytes):
    """
    Matches your sprite viewer:
      @0: height_header u32 (unused here)
      @4: str_len u32
      @8: palette string (str_len bytes)
      @8+str_len: base_width u32
      @8+str_len+4: frame headers start
    """
    if len(data) < 20:
        return None

    str_len = struct.unpack_from("<I", data, 4)[0]
    if str_len == 0 or str_len > 200:
        return None

    width_pos = 8 + str_len
    if width_pos + 4 > len(data):
        return None

    base_width = struct.unpack_from("<I", data, width_pos)[0]
    if base_width == 0 or base_width > 5000:
        return None

    frame_header_start = width_pos + 4
    return frame_header_start, base_width, str_len


def find_frame_headers_viewer_style(data: bytes, max_frames: int = 10):
    header_info = parse_main_header(data)
    if not header_info:
        return []

    frame_start, base_width, str_len = header_info

    frames = []
    pos = frame_start

    while pos + 14 <= len(data) and len(frames) < max_frames:
        rle_size = struct.unpack_from("<I", data, pos)[0]
        h = struct.unpack_from("<H", data, pos + 4)[0]
        w = struct.unpack_from("<H", data, pos + 6)[0]
        x_off = struct.unpack_from("<H", data, pos + 8)[0]
        y_off = struct.unpack_from("<H", data, pos + 10)[0]
        flags = struct.unpack_from("<H", data, pos + 12)[0]

        # Lenient-ish validation (tune as needed)
        if not (16 <= rle_size <= len(data)):
            break
        if not (1 <= w <= 4096 and 1 <= h <= 4096):
            break
        if pos + 14 + rle_size > len(data):
            break

        frames.append(
            {
                "header_pos": pos,
                "rle_pos": pos + 14,
                "rle_size": rle_size,
                "width": w,
                "height": h,
                "x_off": x_off,
                "y_off": y_off,
                "flags": flags,
                "base_width": base_width,
                "str_len": str_len,
            }
        )

        # Try to locate the next header near end of current frame
        next_search_start = pos + 14 + rle_size - 30
        if next_search_start < pos + 14:
            next_search_start = pos + 14

        found = False
        for search in range(
            next_search_start, min(next_search_start + 200, len(data) - 14)
        ):
            rs = struct.unpack_from("<I", data, search)[0]
            hh = struct.unpack_from("<H", data, search + 4)[0]
            ww = struct.unpack_from("<H", data, search + 6)[0]
            if 16 <= rs <= len(data) and 1 <= ww <= 4096 and 1 <= hh <= 4096:
                if search + 14 + rs <= len(data):
                    pos = search
                    found = True
                    break

        if not found:
            break

    return frames


def scan_rle_hits(data: bytes, header_size: int = 14, min_rle: int = 16):
    n = len(data)
    hits = []
    for pos in range(0, n - 4):
        rle = struct.unpack_from("<I", data, pos)[0]
        if rle < min_rle or rle > n:
            continue
        if pos + header_size + rle <= n:
            hits.append((pos, rle))
    return hits


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--ztd", required=True, help="Path to .ztd")
    ap.add_argument(
        "--entry",
        required=True,
        help="Entry name inside ztd (case-insensitive), e.g. freeform/mars/n",
    )
    args = ap.parse_args()

    with zipfile.ZipFile(args.ztd, "r") as z:
        actual = find_entry_case_insensitive(z, args.entry)
        if not actual:
            print("ENTRY NOT FOUND. Try listing with:")
            print(
                r"  python -c "
                r"\"import zipfile; z=zipfile.ZipFile(r'build\Release\scenario.ztd'); "
                r"print('\n'.join(z.namelist()[:200]))\""
            )
            return

        data = z.read(actual)
        print("entry:", actual)
        print("len:", len(data))
        print("hex[0:512]:", hexdump(data, 512))

        hits = scan_rle_hits(data)
        print("rle_hits_total:", len(hits))
        print("rle_hits_first20:", hits[:20])
        print("rle_hits_last20:", hits[-20:])

        mh = parse_main_header(data)
        print("main_header:", mh)

        frames = find_frame_headers_viewer_style(data, max_frames=10)
        print("viewer_style_frames:", len(frames))
        for f in frames:
            print(
                "  header_pos={header_pos} rle_pos={rle_pos} rle_size={rle_size} "
                "w={width} h={height} x_off={x_off} y_off={y_off} flags={flags}".format(
                    **f
                )
            )


if __name__ == "__main__":
    main()