"""Collect the notices of everything the Lab ships that it did not write.

The installer carries clangd (Apache-2.0 with LLVM exceptions), Ghidra
(Apache-2.0), a JDK (GPLv2 + Classpath Exception) and Python with capstone,
pefile and unicorn (PSF, BSD-3, GPLv2 respectively). Their licences have to
travel with them, so this gathers what each one ships and writes it where the
installer can put it in front of the player - plus the written offer GPLv2
asks for when the corresponding source is not shipped alongside.

Nothing here invents a licence text: it copies the file each project itself
provides, and says plainly when one was not found.
"""
import io
import os

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# What the installer carries, and what each is. The names are the projects'
# own: this is the "who wrote it" half of the notice, which no single file
# states all together.
#   name, licence, where the text lives upstream,
#   a licence text kept beside this script (for one that ships none),
#   the exact corresponding source for the version carried
WHAT = [
    ("clangd", "Apache License 2.0, with LLVM's exceptions",
     "https://llvm.org/LICENSE.txt", None, None),
    ("Ghidra", "Apache License 2.0",
     "https://github.com/NationalSecurityAgency/ghidra/blob/master/LICENSE", None,
     "https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build"),
    ("Eclipse Temurin JDK", "GPLv2 with the Classpath Exception",
     "https://adoptium.net/", None,
     "https://github.com/adoptium/temurin25-binaries/releases/tag/jdk-25.0.4.1%2B1"),
    ("Python", "PSF License Agreement",
     "https://docs.python.org/3/license.html", None,
     "https://www.python.org/downloads/release/python-31210/"),
    ("capstone", "BSD 3-Clause",
     "https://github.com/capstone-engine/capstone/blob/master/LICENSE.TXT", None,
     "https://github.com/capstone-engine/capstone/tree/5.0.9"),
    ("pefile", "MIT",
     "https://github.com/erocarrera/pefile/blob/master/LICENSE", None,
     "https://github.com/erocarrera/pefile/tree/2024.8.26"),
    ("unicorn", "GPLv2",
     "https://github.com/unicorn-engine/unicorn/blob/master/COPYING", "GPL-2.0.txt",
     "https://github.com/unicorn-engine/unicorn/tree/2.1.4"),
]
LICENCES_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), "licenses")

# (the file names a licence is usually in, in the order worth trying)
LICENCE_NAMES = ("LICENSE", "LICENSE.txt", "LICENSE.md", "license.txt", "LICENCE",
                 "COPYING", "COPYING.txt", "NOTICE", "NOTICE.txt", "License.txt")


def find_licence(d, depth=3):
    """The licence text inside a folder, or None (shallow first)."""
    if not d or not os.path.isdir(d):
        return None
    for n in LICENCE_NAMES:
        p = os.path.join(d, n)
        if os.path.isfile(p):
            return p
    if depth <= 0:
        return None
    try:
        for e in sorted(os.listdir(d)):
            p = os.path.join(d, e)
            if os.path.isdir(p) and not e.startswith("."):
                got = find_licence(p, depth - 1)
                if got:
                    return got
    except OSError:
        pass
    return None


def _where(tree, name, site):
    """Where a carried component sits in the tree, or None."""
    for cand in (name.lower(), name.split()[0].lower(), name.split()[0]):
        p = os.path.join(tree, cand)
        if os.path.isdir(p):
            return p
    if name == "Python":
        return os.path.join(tree, "python")
    if name == "Eclipse Temurin JDK":
        return os.path.join(tree, "java")
    # (the Python packages live inside Python: their folder is their .dist-info)
    if os.path.isdir(site):
        low = name.lower()
        for d in sorted(os.listdir(site)):
            if d.lower().startswith(low) and d.lower().endswith(".dist-info"):
                return os.path.join(site, d)
    return None


def gather(tree):
    """Write THIRD-PARTY-NOTICES.txt and SOURCE-OFFER.txt from the tree.

    Returns (path, missing) or None when there is no tree.
    """
    tree = os.path.abspath(tree)
    if not os.path.isdir(tree):
        return None
    site = os.path.join(tree, "python", "Lib", "site-packages")
    parts = [
        "Third-party notices",
        "===================",
        "",
        "The ZT1 Lab carries the programs below so that a machine with none of",
        "them installed can still read zoo.exe. Each is somebody else's work,",
        "under its own licence, and that licence travels with it here.",
        "",
        "For the programs under the GPL, SOURCE-OFFER.txt beside this file is the",
        "written offer of their corresponding source.",
        "",
        "The Lab itself is not these, and none of these is the game: Zoo Tycoon",
        "is not included, and is not ours to include.",
        "",
    ]
    missing = []
    for name, licence, url, kept, source in WHAT:
        where = _where(tree, name, site)
        if where is None:
            continue   # (not carried in this build: nothing to notice)
        parts.append(f"--- {name} ---")
        parts.append(f"Licence: {licence}")
        parts.append(f"More: {url}")
        if source:
            parts.append(f"Corresponding source: {source}")
        got = find_licence(where)
        # (a package may declare a standard licence and ship no copy of it:
        #  unicorn's wheel is GPLv2 and carries none. Use the text kept beside
        #  this script, rather than shipping the program without its licence)
        if not got and kept:
            got = os.path.join(LICENCES_DIR, kept)
            if not os.path.isfile(got):
                got = None
        if got:
            try:
                text = io.open(got, encoding="utf-8", errors="replace").read().strip()
            except OSError:
                text = ""
            if text:
                parts.append("")
                parts.append("\n".join("    " + l for l in text.splitlines()))
            else:
                missing.append((name, got))
        else:
            missing.append((name, None))
        parts.append("")

    # (what the packages themselves say: each Python package keeps its own
    #  .dist-info, which holds its licence - listed so a reader can find it
    #  without opening the tree)
    if os.path.isdir(site):
        info = [d for d in sorted(os.listdir(site)) if d.endswith(".dist-info")]
        if info:
            parts += ["--- the Python packages' own records ---",
                      "Each keeps its licence and metadata here, inside the Lab:",
                      ""] + [f"    tree/python/Lib/site-packages/{d}" for d in info] + [""]

    if missing:
        parts += ["--- licences not found in this build ---",
                  "These components ship, but no licence file was found in the copy:",
                  ""] + [f"    {n}" + (f" (looked in {p}, no licence file)" if p else "")
                         for n, p in missing] + [
                      "",
                      "A release should not be published with one of these missing:",
                      "the project's own page (the More: link above) has the text.",
                      ""]

    out = os.path.join(tree, "THIRD-PARTY-NOTICES.txt")
    io.open(out, "w", encoding="utf-8", newline="\r\n").write("\n".join(parts))
    write_source_offer(tree)
    return out, missing


def write_source_offer(tree):
    """The written offer for the GPL-covered pieces the installer carries.

    The Temurin JDK (GPLv2 with the Classpath Exception) and unicorn (GPLv2)
    are distributed inside the Lab, and GPLv2 gives two ways to meet the source
    requirement: ship the exact corresponding source alongside, or carry a
    written offer to supply it. This is that offer - the source is not in the
    installer (it would be larger than the installer itself), so the offer
    names the exact version carried and where that version's source is
    published.

    Generated from the same table as the notices, so the versions and links
    cannot drift away from what is actually shipped.
    """
    lines = [
        "Written offer of corresponding source",
        "=====================================",
        "",
        "The ZT1 Lab's installer carries the programs below, each under a licence",
        "that asks for corresponding source to be made available. Rather than put",
        "that source in the installer, this is the written offer to supply it, as",
        "GPLv2 section 3 permits.",
        "",
        "To: anyone who received the ZT1 Lab installer carrying this file.",
        "",
        "Offer: the complete corresponding source code for the versions below, on",
        "a medium customarily used for software interchange, for no more than the",
        "cost of physically performing the distribution. Each version is also",
        "published by its own project at the link given, which is the same source",
        "by a faster route.",
        "",
        "Valid for at least three years from the date the installer carrying this",
        "file was distributed.",
        "",
    ]
    for name, licence, url, kept, source in WHAT:
        if "GPL" not in licence:
            continue
        lines += [f"--- {name} ---",
                  f"Licence: {licence}",
                  f"Corresponding source: {source or url}",
                  ""]
    lines += [
        "The other programs the Lab carries are under licences that do not ask",
        "for this (Apache-2.0, PSF, BSD-3, MIT); their notices are in",
        "THIRD-PARTY-NOTICES.txt beside this file.",
        "",
        "Nothing here is the game. Zoo Tycoon is not included, and is not ours",
        "to include.",
        "",
    ]
    out = os.path.join(tree, "SOURCE-OFFER.txt")
    io.open(out, "w", encoding="utf-8", newline="\r\n").write("\n".join(lines))
    return out


if __name__ == "__main__":
    import argparse
    ap = argparse.ArgumentParser()
    ap.add_argument("--tree", default=os.path.join(ROOT, "lab-app", ".lab-pack", "tree"))
    a = ap.parse_args()
    got = gather(a.tree)
    if not got:
        print("no tree at", a.tree)
    else:
        path, missing = got
        print("wrote", path)
        print("wrote", os.path.join(a.tree, "SOURCE-OFFER.txt"))
        if missing:
            print("missing licences:", ", ".join(n for n, _ in missing))
