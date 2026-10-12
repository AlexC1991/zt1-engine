"""Make a release of the engine, the way the Alpha was made.

Run from the ZT1 Lab (Engine code > Make release) or by hand:
    python engine-build-resources/make_release.py [--version 0.4.0-alpha] [--no-build]

1. Builds the engine with MSVC, x64, Release, in build-x64/ (a folder of its
   own: the Lab's build/ is Win32 and is left alone).
2. Writes dist/zt1-engine-<version, else YYYYMMDD>-win64.zip and its .sha256:
     zt1-engine-<date>-win64/build/Release/zt1-engine.exe  (where start.bat looks)
     engine-build-resources/  the setup scripts start.bat runs (only those)
     tools/decode_button_animation.py  (step5 runs it on the player's ui.ztd)
     fonts/                   our UI font (Aileron, open licence)
     pc-sync/                 empty: for the player's own overrides
     saves/                   scenario progress
     start.bat, LICENSE.txt, README.md + README.txt (the release notes)

Nothing is uploaded: publishing the zip is up to you.

No Zoo Tycoon content goes in. The player's own install supplies it when
start.bat first runs. Refused outright: .ztd/.zoo files and the game's DLLs.
It needs no copy of the game, so it runs on a build server too (the GitHub
Actions release workflow, .github/workflows/release.yml).
"""
import argparse
import datetime
import hashlib
import os
import shutil
import subprocess
import sys
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build-x64")
EXE = os.path.join(BUILD, "Release", "zt1-engine.exe")
# The original game's DLLs (never shipped)
GAME_DLL_PREFIXES = ("lang", "res", "dwin", "ebueula", "eula", "imeuires", "ijl15", "setupenu")
# What start.bat runs, from engine-build-resources/
PLAYER_SCRIPTS = ("start.py", "zt1_paths.py", "step3_build.py", "step4_import.py", "step5_setup.py")
CMAKE_CANDIDATES = [
    r"C:\Program Files\CMake\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
]


def say(*a):
    print(*a, flush=True)


def cmake():
    found = shutil.which("cmake") or next((p for p in CMAKE_CANDIDATES if os.path.exists(p)), None)
    if not found:
        sys.exit("[X] No CMake found (Visual Studio's, or CMake on the PATH)")
    return found


def run(args):
    say("> " + " ".join(f'"{a}"' if " " in a else a for a in args))
    p = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in p.stdout:
        say(line.rstrip())
    if p.wait() != 0:
        sys.exit(f"[X] Failed ({p.returncode}): {args[1] if len(args) > 1 else args[0]}")


def build():
    c = cmake()
    if not os.path.exists(os.path.join(BUILD, "CMakeCache.txt")):
        say("[1/3] Configuring build-x64/ (MSVC, x64)")
        run([c, "-S", ROOT, "-B", BUILD, "-A", "x64"])
    say("[2/3] Building zt1-engine, Release")
    # (CMakeLists.txt links SDL2_image-static / SDL2_mixer-static by file
    # name, not as targets, so nothing builds them before the engine links:
    # built first here)
    run([c, "--build", BUILD, "--config", "Release", "--target", "SDL2_image", "SDL2_mixer", "--parallel"])
    run([c, "--build", BUILD, "--config", "Release", "--target", "zt1-engine", "--parallel"])
    if not os.path.exists(EXE):
        sys.exit("[X] The build said it worked, but there's no " + os.path.relpath(EXE, ROOT))


def package(version):
    say("[3/3] Packaging")
    date = datetime.date.today().strftime("%Y%m%d")
    name = f"zt1-engine-{version or date}-win64"
    out = os.path.join(ROOT, "dist", name + ".zip")
    entries = []

    entries.append((f"{name}/build/Release/zt1-engine.exe", EXE))

    def add_tree(rel_dir):
        top = os.path.join(ROOT, rel_dir)
        for d, dirs, files in os.walk(top):
            dirs[:] = [x for x in dirs if x != "__pycache__"]
            for f in files:
                if not f.endswith((".pyc", ".bak")):
                    p = os.path.join(d, f)
                    entries.append((f"{name}/{os.path.relpath(p, ROOT).replace(chr(92), '/')}", p))

    # The setup start.bat runs (not the dev tools beside them), the font,
    # the one tool step5 runs (it makes the button art from the player's own
    # ui.ztd)
    for f in PLAYER_SCRIPTS:
        entries.append((f"{name}/engine-build-resources/{f}", os.path.join(ROOT, "engine-build-resources", f)))
    entries.append((f"{name}/tools/decode_button_animation.py", os.path.join(ROOT, "tools", "decode_button_animation.py")))
    add_tree("fonts")
    for f in ("start.bat", "LICENSE.txt"):
        if os.path.isfile(os.path.join(ROOT, f)):
            entries.append((f"{name}/{f}", os.path.join(ROOT, f)))
    notes = os.path.join(ROOT, "engine-build-resources", "RELEASE_README.md")
    if os.path.isfile(notes):
        entries.append((f"{name}/README.md", notes))
        entries.append((f"{name}/README.txt", notes))

    # The guard: nothing of the game's slips in
    bad = [a for a, _ in entries
           if a.lower().endswith((".ztd", ".zoo"))
           or (a.lower().endswith(".dll") and os.path.basename(a).lower().startswith(GAME_DLL_PREFIXES))]
    if bad:
        sys.exit("[X] Refusing to package the game's files: " + ", ".join(bad[:5]))

    os.makedirs(os.path.dirname(out), exist_ok=True)
    tmp = out + ".tmp"
    missing = [src for _a, src in entries if not os.path.isfile(src)]
    if missing:
        sys.exit("[X] Missing: " + ", ".join(os.path.relpath(m, ROOT) for m in missing))
    with zipfile.ZipFile(tmp, "w", zipfile.ZIP_DEFLATED, compresslevel=9) as z:
        for arc, src in entries:
            z.write(src, arc)
        # (folders the game writes to: scenario progress goes in saves/;
        # pc-sync/ for the player's own overrides)
        z.writestr(f"{name}/saves/", "")
        z.writestr(f"{name}/pc-sync/README.txt",
                   "Your own files to put over the game's go here (start.bat copies them in).\r\n"
                   "None of Zoo Tycoon's own files are shipped: start.bat imports them from your install.\r\n")
    os.replace(tmp, out)
    sha = hashlib.sha256(open(out, "rb").read()).hexdigest()
    with open(out + ".sha256", "w") as fh:
        fh.write(f"{sha}  {os.path.basename(out)}\n")
    say(f"[OK] {os.path.relpath(out, ROOT)}: {len(entries)} files, {os.path.getsize(out) / 1024 / 1024:.1f} MB")
    say(f"     sha256 {sha}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="", help="the release's version for its name (else today's date)")
    ap.add_argument("--no-build", action="store_true", help="package the exe already in build-x64/")
    a = ap.parse_args()
    if not a.no_build:
        build()
    elif not os.path.exists(EXE):
        sys.exit("[X] No " + os.path.relpath(EXE, ROOT) + ": build first")
    package(a.version)
