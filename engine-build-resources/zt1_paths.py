#!/usr/bin/env python3
"""
+==================================================================+
|      ZOO TYCOON 1 ENGINE - SHARED SETUP PATH / ASSET HELPERS     |
+==================================================================+

One place that answers two questions every build step needs:

  * where are the original Zoo Tycoon game files?
  * which files from pc-sync/ should be installed loose, and which
    must be left to the game's own archives?

WHY THIS EXISTS (please read before "tidying" it)
-------------------------------------------------
The UI layouts ship inside the game's ZTD archives in patch order:

    pri 3  xpack2\\UI6.ZTD     (Marine Mania)
    pri 5  zupdate1\\UI4.ZTD   (Dinosaur Digs / patch 1.2)
    pri 8  zupdate\\UI2.ZTD    (patch 1.1)
    pri 10 UI.ZTD             (base game)

The engine gives a LOOSE file in build\\Release\\ui\\ priority over any
archive. pc-sync\\ui\\ holds 77 layouts, but 76 of them are byte-identical
to the BASE UI.ZTD copies. Installing those loose makes them shadow the
patched layouts, and the visible symptom is that the expansion filter
dropdown ("All / Zoo Tycoon / Dinosaur Digs / Marine Mania") disappears
from every buy panel: buldhab.lyt in UI2.ZTD has the dropdown button
([Xpac] id=3204, action=1, target=152) while the base copy has a plain
title text at id 3204 instead.

Only three files in pc-sync\\ui\\ genuinely differ from the archives, so
only those three are installed loose. Keep it that way.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
BUILD_DIR = os.path.join(ROOT_DIR, "build")
REL_DIR = os.path.join(BUILD_DIR, "Release")
PCSYNC_DIR = os.path.join(ROOT_DIR, "pc-sync")

EXECUTABLE_NAME = "zt1-engine.exe"

# Files from pc-sync/ui installed over the game's own. None: the three that
# were here were the original game's files (startup.lyt edited, infoimg.cfg
# and miniclr.cfg the base game's - installed loose they hid the patches'
# and Marine Mania's newer ones). The engine reads the player's own archives.
PCSYNC_UI_OVERRIDES = ()


def is_engine_dir(path):
    """True when `path` is this engine's folder or inside it (importing it
    would copy the folder into itself)."""
    try:
        p = os.path.normcase(os.path.realpath(path))
        root = os.path.normcase(os.path.realpath(ROOT_DIR))
    except OSError:
        return False
    return p == root or p.startswith(root + os.sep)

# Files that identify a real Zoo Tycoon 1 installation folder.
GAME_MARKERS = ("animals.ztd", "ui.ztd", "scenery.ztd")

# Where expansions put their language resources; used only for a warning.
EXPANSION_DLLS = ("lang100.dll", "lang200.dll")

# Remembered between runs so start.bat only asks once.
SETTINGS_FILE = os.path.join(BUILD_DIR, "zt1_game_dir.txt")


def _norm(path):
    return os.path.normcase(os.path.abspath(path.strip().strip('"')))


def looks_like_game_dir(path):
    """True when `path` contains the files an original ZT1 install has."""
    if not path or not os.path.isdir(path):
        return False
    try:
        names = {n.lower() for n in os.listdir(path)}
    except OSError:
        return False
    return sum(1 for m in GAME_MARKERS if m in names) >= 2


def missing_expansions(path):
    """Expansion language DLLs that a folder does not have."""
    if not path or not os.path.isdir(path):
        return list(EXPANSION_DLLS)
    try:
        names = {n.lower() for n in os.listdir(path)}
    except OSError:
        return list(EXPANSION_DLLS)
    return [d for d in EXPANSION_DLLS if d not in names]


def save_game_dir(path):
    try:
        os.makedirs(BUILD_DIR, exist_ok=True)
        with open(SETTINGS_FILE, "w", encoding="utf-8") as f:
            f.write(_norm(path))
    except OSError:
        pass


def load_game_dir():
    try:
        with open(SETTINGS_FILE, "r", encoding="utf-8") as f:
            saved = f.read().strip()
        if saved and looks_like_game_dir(saved):
            return saved
    except OSError:
        pass
    return None


# Common retail / digital locations, so most users are never asked at all.
_COMMON_DIRS = (
    r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon",
    r"C:\Program Files\Microsoft Games\Zoo Tycoon",
    r"C:\Program Files (x86)\EA Games\Zoo Tycoon",
    r"C:\Program Files (x86)\GOG Galaxy\Games\Zoo Tycoon",
    r"C:\Program Files (x86)\Steam\steamapps\common\Zoo Tycoon",
    r"C:\GOG Games\Zoo Tycoon",
)


def candidate_dirs():
    """Folders to try before asking the user, best guess first."""
    out = []
    env = os.environ.get("ZT1_SOURCE_DIR", "").strip().strip('"')
    if env:
        out.append(env)
    saved = load_game_dir()
    if saved:
        out.append(saved)
    out.extend(_COMMON_DIRS)
    # Anything the user already pointed an install at last time.
    seen, uniq = set(), []
    for d in out:
        key = _norm(d)
        if key not in seen:
            seen.add(key)
            uniq.append(d)
    return uniq


def find_game_dir(explicit=None):
    """First existing folder that looks like a ZT1 install, or None."""
    if explicit:
        return _norm(explicit) if looks_like_game_dir(explicit) else None
    for d in candidate_dirs():
        if looks_like_game_dir(d):
            return _norm(d)
    return None


# ----------------------------------------------------------------------
# pc-sync installation
# ----------------------------------------------------------------------

def install_pc_sync_ui(rel_dir=REL_DIR, pc_sync_dir=PCSYNC_DIR, verbose=True):
    """Install only the pc-sync UI files that really override an archive.

    Deliberately does NOT copy the other 74 pc-sync/ui layouts: they are
    base UI.ZTD copies and installing them loose hides the patched layouts
    (see this module's docstring). Returns the number of files installed.
    """
    ui_src = os.path.join(pc_sync_dir, "ui")
    if not os.path.isdir(ui_src):
        if verbose:
            print("  [i] No pc-sync/ui folder - skipping UI overrides")
        return 0

    ui_dst = os.path.join(rel_dir, "ui")
    os.makedirs(ui_dst, exist_ok=True)

    import shutil
    count = 0
    for name in PCSYNC_UI_OVERRIDES:
        src = os.path.join(ui_src, name)
        if not os.path.isfile(src):
            if verbose:
                print(f"  [!] pc-sync/ui/{name} missing - skipped")
            continue
        shutil.copy2(src, os.path.join(ui_dst, name))
        count += 1

    if verbose and count:
        print(f"  [OK] pc-sync UI overrides installed ({count} file(s)); "
              f"the other layouts come from the game archives")
    return count
