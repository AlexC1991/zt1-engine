# Shared by the ZT1_*.py Ghidra scripts: where the repo is, reading and
# writing its CSVs. Runs in Ghidra's Jython (Python 2.7) and in PyGhidra
# (Python 3), so: no f-strings, csv opened the way each wants.
from __future__ import print_function
import csv, os, sys

# The zoo.exe everything in re/ is for: the 2002 Complete Collection build
# (2,449,432 bytes, 25 Sep 2002). Another build's addresses don't match.
ZOO_SHA256 = "61fcc6d18bdd9339b8ce1ed00ed5a9f1bffca00781266396826f6aa2d00124d5"

# Names Ghidra (or OpenZT's project) gives a function nobody has worked out
UNNAMED = ("FUN_", "meth_0x", "virt_", "cls_0x", "~cls_0", "dtor_0x", "thunk_", "switchD", "caseD_", "LAB_")


def repo_root(source_file):
    """The repo, from the running script's own file (getSourceFile():
    re/ghidra_scripts/x.py)."""
    here = source_file.getParentFile().getAbsolutePath()
    # (through a link - %USERPROFILE%\ghidra_scripts - to the real folder)
    try:
        from java.nio.file import Paths
        here = Paths.get(here).toRealPath().toString()
    except:  # noqa: E722 (Java exceptions too)
        pass
    root = os.path.dirname(os.path.dirname(here))
    if not os.path.isdir(os.path.join(root, "re", "ghidra_scripts")):
        raise RuntimeError("Can't find the zt1-engine repo from %s: put the scripts' folder in Script Manager "
                           "as <repo>\\re\\ghidra_scripts" % here)
    return root


def path(root, *parts):
    return os.path.join(root, *parts)


def is_named(name):
    return bool(name) and not name.startswith(UNNAMED) and name != "entry"


def read_csv(file_name):
    """Rows as dicts, keys lower case and trimmed (OpenZT's exporter writes "Class, Name,Address")."""
    if not os.path.exists(file_name):
        return []
    if sys.version_info[0] >= 3:
        f = open(file_name, "r", newline="", encoding="utf-8")
    else:
        f = open(file_name, "rb")
    try:
        reader = csv.reader(f)
        rows = list(reader)
    finally:
        f.close()
    if not rows:
        return []
    head = [h.strip().lower() for h in rows[0]]
    out = []
    for r in rows[1:]:
        if not r:
            continue
        d = {}
        for i, h in enumerate(head):
            d[h] = r[i].strip() if i < len(r) else ""
        out.append(d)
    return out


def write_csv(file_name, columns, rows):
    if sys.version_info[0] >= 3:
        f = open(file_name, "w", newline="", encoding="utf-8")
    else:
        f = open(file_name, "wb")
    try:
        w = csv.writer(f)
        w.writerow(columns)
        for r in rows:
            w.writerow([r.get(c, "") for c in columns])
    finally:
        f.close()


def parse_address(text):
    """0x438a79, 00438a79 or 438a79 -> int (None if it isn't one)."""
    t = (text or "").strip().lower()
    if t.startswith("0x"):
        t = t[2:]
    if not t:
        return None
    try:
        return int(t, 16)
    except ValueError:
        return None


def me(root, ask_string):
    """Who's working (the "by" column), asked once and kept in re/private/me.txt."""
    f = path(root, "re", "private", "me.txt")
    if os.path.exists(f):
        name = open(f).read().strip()
        if name:
            return name
    name = ask_string("zt1-engine", "Your name or handle, for the 'by' column of the functions you name:")
    out = open(f, "w")
    out.write(name.strip() + "\n")
    out.close()
    return name.strip()


def check_binary(program, ask_yes_no):
    """Warns when the open program isn't the zoo.exe re/ is for; True to carry on."""
    sha = program.getExecutableSHA256()
    if sha and sha.lower() == ZOO_SHA256:
        return True
    return ask_yes_no(
        "Different zoo.exe",
        "This program's SHA-256 is\n%s\nbut re/ is for the 2002 Complete Collection zoo.exe\n%s\n\n"
        "Addresses won't line up. Carry on anyway?" % (sha, ZOO_SHA256))


def class_of(func):
    """A function's class (its namespace, "A::B"), "" if it's global."""
    ns = func.getParentNamespace()
    if ns is None or ns.isGlobal():
        return ""
    return ns.getName(True)


def tag_value(func, prefix):
    """The rest of the first tag on func starting with prefix ("by:" -> "Batty251")."""
    for t in func.getTags():
        if t.getName().startswith(prefix):
            return t.getName()[len(prefix):]
    return ""
