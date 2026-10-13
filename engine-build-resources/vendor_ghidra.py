"""Ship Ghidra and the Java it runs on with the Lab.

The player gets a working Ghidra without installing one. What it can NOT ship
is the project: a Ghidra project is built out of zoo.exe itself (the game's own
file, which is nobody's to redistribute), so the Lab still makes one from the
player's own copy - the Ghidra here is the program, not the game.

This is large - Ghidra unpacks to about 1 GB and the JDK about 300 MB - so
everything but the program proper is left out, and it is all skipped entirely
when these are not on the machine building it.
"""
import argparse
import os
import shutil
import subprocess
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (a release must not quietly ship a Lab without these: see --require. The
#  installer promises a first-time machine nothing to install, and Ghidra and
#  the JDK are half of that promise - the decompiled views are dead without
#  them, and nothing on the machine would say why)
class Missing(Exception):
    pass

# (where Ghidra is on the machine building the release, in order. The first
# that has support/analyzeHeadless.bat wins. GHIDRA_INSTALL_DIR is what Ghidra
# itself sets)
GHIDRA_CANDIDATES = [
    os.environ.get("GHIDRA_INSTALL_DIR", ""),
    r"E:\ghidra\ghidra_12.1.4_PUBLIC",
    r"C:\ghidra",
    r"C:\tools\ghidra",
]
JAVA_CANDIDATES = [
    os.environ.get("JAVA_HOME", ""),
    r"C:\Program Files\Eclipse Adoptium\jdk-25.0.1.8-hotspot",
    r"C:\Program Files\Java",
]

# (never needed to run headless Ghidra: the manuals, the debugger's GUI pieces,
#  the servers. Kept as names so it is obvious what is being left behind)
GHIDRA_SKIP = ("docs", "docker", "server", ".git", "GhidraServer", "BSim", "Ghidra\\Debug")


def find(cands, marker):
    for c in cands:
        if not c:
            continue
        if os.path.isfile(os.path.join(c, marker)):
            return c
    # (a Java home is often a folder of versions: take one that has the marker)
    for c in cands:
        if not c or not os.path.isdir(c):
            continue
        for e in sorted(os.listdir(c)):
            p = os.path.join(c, e)
            if os.path.isdir(p) and os.path.isfile(os.path.join(p, marker)):
                return p
    return None


def size_mb(p):
    if not p or not os.path.isdir(p):
        return 0
    return sum(os.path.getsize(os.path.join(dp, f))
               for dp, _, fs in os.walk(p) for f in fs) / 1e6


def build(tree, force=False, require=False):
    ghidra_dst = os.path.join(tree, "ghidra")
    java_dst = os.path.join(tree, "java")

    if os.path.isdir(ghidra_dst) and not force:
        print(f"[=] {os.path.relpath(ghidra_dst, ROOT)} is already there (--force to rebuild)")
    else:
        src = find(GHIDRA_CANDIDATES, os.path.join("support", "analyzeHeadless.bat"))
        if not src:
            if require:
                raise Missing(
                    "no Ghidra on this machine, and this build asked for one.\n"
                    "    A release's installer must carry Ghidra (and the JDK for it):\n"
                    "    without them the decompiled views are dead on a machine that\n"
                    "    installed nothing else. Put a Ghidra where this can find it -\n"
                    "    set GHIDRA_INSTALL_DIR, or put it in one of:\n"
                    + "\n".join("      " + c for c in GHIDRA_CANDIDATES if c)
                )
            print("[=] No Ghidra on this machine: the Lab ships without one")
            print("    (the player can still point it at their own: re/private/ghidra.json)")
            print("    (a release must not do this: build with --require)")
            return None
        print(f"[1/2] Ghidra from {src} ({size_mb(src):.0f} MB)")
        shutil.rmtree(ghidra_dst, ignore_errors=True)
        # (the releases unpack to ghidra_<version>_PUBLIC/, and that level is
        #  what has support/: ship it as tree/ghidra so the app finds it simply)
        shutil.copytree(src, ghidra_dst,
                        ignore=shutil.ignore_patterns(*GHIDRA_SKIP), dirs_exist_ok=True)
        for junk in GHIDRA_SKIP:
            p = os.path.join(ghidra_dst, junk)
            if os.path.isdir(p):
                shutil.rmtree(p, ignore_errors=True)
        print(f"      -> {size_mb(ghidra_dst):.0f} MB (dropped: {', '.join(GHIDRA_SKIP)})")

    if os.path.isdir(java_dst) and not force:
        print(f"[=] {os.path.relpath(java_dst, ROOT)} is already there (--force to rebuild)")
    else:
        src = find(JAVA_CANDIDATES, os.path.join("bin", "java.exe"))
        if not src:
            if require:
                raise Missing(
                    "no Java on this machine, and this build asked for one.\n"
                    "    Ghidra is a Java program: without a JDK here it cannot start,\n"
                    "    so the carried Ghidra would do nothing. Set JAVA_HOME, or put\n"
                    "    a JDK in one of:\n"
                    + "\n".join("      " + c for c in JAVA_CANDIDATES if c)
                )
            print("[=] No Java on this machine: Ghidra cannot start without one")
            print("    (a release must not do this: build with --require)")
            return ghidra_dst
        print(f"[2/2] Java from {src} ({size_mb(src):.0f} MB)")
        shutil.rmtree(java_dst, ignore_errors=True)
        shutil.copytree(src, java_dst,
                        ignore=shutil.ignore_patterns("demo", "sample", "man", "src.zip",
                                                      "jmods", ".git"),
                        dirs_exist_ok=True)
        # (jmods is for building a runtime, not running one: 100 MB and no use
        #  to a Ghidra the Lab starts)
        jm = os.path.join(java_dst, "jmods")
        if os.path.isdir(jm):
            shutil.rmtree(jm, ignore_errors=True)
        print(f"      -> {size_mb(java_dst):.0f} MB")

    # (does it actually run? Ghidra's own launcher, with the Java we just put
    #  beside it - the same check the app makes)
    java = os.path.join(java_dst, "bin", "java.exe")
    if os.path.isfile(java):
        r = subprocess.run([java, "-version"], capture_output=True, text=True, timeout=120)
        print("      java -version:", ((r.stdout or r.stderr).strip().splitlines() or [""])[0])
    elif require:
        raise Missing("Ghidra is there but no Java is: the two go together")
    # (and Ghidra's own launcher, which is what the app actually runs. It
    #  prints its usage and then waits for a keypress - so it does not exit,
    #  and it is the OUTPUT that says whether it started. With no Java at all
    #  it prints 'the java command could not be found' instead, and that is
    #  what a release must not ship)
    bat = os.path.join(ghidra_dst, "support", "analyzeHeadless.bat")
    if require and os.path.isfile(bat) and os.path.isfile(java):
        env = {**os.environ, "JAVA_HOME": java_dst, "PATH": r"C:\Windows\system32;C:\Windows"}
        out = os.path.join(os.path.dirname(ghidra_dst), ".ghidra-launch.txt")
        try:
            with open(out, "w", encoding="utf-8", errors="replace") as f:
                p = subprocess.Popen(["cmd", "/c", bat, "-badusage"], env=env, cwd=ghidra_dst,
                                     stdin=subprocess.DEVNULL, stdout=f, stderr=subprocess.STDOUT)
                try:
                    p.wait(timeout=180)
                except subprocess.TimeoutExpired:
                    p.kill()   # (it is waiting for the keypress: that is success)
            said = open(out, encoding="utf-8", errors="replace").read()
        finally:
            if os.path.exists(out):
                os.remove(out)
        low = said.lower()
        if "could not be found" in low:
            raise Missing("the carried Ghidra could not start on the carried Java\n"
                          "    (its launcher said the java command was not found)")
        if "usage" not in low and "analyzeheadless" not in low:
            raise Missing("the carried Ghidra did not run (its launcher said nothing useful)")
        print("      Ghidra's own launcher starts on the carried Java")
    return ghidra_dst


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "lab-app", ".lab-pack", "tree"))
    ap.add_argument("--force", action="store_true")
    ap.add_argument("--require", action="store_true",
                    help="fail (rather than warn) when Ghidra or Java is missing")
    a = ap.parse_args()
    try:
        build(a.out, a.force, a.require)
    except Missing as e:
        sys.exit("[X] " + str(e))
