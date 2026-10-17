"""Pack the ZT1 Lab: the Electron app (a runtime of its own, the app inside
resources/app.asar) and then its installer, dist/ZT1-Lab-Setup-<version>.exe.
Run by engine-build-resources/make_release.py --lab, or by hand:

    python engine-build-resources/pack_lab.py [--version 2026.10.19] [--app-only]

1. Stages the Lab's own tree in lab-app/.lab-pack/tree/, which the installer
   carries and the app reads when the player has no checkout:
     engine/   zt1-engine.exe, built for release (build-x64/Release), and the
               original game's hook (build/Release: zt1-original.exe and
               zt1-hook.dll, 32-bit like zoo.exe) when it is there - the Lab's
               Play views run them beside the game's own files
     python/  the interpreter and the packages the Lab's own tools need
               (capstone, pefile, unicorn) - see vendor_python.py
     ghidra/  Ghidra, for decompiling zoo.exe, and java/ the JDK it needs
               - see vendor_ghidra.py. Big: about 1 GB packed
     re/       the knowledge base (knowledge.json, engine_refs.csv), the Lab's
               Python tools (lab/*.py, kb.py, scan_engine.py, check.py,
               msg.py, rd.py), the Ghidra scripts, the shared lists
               (lab/formulas.json, lab/verdicts.json, lab/loaders.json,
               lab/classes.json, lab/scripts/) and re/README.md - laid out as
               the checkout has them, because the tools find each other by
               their own path. No re/private: that is a person's own work
     clangd/   clangd, for the code intelligence
     ghidra/   a Ghidra project of zoo.exe, when there is one to ship
   None of the original game's files go in, as in the engine's release.

2. Packs the app with electron-builder (lab-app/electron-builder.yml): the
   Electron runtime it downloads, alongside a ZT1 Lab.exe named and pictured
   as ours. Its output is lab-app/.lab-pack/win-unpacked/.

3. Builds the installer with Inno Setup 6 (installer/zt1-lab.iss), which puts
   the app in the player's own folder - and can point the Lab at an engine
   checkout they already have, so the Lab edits and builds that.

It needs node/npm (electron-builder is fetched into lab-app/node_modules/ the
first time). Nothing is uploaded: a release does that.
"""
import argparse
import datetime
import json
import os
import re
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
APP = os.path.join(ROOT, "lab-app")
PACK = os.path.join(APP, ".lab-pack")
TREE = os.path.join(PACK, "tree")
DIST = os.path.join(ROOT, "dist")
EXE = os.path.join(ROOT, "build-x64", "Release", "zt1-engine.exe")
EXE32 = os.path.join(ROOT, "build", "Release", "zt1-engine.exe")
ISS = os.path.join(ROOT, "installer", "zt1-lab.iss")
ISCC_CANDIDATES = [
    os.path.join(os.environ.get("LOCALAPPDATA", ""), "Programs", "Inno Setup 6", "ISCC.exe"),
    r"C:\Program Files (x86)\Inno Setup 6\ISCC.exe",
    r"C:\Program Files\Inno Setup 6\ISCC.exe",
]
# The Lab's Python tools: what runs on its own (zoo.exe read, the knowledge
# base, the maps). re/lab/scripts is the mods the Lab builds, re/ghidra_scripts
# what Ghidra runs
PY_TOP = ("kb.py", "scan_engine.py", "check.py", "msg.py", "rd.py")
PY_LAB = ("atlas.py", "fidelity.py", "uiids.py", "zoorun.py", "zoobin.py", "animaker.py")
SHARED = ("knowledge.json", "engine_refs.csv", "README.md")
SHARED_LAB = ("formulas.json", "verdicts.json", "loaders.json", "classes.json")


def say(*a):
    print(*a, flush=True)


def find(name, candidates):
    found = shutil.which(name) or next((p for p in candidates if p and os.path.exists(p)), None)
    if not found:
        sys.exit(f"[X] No {name} found" + (" (install Inno Setup 6)" if name == "ISCC" else ""))
    return found


def run(args, cwd=ROOT, env=None):
    say("> " + " ".join(f'"{a}"' if " " in a else a for a in args))
    p = subprocess.Popen(args, cwd=cwd, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True, errors="replace")
    for line in p.stdout:
        say(line.rstrip())
    if p.wait() != 0:
        sys.exit(f"[X] Failed ({p.returncode}): {os.path.basename(args[0])}")


def _is_link(p):
    """A symlink OR a Windows junction/directory reparse point.

    Both redirect somewhere else, and here that somewhere could be re/private
    or the game's own files - which are never shipped. os.path.islink is NOT
    enough: it is False for a junction, and both os.walk and shutil.copytree
    happily descend into one (a junction looks like an ordinary directory to
    them). That is exactly how a junction pointing at the game slipped into a
    staged tree in testing.
    """
    try:
        if os.path.islink(p):
            return True
    except OSError:
        pass
    isjunction = getattr(os.path, "isjunction", None)   # (Python 3.12+)
    if isjunction:
        try:
            return bool(isjunction(p))
        except OSError:
            return False
    try:
        st = os.lstat(p)
    except OSError:
        return False
    # (older Python: any reparse point is a redirect)
    return bool(getattr(st, "st_reparse_tag", 0))


def _copy_tree(src, dst, what):
    """Copy a folder, never following a link out of it. Returns the links found.

    os.walk(followlinks=False) does not stop at a junction, and copytree
    follows one too - so this walks the tree itself and simply leaves every
    link out, rather than resolving it.
    """
    skipped = []
    for base, dirs, files in os.walk(src, followlinks=False):
        rel = os.path.relpath(base, src)
        out = dst if rel == "." else os.path.join(dst, rel)
        os.makedirs(out, exist_ok=True)
        for n in list(dirs):
            if _is_link(os.path.join(base, n)):
                skipped.append(os.path.relpath(os.path.join(base, n), src))
                dirs.remove(n)      # (do not walk into it)
        for n in files:
            p = os.path.join(base, n)
            if _is_link(p):
                skipped.append(os.path.relpath(p, src))
                continue
            shutil.copy2(p, os.path.join(out, n))
    return skipped


def copy(src, dst, what):
    """Copy a file or a folder into the staged tree.

    A folder is copied with any symlink or junction left out, never followed:
    one inside a copied folder could point at re/private or at the game's own
    files, and following it would put those into the installer. This ships
    findings ABOUT the game, and the game itself is never shipped - so a link
    is left behind and the build says which one.
    """
    if not os.path.exists(src):
        say(f"    (no {what}: {os.path.relpath(src, ROOT)} isn't there)")
        return False
    os.makedirs(os.path.dirname(dst), exist_ok=True)
    if os.path.isdir(src) and not _is_link(src):
        for rel in _copy_tree(src, dst, what):
            say(f"    [!] {what}: {rel} is a link - left out (a link is not ours to ship)")
    else:
        shutil.copy2(src, dst)
    return True


def stage_tree(require=False):
    """What the installed Lab carries, in the checkout's own layout.

    require=True is a release's build: the pieces a first-time machine must
    not be without (Ghidra, the JDK for it, the Python) are insisted on rather
    than warned about, so a release cannot quietly ship half a Lab.
    """
    say("[1/3] Staging the Lab's own files (lab-app/.lab-pack/tree)")
    shutil.rmtree(TREE, ignore_errors=True)
    # The engine: the release build when there is one, else the other tree's
    exe = EXE if os.path.exists(EXE) else EXE32
    if not os.path.exists(exe):
        sys.exit("[X] No zt1-engine.exe built: run make_release.py (or build the engine) first")
    copy(exe, os.path.join(TREE, "engine", "zt1-engine.exe"), "engine")
    # The original game's hook, 32-bit, beside it (Play original)
    for f in ("zt1-original.exe", "zt1-hook.dll"):
        copy(os.path.join(ROOT, "build", "Release", f), os.path.join(TREE, "engine", f), "original game's hook")
    # (the game's files the engine reads are beside it in a checkout's
    # build/Release; a player's own Zoo Tycoon is what the engine looks for,
    # and mods.cfg says where the lab's own mods are)
    copy(os.path.join(ROOT, "build", "Release", "mods.cfg"), os.path.join(TREE, "engine", "mods.cfg"), "mods.cfg")
    # The knowledge base, the Lab's tools and the shared lists, laid out as
    # re/ is: the tools find each other by looking up from their own path
    re_dst = os.path.join(TREE, "re")
    for f in PY_TOP + SHARED:
        copy(os.path.join(ROOT, "re", f), os.path.join(re_dst, f), f)
    for f in PY_LAB + SHARED_LAB:
        copy(os.path.join(ROOT, "re", "lab", f), os.path.join(re_dst, "lab", f), f)
    copy(os.path.join(ROOT, "re", "lab", "scripts"), os.path.join(re_dst, "lab", "scripts"), "the Lab's mods")
    copy(os.path.join(ROOT, "re", "lab", "lab_api.h"), os.path.join(re_dst, "lab", "lab_api.h"), "lab_api.h")
    copy(os.path.join(ROOT, "re", "ghidra_scripts"), os.path.join(re_dst, "ghidra_scripts"), "Ghidra's scripts")
    # The shared knowledge base (re/cloud): the connector (sync.py) and the
    # Worker's own source, so an installed Lab with no checkout can take
    # part too. Wrangler's local state and its local secrets stay behind:
    # .dev.vars holds the review token and is never shipped.
    copy(
        os.path.join(ROOT, "re", "cloud"),
        os.path.join(re_dst, "cloud"),
        "the shared knowledge base (re/cloud)",
    )
    for junk in (".wrangler", ".dev.vars", "node_modules"):
        stray = os.path.join(re_dst, "cloud", junk)
        if os.path.isdir(stray):
            shutil.rmtree(stray, ignore_errors=True)
        elif os.path.isfile(stray):
            os.remove(stray)
    # clangd: the code intelligence. Its version folder is flattened to
    # tree/clangd/bin/clangd.exe, which is where the app looks for it
    src_clangd = os.path.join(APP, "clangd")
    flat = None
    if os.path.isfile(os.path.join(src_clangd, "bin", "clangd.exe")):
        flat = src_clangd
    else:
        for d in sorted(os.listdir(src_clangd)) if os.path.isdir(src_clangd) else []:
            inner = os.path.join(src_clangd, d)
            if os.path.isdir(os.path.join(inner, "bin")):
                flat = inner
                break
    if flat:
        shutil.copytree(flat, os.path.join(TREE, "clangd"), dirs_exist_ok=True)
    else:
        say("    (no lab-app/clangd: the Lab ships without code intelligence)")
    # The Python the Lab's tools run on (tree/python): the interpreter itself
    # and capstone/pefile/unicorn, so an install on a machine with no Python
    # still has the Scene view, Test Zoo, the function traces and the shared
    # base. vendor_python.py fetches and checks it, and keeps it between
    # builds (delete the folder, or pass --force, to refetch)
    sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
    import vendor_python
    import vendor_ghidra
    vendor_python.build(os.path.join(TREE, "python"), require=require)
    # Ghidra and the Java it runs on: so a first-time machine can decompile at
    # all. The PROJECT still isn't here - it is built out of the game's own
    # zoo.exe, which is nobody's to hand out - but the program is
    try:
      vendor_ghidra.build(TREE, require=require)
    except vendor_ghidra.Missing as e:
      sys.exit("[X] " + str(e))
    # (no Ghidra PROJECT ships, deliberately: a project is built out of
    #  zoo.exe itself, which is the game and not ours to redistribute. The
    #  Lab makes one from the player's own copy, or uses theirs.)
    # Everything the Lab ships that it did not write: clangd, Ghidra, the JDK,
    # Python and its packages. Their licences have to travel with them, so this
    # gathers the text each project provides and writes it into the tree, where
    # the installer puts it in front of the player
    import notices
    got = notices.gather(TREE)
    if got:
        path, missing = got
        n = os.path.join(TREE, "THIRD-PARTY-NOTICES.txt")
        o = os.path.join(TREE, "SOURCE-OFFER.txt")
        say(f"    {os.path.relpath(n, ROOT)} ({len(missing)} without a licence file)")
        say(f"    {os.path.relpath(o, ROOT)} (the written offer for the GPL-covered ones)")
        if missing and require:
            sys.exit(
                "[X] these ship without their licence text: " + ", ".join(n for n, _ in missing)
                + "\n    A release must not go out like that: each project's own page has\n"
                "    the text (see the More: links in the notices file)."
            )
        if require and not os.path.isfile(o):
            sys.exit("[X] the written offer for the GPL-covered pieces was not written")


def pack_app(version):
    say("[2/3] Packing the app (electron-builder: the Electron runtime and app.asar)")
    npm = shutil.which("npm") or shutil.which("npm.cmd")
    if not npm:
        sys.exit("[X] No npm found: the Lab is packed with electron-builder (node/npm)")
    if not os.path.isdir(os.path.join(APP, "node_modules", "electron-builder")):
        say("    installing electron-builder (once)…")
        run([npm, "install", "--no-save", "--no-audit", "--no-fund", "electron-builder@^26"], cwd=APP)
    package_json = os.path.join(APP, "package.json")
    with open(package_json, encoding="utf-8") as fh:
        package = json.load(fh)
    original_version = package["version"]
    package["version"] = version
    with open(package_json, "w", encoding="utf-8", newline="\n") as fh:
        json.dump(package, fh, indent=2)
        fh.write("\n")
    try:
        run([npm, "run", "pack"], cwd=APP)
    finally:
        package["version"] = original_version
        with open(package_json, "w", encoding="utf-8", newline="\n") as fh:
            json.dump(package, fh, indent=2)
            fh.write("\n")
    out = os.path.join(PACK, "win-unpacked")
    if not os.path.exists(os.path.join(out, "ZT1 Lab.exe")):
        sys.exit("[X] electron-builder said it worked, but there's no " + os.path.relpath(out, ROOT))
    return out


def installer(version):
    say("[3/3] The installer (Inno Setup)")
    os.makedirs(DIST, exist_ok=True)
    iscc = find("ISCC", ISCC_CANDIDATES)
    run([iscc, "/Q", f"/DVersion={version}", f"/DSource={ROOT}", f"/DApp={PACK}", f"/DTree={TREE}", ISS])
    setup = os.path.join(DIST, f"ZT1-Lab-Setup-{version}.exe")
    if not os.path.exists(setup):
        sys.exit("[X] Inno Setup said it worked, but there's no " + os.path.relpath(setup, ROOT))
    return setup


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--version", default="", help="year.month.day, e.g. 2026.10.19 (else today's)")
    ap.add_argument("--official", action="store_true", help="a release build (the signed updater manifest is made by the release workflow)")
    ap.add_argument("--app-only", action="store_true", help="pack the app, no installer")
    ap.add_argument("--no-stage", action="store_true", help="keep the staged tree as it is")
    ap.add_argument("--no-pack", action="store_true", help="the installer, from the app already packed")
    ap.add_argument("--require-backend", action="store_true",
                    help="fail unless Ghidra, the JDK for it and the Python are all there "
                         "(a release build uses this)")
    a = ap.parse_args()
    today = datetime.date.today()
    version = a.version or f"{today.year}.{today.month}.{today.day}"
    if not re.fullmatch(r"\d+\.\d+\.\d+", version):
        sys.exit(f"[X] The version is three numbers, like 2026.10.19 (not {version})")
    if not a.no_stage:
        stage_tree(require=a.require_backend)
    if not a.no_pack:
        pack_app(version)
    if a.app_only:
        say(f"[OK] {os.path.relpath(os.path.join(PACK, 'win-unpacked', 'ZT1 Lab.exe'), ROOT)} (no installer)")
    else:
        setup = installer(version)
        import hashlib
        digest = hashlib.sha256()
        with open(setup, "rb") as binary:
            for block in iter(lambda: binary.read(1024 * 1024), b""):
                digest.update(block)
        with open(setup + ".sha256", "w", encoding="ascii", newline="\n") as fh:
            fh.write(f"{digest.hexdigest()}  {os.path.basename(setup)}\n")
        size = os.path.getsize(setup) / 1024 / 1024
        say(f"[OK] {os.path.relpath(setup, ROOT)}: {size:.1f} MB")
