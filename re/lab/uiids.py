# The game's UI element numbers, by name: every [section] with an id in every
# layout (.lyt) in the game's archives - the base game's, then the
# expansions', then the updates' over them, as the game reads them. zoo.exe
# asks for elements by number (uiFind(0x11cb)); this says which one that is
# ("infocom:ItemCost_Cost", Building Information's price).
#
#   python re/lab/uiids.py [--game <Zoo Tycoon folder>] [--out re/private/ui_ids.json]
#   python re/lab/uiids.py 0x11cb 7000 1071      (look numbers up)
import argparse
import glob
import json
import os
import re
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
OUT = os.path.join(ROOT, "re", "private", "ui_ids.json")
DEFAULT_DIRS = [r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon", r"C:\Program Files\Microsoft Games\Zoo Tycoon"]


def game_dir(given):
    if given:
        return given
    sys.path.insert(0, os.path.join(ROOT, "engine-build-resources"))
    try:
        import zt1_paths  # noqa: E402

        d = zt1_paths.load_game_dir()
        if d:
            return d
    except Exception:  # noqa: BLE001 (fall back to the usual places)
        pass
    for d in DEFAULT_DIRS:
        if os.path.isdir(d):
            return d
    return None


def build(gdir):
    ids = {}
    archives = sorted(glob.glob(os.path.join(gdir, "*.ztd")))
    archives += sorted(glob.glob(os.path.join(gdir, "xpack*", "*.ztd")))
    archives += sorted(glob.glob(os.path.join(gdir, "updates", "*.ztd")))
    for a in archives:
        try:
            zf = zipfile.ZipFile(a)
        except zipfile.BadZipFile:
            continue
        for name in zf.namelist():
            if not name.lower().endswith(".lyt"):
                continue
            layout = name.split("/")[-1][:-4]
            section = None
            for line in zf.read(name).decode("latin1").splitlines():
                line = line.strip()
                m = re.match(r"^\[(.+)\]$", line)
                if m:
                    section = m.group(1)
                    continue
                m = re.match(r"^id\s*=\s*(\d+)", line, re.I)
                if m and section:
                    ids[int(m.group(1))] = "%s:%s" % (layout, section)
    return ids


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("numbers", nargs="*")
    ap.add_argument("--game")
    ap.add_argument("--out", default=OUT)
    a = ap.parse_args()
    if a.numbers and os.path.exists(a.out):
        with open(a.out, encoding="utf-8") as fh:
            ids = {int(k): v for k, v in json.load(fh).items()}
    else:
        gdir = game_dir(a.game)
        if not gdir:
            sys.exit("Where's the game? Pass --game <Zoo Tycoon folder>.")
        ids = build(gdir)
        os.makedirs(os.path.dirname(a.out), exist_ok=True)
        with open(a.out, "w", encoding="utf-8") as fh:
            json.dump({str(k): v for k, v in sorted(ids.items())}, fh, indent=0)
        print("%d UI elements -> %s" % (len(ids), a.out))
    for n in a.numbers:
        v = int(n, 0)
        print("%s (%d): %s" % (hex(v), v, ids.get(v, "not in any layout")))


if __name__ == "__main__":
    main()
