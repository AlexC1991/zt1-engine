"""Drive the original Zoo Tycoon through every freeform map and screenshot it.

Starts from the Freeform map-select screen. For each map: select, Play,
wait for the HUD, capture the default-zoom start view, zoom out fully,
capture the start view and a 3x3 grid of minimap-click views, then return
to the map list via Options > Main Menu > No > Play Freeform Game.
"""
import os
import subprocess
import sys
import time

from PIL import Image

SP = os.path.dirname(os.path.abspath(__file__))
OUT = os.path.join(SP, "orig")

# Map-select rows (0-based) -> .zoo stem, in the original's list order
MAPS = [
    "dinosmal", "dinomed", "lavaland", "airport", "dinolrg",
    "sm_aqua", "sm_rlake", "sm_cclif", "med_kids", "med_aqua", "med_crat",
    "med_isle", "med_bch", "lg_lake", "lg_sandy", "lg_sub", "lg_resrt",
    "default", "deathmtn", "fshore", "lagoon", "svalley", "mythgard",
    "beach", "svolcano", "tundra", "arcmaze", "smcages", "under",
    "medium", "ancient", "highland", "crater", "rockdes", "jungriv",
    "lunar", "mars", "nile", "large", "locean", "lvalley", "cratlake",
    "dryriver", "dunesea", "nile2",
]
VISIBLE_ROWS = 29

# Minimap diamond corners in game coords (default view rotation)
MM_TOP, MM_RIGHT, MM_LEFT = (76, 521), (142, 555), (10, 555)


# One persistent PowerShell process does the window/input/capture work
_server = None


def cmd(line):
    global _server
    if _server is None:
        _server = subprocess.Popen(
            ["powershell", "-NoProfile", "-ExecutionPolicy", "Bypass", "-File",
             os.path.join(SP, "zt_server.ps1")],
            stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True, bufsize=1)
        assert _server.stdout.readline().strip() == "ready"
    _server.stdin.write(line + "\n")
    _server.stdin.flush()
    reply = _server.stdout.readline().strip()
    if not reply.startswith("ok"):
        raise RuntimeError(f"{line}: {reply}")
    return reply


def move(x, y):
    cmd(f"move {int(x)} {int(y)}")


def click(x, y, wait=0.4):
    cmd(f"click {int(x)} {int(y)}")
    time.sleep(wait)


def shot(name):
    path = os.path.join(OUT, name)
    cmd(f"shot {path}")
    return path


def hud_visible():
    """Two fixed pixels on the in-game HUD frame (differ on every menu)."""
    path = shot("_probe.png")
    im = Image.open(path).convert("RGB")
    def near(p, want):
        return all(abs(a - b) <= 6 for a, b in zip(im.getpixel(p), want))
    return near((460, 595), (93, 79, 49)) and near((6, 300), (113, 107, 64))


def wait_for_hud(timeout=120):
    t0 = time.time()
    while time.time() - t0 < timeout:
        time.sleep(3)
        if hud_visible():
            time.sleep(2)  # let the first frame settle
            return True
    return False


def select_row(row):
    for _ in range(20):  # scroll to top
        click(362, 47, 0.05)
    if row < VISIBLE_ROWS:
        click(70, 49 + 16 * row)
    else:
        for _ in range(row - (VISIBLE_ROWS - 1)):
            click(362, 505, 0.1)
        click(70, 49 + 16 * (VISIBLE_ROWS - 1))


def park_mouse():
    move(400, 300)
    time.sleep(1.5)


def capture_map(row, stem):
    select_row(row)
    time.sleep(0.5)
    shot(f"{stem}_00_select.png")
    click(718, 573)  # Play
    if not wait_for_hud():
        print(f"{stem}: HUD never appeared", flush=True)
        shot(f"{stem}_ERR.png")
        return False

    park_mouse()
    shot(f"{stem}_z1_start.png")
    click(13, 513, 1.0)  # zoom out
    click(13, 513, 1.0)
    park_mouse()
    shot(f"{stem}_z2_start.png")

    for i, a in enumerate((0.2, 0.5, 0.8)):
        for j, b in enumerate((0.2, 0.5, 0.8)):
            x = MM_TOP[0] + a * (MM_RIGHT[0] - MM_TOP[0]) + b * (MM_LEFT[0] - MM_TOP[0])
            y = MM_TOP[1] + a * (MM_RIGHT[1] - MM_TOP[1]) + b * (MM_LEFT[1] - MM_TOP[1])
            click(int(x), int(y), 0.8)
            park_mouse()
            shot(f"{stem}_z2_g{i}{j}.png")
    return True


def back_to_map_list():
    click(18, 447, 1.5)    # Options
    click(107, 333, 1.5)   # Main Menu
    click(406, 333, 0.5)   # No (don't save)
    time.sleep(6)
    click(400, 314, 3.0)   # Play Freeform Game


def main():
    os.makedirs(OUT, exist_ok=True)
    only = set(sys.argv[1:])
    for row, stem in enumerate(MAPS):
        if only and stem not in only:
            continue
        t0 = time.time()
        ok = capture_map(row, stem)
        back_to_map_list()
        print(f"{row:2d} {stem:10s} {'ok' if ok else 'FAILED'} {time.time() - t0:.0f}s",
              flush=True)


if __name__ == "__main__":
    main()
