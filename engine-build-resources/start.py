#!/usr/bin/env python3
"""
+==================================================================+
|              ZOO TYCOON 1 ENGINE - FIRST-RUN SETUP               |
+==================================================================+

Called by start.bat. Works out where the original Zoo Tycoon game files
are, imports them, and sets up the runtime folder so the engine can run.

The folder is asked for once and remembered in build\\zt1_game_dir.txt.
After that this is a no-op unless the game files are missing.
"""

import os
import sys

SCRIPT_DIR = os.path.dirname(os.path.abspath(__file__))
ROOT_DIR = os.path.dirname(SCRIPT_DIR)
sys.path.insert(0, SCRIPT_DIR)

import zt1_paths  # noqa: E402  (needs the path above)


class C:
    RESET = "\033[0m"
    BOLD = "\033[1m"
    RED = "\033[91m"
    GREEN = "\033[92m"
    YELLOW = "\033[93m"
    CYAN = "\033[96m"
    DIM = "\033[2m"


def game_files_present():
    """True when the engine already has enough game data to run."""
    present = sum(1 for m in zt1_paths.GAME_MARKERS
                  if os.path.exists(os.path.join(zt1_paths.REL_DIR, m)))
    return present >= 2


def ask_for_folder():
    """Folder-picker dialog, falling back to typing a path."""
    print()
    print(f"  {C.CYAN}{C.BOLD}Where is your original Zoo Tycoon 1 installed?{C.RESET}")
    print(f"  {C.DIM}Pick the folder that contains animals.ztd and zoo.exe.{C.RESET}")
    print(f"  {C.DIM}If you installed Dinosaur Digs and Marine Mania over the base")
    print(f"  game, pick that folder - it has everything.{C.RESET}")
    print()

    try:
        import tkinter as tk
        from tkinter import filedialog

        root = tk.Tk()
        root.withdraw()
        root.attributes("-topmost", True)
        chosen = filedialog.askdirectory(
            title="Select your Zoo Tycoon 1 folder", mustexist=True
        )
        root.destroy()
        if chosen:
            return os.path.normpath(chosen)
    except Exception:
        pass

    # No tkinter (or the user closed the dialog): type it in
    try:
        typed = input(f"  {C.CYAN}Paste the folder path and press Enter: {C.RESET}")
    except EOFError:
        return None
    typed = typed.strip().strip('"')
    return os.path.normpath(typed) if typed else None


def ensure_mod_repository_key():
    """A portable install owns its key; no developer key is distributed."""
    dll = os.path.join(zt1_paths.REL_DIR, "modrepo.dll")
    key = os.path.join(zt1_paths.REL_DIR, "modrepo.key")
    if not os.path.isfile(dll) or os.path.exists(key):
        return
    import secrets
    try:
        with open(key, "x", encoding="ascii") as out:
            out.write(secrets.token_hex(32) + "\n")
    except FileExistsError:
        pass  # Another startup already created this install's key.


def main():
    try:
        ensure_mod_repository_key()
    except OSError as error:
        print(f"Could not initialize the mod repository: {error}")
        return 1
    os.system("cls" if os.name == "nt" else "clear")
    print(f"""{C.CYAN}{C.BOLD}
    +==================================================================+
    |              ZOO TYCOON 1 ENGINE - SETUP                         |
    +==================================================================+
{C.RESET}""")

    src = zt1_paths.find_game_dir()

    if not src:
        src = ask_for_folder()
        if not src or not os.path.isdir(src):
            print(f"\n  {C.RED}[X]{C.RESET} No folder chosen - nothing imported.")
            return 1
        if zt1_paths.is_engine_dir(src):
            print(f"\n  {C.RED}[X]{C.RESET} That's this engine's own folder - pick the "
                  f"folder Zoo Tycoon is installed in. Nothing imported.")
            return 1
        if not zt1_paths.looks_like_game_dir(src):
            print(f"\n  {C.RED}[X]{C.RESET} That folder does not look like Zoo "
                  f"Tycoon (it needs two of animals.ztd, ui.ztd, scenery.ztd).")
            print(f"  {C.DIM}    Nothing imported: run start.bat again and pick the game's folder.{C.RESET}")
            return 1

    print(f"  {C.GREEN}[OK]{C.RESET} Game folder: {src}")
    zt1_paths.save_game_dir(src)

    expansions = zt1_paths.missing_expansions(src)
    if expansions:
        print(f"  {C.YELLOW}[!]{C.RESET} No {', '.join(expansions)} here, so Dinosaur "
              f"Digs / Marine Mania content will be missing.")
        print(f"  {C.DIM}    Install both expansions over the base game for their maps,")
        print(f"  {C.DIM}    animals and the buy-panel filter.{C.RESET}")

    env = dict(os.environ)
    env["ZT1_SOURCE_DIR"] = src

    print()
    print(f"  {C.CYAN}Importing game files and setting up the runtime...{C.RESET}")

    # Only the asset import happens here; start.bat builds the engine next and
    # then runs step 5, because step 5 checks that zt1-engine.exe exists.
    import importlib.util

    path = os.path.join(SCRIPT_DIR, "step4_import.py")
    if not os.path.isfile(path):
        print(f"  {C.RED}[X]{C.RESET} step4_import.py is missing from "
              f"engine-build-resources\\")
        return 1
    spec = importlib.util.spec_from_file_location("step4_import", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    os.environ["ZT1_SOURCE_DIR"] = src
    # step 5 is launched by start.bat, which does the launching itself
    os.environ["ZT1_NO_LAUNCH_PROMPT"] = "1"

    code = module.main() or 0
    if code != 0:
        print(f"\n  {C.RED}[X]{C.RESET} Importing the game files failed.")
    return code


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print(f"\n  {C.YELLOW}Cancelled.{C.RESET}")
        sys.exit(1)
