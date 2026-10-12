"""Lists every zoo.exe address the engine's source cites, into re/engine_refs.csv.

The engine's comments say where each rule came from ("Hunger (zoo.exe
0x438a79): ..."). This gathers them so Ghidra (ZT1_EngineRefs.py) can mark
each one as done, and so what's left to do can be listed.

python re/scan_engine.py
"""
import csv, os, re, sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "src")
OUT = os.path.join(ROOT, "re", "engine_refs.csv")
# zoo.exe's code (.text), 2002 Complete Collection build
TEXT_LO, TEXT_HI = 0x401000, 0x62c824
ADDR = re.compile(r"\b0x([4-6][0-9a-fA-F]{5})\b")


def comment_blocks(lines):
    """(first line number, text) for each run of // lines (or a trailing // comment)."""
    i = 0
    while i < len(lines):
        line = lines[i]
        at = line.find("//")
        if at < 0:
            i += 1
            continue
        start = i
        text = [line[at + 2:].strip()]
        # a run of whole-line comments joins up
        if not line[:at].strip():
            while i + 1 < len(lines) and lines[i + 1].lstrip().startswith("//"):
                i += 1
                text.append(lines[i].lstrip()[2:].strip())
        yield start + 1, " ".join(t for t in text if t)
        i += 1


def scan():
    refs = []
    for folder, _, files in os.walk(SRC):
        for name in sorted(files):
            if not name.endswith((".cpp", ".hpp", ".h")):
                continue
            path = os.path.join(folder, name)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
            for first, text in comment_blocks(lines):
                for m in ADDR.finditer(text):
                    va = int(m.group(1), 16)
                    if not TEXT_LO <= va < TEXT_HI:
                        continue
                    # the line the address itself is on
                    line = first
                    for k in range(first - 1, min(len(lines), first + 40)):
                        if m.group(0).lower() in lines[k].lower():
                            line = k + 1
                            break
                    refs.append(("0x%06x" % va, "%s:%d" % (rel, line), text[:300]))
    refs.sort()
    return refs


def main():
    refs = scan()
    with open(OUT, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["address", "engine", "comment"])
        w.writerows(refs)
    print("%d references to %d addresses -> %s" % (len(refs), len({r[0] for r in refs}), os.path.relpath(OUT, ROOT)))


if __name__ == "__main__":
    sys.exit(main())
