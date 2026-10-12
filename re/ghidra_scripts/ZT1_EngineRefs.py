# Marks in Ghidra every zoo.exe address the engine's source cites, so you can
# see what's done: a bookmark (category zt1-engine, in Window > Bookmarks)
# at each, saying which file and line, and the function holding it tagged
# zt1:implemented (Window > Function Tags).
#
# Then lists what's left: named functions no engine code cites, biggest
# first, in re/private/todo.txt. Re-run after re/scan_engine.py whenever the
# engine changes.
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Mark engine references
#@runtime Jython

from __future__ import print_function
import sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db

from ghidra.program.model.listing import BookmarkType
from ghidra.program.model.symbol import SourceType

CATEGORY = "zt1-engine"
DONE = "zt1:implemented"


def main():
    root = zt1re.repo_root(getSourceFile())
    if not zt1re.check_binary(currentProgram, askYesNo):
        return
    refs = zt1re.read_csv(zt1re.path(root, "re", "engine_refs.csv"))
    if not refs:
        popup("re/engine_refs.csv is empty or missing: run  python re/scan_engine.py  in the repo first.")
        return

    # (last run's marks off: the engine may have moved on)
    bm = currentProgram.getBookmarkManager()
    bm.removeBookmarks(currentProgram.getMemory(), BookmarkType.NOTE, CATEGORY, monitor)
    for f in currentProgram.getFunctionManager().getFunctions(True):
        f.removeTag(DONE)

    done, loose, files = set(), [], {}
    for ref in refs:
        va = zt1re.parse_address(ref.get("address"))
        if va is None:
            continue
        addr = toAddr(va)
        createBookmark(addr, CATEGORY, "%s  %s" % (ref.get("engine", ""), ref.get("comment", "")[:200]))
        f = getFunctionContaining(addr)
        if f is None:
            loose.append("%s (%s): in no function yet - press F there to make one" % (addr, ref.get("engine")))
            continue
        f.addTag(DONE)
        done.add(f.getEntryPoint().getOffset())
        where = files.setdefault(f.getEntryPoint().getOffset(), [])
        if ref.get("engine") and ref["engine"] not in where:
            where.append(ref["engine"])

    # (the knowledge base's functions: which engine code implements each)
    db = zt1db.load(root)
    for key, e in db["functions"].items():
        now = sorted(files.get(zt1db.parse_int(key), []))
        if now != e.get("engine", []):
            e["engine"] = now
    zt1db.save(root, db)

    todo = []
    total = 0
    for f in currentProgram.getFunctionManager().getFunctions(True):
        if f.isThunk() or f.isExternal():
            continue
        total += 1
        # (not the library functions Ghidra's analysis named: the C runtime,
        # DirectX... the engine has its own)
        if f.getSymbol().getSource() == SourceType.ANALYSIS:
            continue
        if zt1re.is_named(f.getName()) and f.getEntryPoint().getOffset() not in done:
            src = zt1re.tag_value(f, "src:") or "?"
            todo.append((f.getBody().getNumAddresses(), f.getEntryPoint(), f.getName(True), src))
    todo.sort(key=lambda t: (-t[0], t[1].getOffset()))
    out = open(zt1re.path(root, "re", "private", "todo.txt"), "w")
    out.write("Named in Ghidra, not cited by the engine yet (bytes, address, name, whose name):\n")
    for size, addr, name, src in todo:
        out.write("%6d  %s  %s  [%s]\n" % (size, addr, name, src))
    out.close()

    for l in loose:
        print(l)
    print("%d engine references: %d functions marked %s, out of %d. %d named ones left -> re/private/todo.txt"
          % (len(refs), len(done), DONE, total, len(todo)))


main()
