"""Publish a release on CodeRook: a named version of the zt1-engine-releases
project, carrying the release's zip and its .sha256 (the same files as the
GitHub release). Run by .github/workflows/release.yml for each weekly alpha,
or by hand, signed in with `cbx sign-in`:

    python engine-build-resources/coderook_release.py --name alpha-2026-10-19 \
        --notes notes.md --zip dist/zt1-engine-alpha-2026-10-19-win64.zip

The releases project holds only the player's README, the licence and a
changelog; each release is a version of it with the downloads attached
(the way VoxAI Coder's releases are kept).
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
PROJECT = "zt1-engine-releases"


def cbx(*args, check=True):
    # (on Windows, npm's cbx is a .cmd: it needs the shell)
    line = subprocess.list2cmdline(["cbx", *args]) if os.name == "nt" else None
    r = subprocess.run(line if line else ["cbx", *args], shell=os.name == "nt", capture_output=True, text=True,
                       encoding="utf-8", errors="replace")
    out = re.sub(r"\x1b\[[0-9;]*m", "", (r.stdout or "") + (r.stderr or ""))
    print("> cbx " + " ".join(args) + "\n" + out.strip(), flush=True)
    if check and r.returncode != 0:
        sys.exit(f"[X] cbx {args[0]} failed")
    return r.returncode, out


def newest_save(project):
    _, out = cbx("versions", project, "--limit", "1")
    m = re.search(r"^\s*v(\d+)\s", out, re.M)
    if not m:
        sys.exit("[X] Couldn't tell which save was just made")
    return m.group(1)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--name", required=True, help="the release's name, e.g. alpha-2026-10-19")
    ap.add_argument("--notes", required=True, help="a Markdown file: what's in this release")
    ap.add_argument("--zip", required=True)
    ap.add_argument("--project", default=PROJECT)
    a = ap.parse_args()
    notes = open(a.notes, encoding="utf-8").read().strip()

    work = tempfile.mkdtemp(prefix="zt1-release-")
    folder = os.path.join(work, a.project)
    code, _ = cbx("clone", a.project, folder, check=False)
    first = code != 0
    if first:
        os.makedirs(folder, exist_ok=True)

    # The player's README, the licence, and the changelog with this release on top
    shutil.copyfile(os.path.join(ROOT, "engine-build-resources", "RELEASE_README.md"), os.path.join(folder, "README.md"))
    shutil.copyfile(os.path.join(ROOT, "LICENSE.txt"), os.path.join(folder, "LICENSE"))
    log = os.path.join(folder, "CHANGELOG.md")
    old = open(log, encoding="utf-8").read() if os.path.exists(log) else "# Changelog\n"
    head, _, rest = old.partition("\n")
    with open(log, "w", encoding="utf-8", newline="\n") as fh:
        fh.write(f"{head}\n\n## {a.name}\n\n{notes}\n{rest}")

    summary = notes.splitlines()[0][:200] if notes else a.name
    if first:
        cbx("submit", folder, "--name", a.project, "--new", "-m", f"{a.name}: {summary}")
        cbx("visibility", "public", a.project, "--yes")
    else:
        cbx("submit", folder, "-m", f"{a.name}: {summary}")
    n = newest_save(a.project)
    cbx("promote", n, a.project, "--name", a.name, "--notes-file", os.path.abspath(a.notes))
    for f in (a.zip, a.zip + ".sha256"):
        if os.path.exists(f):
            cbx("attach", os.path.abspath(f), n, a.project)
    shutil.rmtree(work, ignore_errors=True)
    print(f"[OK] {a.project} v{n} is {a.name}, with its downloads")


if __name__ == "__main__":
    main()
