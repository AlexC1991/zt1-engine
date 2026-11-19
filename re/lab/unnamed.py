# The zoo.exe functions that gameplay reaches but nobody has named yet.
#
# Starts at every function the knowledge base names in zoo.exe's gameplay
# (the same list re/lab/fidelity.py counts), follows each one's direct calls,
# and keeps the callees that have no name: not in re/knowledge.json, and
# still "FUN_xxxxxxxx" in Ghidra's list (re/private/atlas-functions.json). It
# also follows the callees' own calls (up to --depth) through functions that
# are unnamed, and the class tables' slots (re/private/class-*.json).
# The C runtime, the framework layers, drawing, sound and the developer's
# panels are left out as fidelity.py leaves them out.
#
#   python re/lab/unnamed.py [--out B:/zt1-shots/<x>/unnamed.md] [--json file] [--depth 3]
#
# (needs pefile and capstone, like re/rd.py; reads zoo.exe from the game's
# folder, or ZT1_EXE)
import argparse
import collections
import glob
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
sys.path.insert(0, HERE)
sys.path.insert(0, os.path.join(ROOT, "re"))
import capstone  # noqa: E402
import fidelity  # noqa: E402
import rd  # noqa: E402

PRIVATE = os.path.join(ROOT, "re", "private")
if not os.path.exists(os.path.join(PRIVATE, "atlas-functions.json")):
    # a worktree has no private folder: the main checkout's is the same data
    PRIVATE = os.path.join(os.path.dirname(os.path.dirname(os.path.dirname(ROOT))), "re", "private")

CALL = capstone.x86.X86_INS_CALL


def atlas():
    with open(os.path.join(PRIVATE, "atlas-functions.json"), encoding="utf-8") as fh:
        return {int(f["address"], 16): f for f in json.load(fh)["functions"]}


def callees(entry):
    """Direct call targets (and tail-jump targets outside the function) of one function."""
    seen, targets = rd.func(entry, limit=6000)
    out = collections.Counter()
    for va, i in seen.items():
        if i.mnemonic == "call" and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            out[i.operands[0].imm] += 1
        elif i.mnemonic == "jmp" and i.operands and i.operands[0].type == capstone.x86.X86_OP_IMM:
            t = i.operands[0].imm
            if t not in targets and fidelity.TEXT_LO <= t < fidelity.TEXT_HI:
                out[t] += 1
    return out


def reach(depth=3):
    """{address: {system, from, calls, depth, size}} of the unnamed functions gameplay reaches."""
    a = argparse.Namespace(depth=depth)
    with open(fidelity.KB, encoding="utf-8") as fh:
        kb = json.load(fh)["functions"]
    at = atlas()
    named = {int(k, 16) for k in kb}
    # a function the knowledge base notes "plumbing:" or "developer-only:" is settled too
    with open(fidelity.KB, encoding="utf-8") as fh:
        for addr, note in json.load(fh).get("notes", {}).items():
            text = (note.get("text") or "").strip().lower() if isinstance(note, dict) else ""
            if text.startswith(("plumbing:", "developer-only:")):
                named.add(int(addr, 16))
    lib = {va for va, f in at.items() if not f["name"].startswith("FUN_") and va not in named}

    # the gameplay roots, by system
    roots = collections.OrderedDict()
    for addr, e in sorted(kb.items()):
        s = fidelity.system_of(e.get("name", ""), e.get("class", ""))
        if s:
            roots[int(addr, 16)] = s
    # class-table slots that nobody has named, with the class's system
    slot_root = {}
    for p in glob.glob(os.path.join(PRIVATE, "class-*.json")):
        with open(p, encoding="utf-8") as fh:
            c = json.load(fh)
        cls = c.get("name", "")
        s = fidelity.system_of("", cls)
        if not s:
            continue
        for _, t in c.get("slots", {}).items():
            try:
                tv = int(t, 16)
            except (TypeError, ValueError):
                continue
            slot_root.setdefault(tv, (s, cls))

    found = {}       # address -> {"system", "from": set of caller names, "calls": n, "depth": d}
    cache = {}

    def visit(entry, system, via, depth):
        if depth > a.depth:
            return
        if entry not in cache:
            try:
                cache[entry] = callees(entry)
            except Exception:
                cache[entry] = collections.Counter()
        for t, n in cache[entry].items():
            if not (fidelity.TEXT_LO <= t < fidelity.TEXT_HI) or t in named or t in lib:
                continue
            f = at.get(t)
            if f is None or not f["name"].startswith("FUN_"):
                continue
            e = found.get(t)
            if e is None:
                e = found[t] = {"system": system, "from": set(), "calls": 0, "depth": depth, "size": f.get("size", 0)}
                first = True
            else:
                first = False
                if depth < e["depth"]:
                    e["depth"] = depth
            e["calls"] += n
            if len(e["from"]) < 6:
                e["from"].add(via)
            if first:
                visit(t, system, via, depth + 1)

    for va, s in roots.items():
        e = kb["0x%06x" % va]
        visit(va, s, (e.get("class") + "::" if e.get("class") else "") + e.get("name", ""), 1)
    for va, (s, cls) in slot_root.items():
        if va not in named and va not in lib and va in at and at[va]["name"].startswith("FUN_"):
            e = found.setdefault(va, {"system": s, "from": set(), "calls": 0, "depth": 0, "size": at[va].get("size", 0)})
            e["from"].add(cls + " vtable")
            e["calls"] += 1
            visit(va, s, cls + " vtable", 1)

    return found


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "re", "private", "unnamed.md"))
    ap.add_argument("--json", default="")
    ap.add_argument("--depth", type=int, default=3)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()
    found = reach(a.depth)
    by_sys = collections.defaultdict(list)
    for va, e in found.items():
        by_sys[e["system"]].append((va, e))
    rows = []
    with open(a.out, "w", encoding="utf-8") as fh:
        fh.write("# zoo.exe functions gameplay reaches that have no name yet\n\n")
        fh.write("Made by `python re/lab/unnamed.py` (depth %d). Each row: address, size in bytes, direct-call count,\n" % a.depth)
        fh.write("depth (1 = called by a named function), and some of its callers. Most-called first.\n\n")
        fh.write("| System | Unnamed reached |\n|---|---|\n")
        for s in sorted(by_sys, key=lambda k: -len(by_sys[k])):
            fh.write("| %s | %d |\n" % (s, len(by_sys[s])))
        fh.write("| **all** | %d |\n\n" % len(found))
        for s in sorted(by_sys, key=lambda k: -len(by_sys[k])):
            fh.write("## %s (%d)\n\n" % (s, len(by_sys[s])))
            for va, e in sorted(by_sys[s], key=lambda x: (-x[1]["calls"], x[0])):
                fh.write("- 0x%06x  %4d B  x%-3d d%d  %s\n" % (va, e["size"], e["calls"], e["depth"], ", ".join(sorted(e["from"])[:3])))
            fh.write("\n")
    if a.json:
        with open(a.json, "w", encoding="utf-8") as fh:
            json.dump({"count": len(found), "bySystem": {s: len(v) for s, v in by_sys.items()},
                       "functions": [{"address": "0x%06x" % va, "system": e["system"], "calls": e["calls"], "depth": e["depth"],
                                      "size": e["size"], "from": sorted(e["from"])} for va, e in sorted(found.items())]},
                      fh, indent=1)
    if not a.quiet:
        print("Unnamed functions gameplay reaches: %d" % len(found))
        for s in sorted(by_sys, key=lambda k: -len(by_sys[k])):
            print("  %-24s %d" % (s, len(by_sys[s])))
        print("->", a.out)


if __name__ == "__main__":
    main()
