"""Make a release of the engine: its installer, and the feed the in-game
updater reads.

Run by the release workflow (.github/workflows/release.yml), from the ZT1
Lab (Engine code > Make release), or by hand:
    python engine-build-resources/make_release.py [--version 2026.10.19] [--official] [--no-build]

1. Builds the engine with MSVC, x64, Release, in build-x64/ (a folder of its
   own: the Lab's build/ is Win32 and is left alone), with the C++ runtime
   built in (players need no Visual C++ Redistributable) and the version
   baked in. --official marks it as a release that may update itself
   (src/Updater.hpp): only the release workflow passes it.
2. Builds the installer with Inno Setup 6 (installer/zt1-engine.iss):
   dist/ZT1-Engine-Setup-<version>.exe, and its .sha256.
3. Writes dist/latest.yml, the updater's feed (electron-updater's format:
   the version, the installer's name, its SHA-512 and size). The release
   carries it, on GitHub and on CodeRook (which also makes its own from the
   release's name and files).

The version is a date, year.month.day without leading zeros (2026.10.19):
a version number the feeds can compare. Without --version, today's.

No Zoo Tycoon content goes in: the engine reads the player's own install.
It needs no copy of the game, so it runs on a build server too.
"""
import argparse
import base64
import datetime
import hashlib
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
BUILD = os.path.join(ROOT, "build-x64")
EXE = os.path.join(BUILD, "Release", "zt1-engine.exe")
DIST = os.path.join(ROOT, "dist")
ISS = os.path.join(ROOT, "installer", "zt1-engine.iss")
CMAKE_CANDIDATES = [
    r"C:\Program Files\CMake\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
    r"C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe",
]
ISCC_CANDIDATES = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "Inno Setup 6", "ISCC.exe"),
    r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    r"C:\Program Files\Inno Setup 6\ISCC.exe",
]


def say(*a):
    print(*a, flush=True)


def find(name, candidates):
    found = shutil.which(name) or next((p for p in candidates if p and os.path.exists(p)), None)
    if not found:
        sys.exit(f"[X] No {name} found")
    return found


def run(args):
    say("> " + " ".join(f'"{a}"' if " " in a else a for a in args))
    p = subprocess.Popen(args, cwd=ROOT, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in p.stdout:
        say(line.rstrip())
    if p.wait() != 0:
        sys.exit(f"[X] Failed ({p.returncode}): {os.path.basename(args[0])}")


def build(version, official):
    c = find("cmake", CMAKE_CANDIDATES)
    say(f"[1/3] Building zt1-engine {version}{' (official)' if official else ''}, x64 Release")
    run([c, "-S", ROOT, "-B", BUILD, "-A", "x64", "-DZT1_STATIC_RUNTIME=ON", f"-DZT1_VERSION={version}",
         f"-DZT1_OFFICIAL_BUILD={'ON' if official else 'OFF'}"])
    # (CMakeLists.txt links SDL2_image-static / SDL2_mixer-static by file
    # name, not as targets, so nothing builds them before the engine links:
    # built first here)
    run([c, "--build", BUILD, "--config", "Release", "--target", "SDL2_image", "SDL2_mixer", "--parallel"])
    run([c, "--build", BUILD, "--config", "Release", "--target", "zt1-engine", "--parallel"])
    if not os.path.exists(EXE):
        sys.exit("[X] The build said it worked, but there's no " + os.path.relpath(EXE, ROOT))


def installer(version):
    say("[2/3] The installer (Inno Setup)")
    os.makedirs(DIST, exist_ok=True)
    iscc = find("ISCC", ISCC_CANDIDATES)
    run([iscc, "/Q", f"/DVersion={version}", f"/DSource={ROOT}", f"/DExe={EXE}", ISS])
    setup = os.path.join(DIST, f"ZT1-Engine-Setup-{version}.exe")
    if not os.path.exists(setup):
        sys.exit("[X] Inno Setup said it worked, but there's no " + os.path.relpath(setup, ROOT))
    return setup


def feed(setup, version):
    say("[3/3] The updater's feed (latest.yml)")
    data = open(setup, "rb").read()
    sha256 = hashlib.sha256(data).hexdigest()
    sha512 = base64.b64encode(hashlib.sha512(data).digest()).decode()
    name = os.path.basename(setup)
    with open(setup + ".sha256", "w", newline="\n") as fh:
        fh.write(f"{sha256}  {name}\n")
    when = datetime.datetime.now(datetime.timezone.utc).strftime("%Y-%m-%dT%H:%M:%S.000Z")
    with open(os.path.join(DIST, "latest.yml"), "w", newline="\n") as fh:
        fh.write(f"version: '{version}'\nfiles:\n  - url: '{name}'\n    sha512: '{sha512}'\n    size: {len(data)}\n"
                 f"path: '{name}'\nsha512: '{sha512}'\nreleaseDate: '{when}'\n")
    say(f"[OK] {os.path.relpath(setup, ROOT)}: {len(data) / 1024 / 1024:.1f} MB")
    say(f"     sha256 {sha256}")


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="", help="year.month.day, e.g. 2026.10.19 (else today's)")
    ap.add_argument("--official", action="store_true", help="a release that may update itself (the release workflow)")
    ap.add_argument("--no-build", action="store_true", help="make the installer from the exe already in build-x64/")
    a = ap.parse_args()
    today = datetime.date.today()
    version = a.version or f"{today.year}.{today.month}.{today.day}"
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"[X] The version is three numbers, like 2026.10.19 (not {version})")
    if not a.no_build:
        build(version, a.official)
    elif not os.path.exists(EXE):
        sys.exit("[X] No " + os.path.relpath(EXE, ROOT) + ": build first")
    feed(installer(version), version)
