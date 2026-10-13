"""Vendor a Python for the Lab: the interpreter itself, plus the three
packages the Lab's tools need. Until now the Lab was built on whatever Python
the builder happened to have - which meant an install on a machine with no
Python (or one without these) had no Scene view, no Test Zoo, no function
traces and no shared-base sync, with nothing to say why.

Run by engine-build-resources/pack_lab.py while staging the tree, or by hand:

    python engine-build-resources/vendor_python.py [--out <dir>] [--force]

What it makes, laid out inside the Lab's own tree:

    python/python.exe           the interpreter (no install, no PATH, no admin)
    python/python312.zip        its standard library
    python/python312._pth       the path file that turns on site/ for the
                                packages below (an embeddable Python has it off)
    python/Lib/site-packages/   capstone, pefile, unicorn

The embeddable build is the official one from python.org: about 11 MB, and it
needs nothing from the machine it lands on. It has no tkinter (check.py's
optional window is the only user, and it says so when it isn't there).
"""
import argparse
import io
import os
import shutil
import subprocess
import sys
import urllib.request
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
# (pinned: a release must build the same Python every time. 3.12 is what the
# tools are written and tested against)
PY = "3.12.10"
EMBED = f"https://www.python.org/ftp/python/{PY}/python-{PY}-embed-amd64.zip"
# (what the Lab's Python tools import from outside the standard library. Each
# is self-contained - pefile has no dependencies, capstone and unicorn only
# need things on Pythons older than 3.9 - so there is nothing else to fetch)
PACKAGES = ("capstone", "pefile", "unicorn")


def say(*a):
    print(*a, flush=True)


def fetch(url, dest):
    """Download, with an explicit user-agent (some hosts refuse Python's)."""
    req = urllib.request.Request(url, headers={"user-agent": "zt1-lab-build/1.0"})
    with urllib.request.urlopen(req, timeout=300) as r, open(dest, "wb") as f:
        shutil.copyfileobj(r, f)
    return os.path.getsize(dest)


def wheels(dest):
    """The packages as wheels, for the interpreter being shipped.

    (--only-binary: never build from source, so no compiler is needed and the
    version that lands is the one tested. The platform and the Python version
    are stated so a machine building with a different Python still produces
    wheels for the 3.12 that ships.)
    """
    os.makedirs(dest, exist_ok=True)
    cmd = [
        sys.executable, "-m", "pip", "download",
        "--only-binary", ":all:",
        "--python-version", "3.12",
        "--platform", "win_amd64",
        "--implementation", "cp",
        "--dest", dest,
        *PACKAGES,
    ]
    say("> " + " ".join(cmd))
    p = subprocess.Popen(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                         text=True, errors="replace")
    for line in p.stdout:
        say("    " + line.rstrip())
    if p.wait() != 0:
        sys.exit("[X] pip could not fetch the packages")
    return [os.path.join(dest, f) for f in sorted(os.listdir(dest)) if f.endswith(".whl")]


def install_wheel(whl, site_packages, licences=None):
    """A wheel is a zip: unpack it, minus the records pip would write - and
    minus the C development files.

    unicorn's wheel carries lib/unicorn.lib and its headers (50 MB) so C
    programs can link against it, and capstone carries headers likewise. The
    Lab only ever imports these from Python, where the DLL beside them is what
    is loaded, so the .lib and lib/ folders are dead weight in an installer.

    What it does NOT drop is the licence: `.dist-info` holds it (the wheel's
    licence text and who wrote it), so when `licences` is given the whole
    .dist-info is kept, name and all. The Lab ships these packages, so their
    notices have to travel with them.
    """
    with zipfile.ZipFile(whl) as z:
        for n in z.namelist():
            parts = n.split("/")
            if n.endswith("/"):
                continue
            # (nothing escapes the folder we are unpacking into)
            if ".." in parts or n.startswith("/"):
                continue
            if parts[0].endswith(".dist-info"):
                # (the licence, the metadata and the record of what is in it:
                #  a wheel's own statement of what it is and who wrote it)
                if licences and parts[0] not in licences:
                    licences.append(parts[0])
                z.extract(n, site_packages)
                continue
            # (the C development files, in <package>/lib/. They are for
            #  compiling against the package, never for importing it - and
            #  unicorn.lib alone is 50 MB. Just these extensions: capstone and
            #  unicorn both keep the DLL Python actually loads in that same
            #  folder, so nothing else there may be dropped)
            if os.path.splitext(n)[1].lower() in (".lib", ".h", ".hpp", ".exp"):
                continue
            z.extract(n, site_packages)


def build(out, force=False, work=None, require=False):
    if os.path.isdir(out) and not force:
        say(f"[=] {os.path.relpath(out, ROOT)} is already there (--force to rebuild)")
        return out
    shutil.rmtree(out, ignore_errors=True)
    work = work or os.path.join(os.path.dirname(out), ".python-build")
    os.makedirs(work, exist_ok=True)

    # 1. the interpreter
    zip_path = os.path.join(work, f"python-{PY}-embed-amd64.zip")
    say(f"[1/4] Fetching Python {PY} (the embeddable build)")
    if not os.path.exists(zip_path):
        mb = fetch(EMBED, zip_path) / 1e6
        say(f"      {mb:.1f} MB from python.org")
    os.makedirs(out, exist_ok=True)
    with zipfile.ZipFile(zip_path) as z:
        z.extractall(out)

    # 2. the packages (and their licences: a .dist-info is kept whole, so the
    #    notice each wheel carries travels with it. The Lab ships software
    #    somebody else wrote, so its licence has to be in the installer)
    say(f"[2/4] Fetching the packages: {', '.join(PACKAGES)}")
    whls = wheels(os.path.join(work, "wheels"))
    site = os.path.join(out, "Lib", "site-packages")
    os.makedirs(site, exist_ok=True)
    licences = []
    for w in whls:
        install_wheel(w, site, licences)
        say(f"      {os.path.basename(w)}")
    if licences:
        say(f"      licences kept: {', '.join(sorted(set(licences)))}")

    # 3. turn site/ on, and put Lib\\site-packages on the path
    #    (an embeddable Python ships a ._pth that disables site, so nothing in
    #    site-packages is importable until this says so)
    say("[3/4] Turning on site/ so the packages are importable")
    for f in os.listdir(out):
        if f.endswith("._pth"):
            io.open(os.path.join(out, f), "w", encoding="utf-8", newline="\n").write(
                "python312.zip\n"
                ".\n"
                "Lib\\site-packages\n"
                "import site\n"
            )
            say(f"      wrote {f}")

    # 4. prove it, from the copy itself
    say("[4/4] Checking the copy runs and can import them")
    py = os.path.join(out, "python.exe")
    if not os.path.exists(py):
        sys.exit(f"[X] No python.exe in {os.path.relpath(out, ROOT)}")
    code = ("import sys, capstone, pefile, unicorn; "
            "print('python', sys.version.split()[0]); "
            "print('capstone', capstone.__version__); "
            "print('pefile', pefile.__version__); "
            "print('unicorn', unicorn.__version__)")
    r = subprocess.run([py, "-c", code], capture_output=True, text=True, timeout=180)
    say("      " + (r.stdout or r.stderr).strip().replace("\n", "\n      "))
    if r.returncode != 0:
        sys.exit("[X] The bundled Python cannot import what the Lab needs")
    # (the same checks a clean machine would make. The embeddable Python is a
    #  bare interpreter: it ships its own vcruntime140.dll (which is why it
    #  starts where Python is not installed), but it will NOT start if that
    #  file is gone, and a release must not find that out from a player)
    for need in ("vcruntime140.dll", os.path.join("Lib", "site-packages", "capstone"),
                 os.path.join("Lib", "site-packages", "unicorn")):
        if not os.path.exists(os.path.join(out, need)):
            sys.exit(f"[X] The bundled Python is missing {need}: an install could not run it")
    # (and it must run with nothing else on the machine: a nearly empty
    #  environment, the way a player's PC has no Python and no developer tools)
    bare = {"SystemRoot": r"C:\Windows", "windir": r"C:\Windows",
            "PATH": r"C:\Windows\system32;C:\Windows",
            "TEMP": os.environ.get("TEMP", r"C:\Windows\Temp")}
    r = subprocess.run([py, "-c",
                        "import sys, ctypes, ssl, capstone, pefile, unicorn; "
                        "print('bare environment ok:', sys.version.split()[0])"],
                       env=bare, capture_output=True, text=True, timeout=180)
    say("      " + ((r.stdout or r.stderr).strip().splitlines() or [""])[-1])
    if r.returncode != 0:
        if require:
            sys.exit("[X] The bundled Python does not run with nothing else installed")
        say("      (!) it ran here, but not with an empty environment: check it before a release")

    shutil.rmtree(work, ignore_errors=True)
    mb = sum(os.path.getsize(os.path.join(dp, f))
             for dp, _, fs in os.walk(out) for f in fs) / 1e6
    say(f"[OK] {os.path.relpath(out, ROOT)}: {mb:.1f} MB")
    return out


if __name__ == "__main__":
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "lab-app", ".lab-pack", "tree", "python"))
    ap.add_argument("--force", action="store_true", help="rebuild it even if it's there")
    ap.add_argument("--require", action="store_true",
                    help="fail unless the copy runs with an empty environment (a release does)")
    a = ap.parse_args()
    build(a.out, a.force, require=a.require)
