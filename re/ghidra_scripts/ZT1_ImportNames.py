# Names zoo.exe's functions from someone else's CSV: OpenZT's Ghidra project
# (their function_exporter.py's "Class, Name,Address,...", or ZT1_Sync's
# re/private/all_names.csv run on their project).
# Each function named gets a tag saying where its name came from
# (src:openzt), so their work and ours stay told apart. A provisional name
# (one we gave a function till its real one was known) gives way to theirs,
# and the knowledge base notes whose name it now is. Names you set yourself
# are never overwritten unless you say so.
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Import names
#@runtime Jython

from __future__ import print_function
import sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db, zt1ghidra

from ghidra.program.model.symbol import SourceType


def main():
    root = zt1re.repo_root(getSourceFile())
    if not zt1re.check_binary(currentProgram, askYesNo):
        return
    csv_file = askFile("CSV of function names (e.g. OpenZT's export, in re/private/)", "Import")
    rows = zt1re.read_csv(csv_file.getAbsolutePath())
    if not rows or "address" not in rows[0] or "name" not in rows[0]:
        popup("That CSV has no Address and Name columns.")
        return
    source = askString("Where are these names from?", "A short label (it becomes the tag src:<label>):", "openzt")
    overwrite = askYesNo("Overwrite?", "Rename functions you (or an earlier import) already named?\n"
                         "No keeps every name already set (bar provisional ones); clashes are listed.")
    db = zt1db.load(root)

    named, same, kept, missing, real = 0, 0, [], [], 0
    for row in rows:
        va = zt1re.parse_address(row.get("address"))
        name = row.get("name", "")
        if va is None or not zt1re.is_named(name):
            continue
        addr = toAddr(va)
        f = zt1ghidra.function_at(currentProgram, monitor, addr)
        if f is None:
            missing.append("%s %s" % (addr, name))
            continue
        cls = row.get("class", "").strip()
        if f.getName() == name and zt1re.class_of(f) == cls:
            same += 1
        elif f.getSymbol().getSource() == SourceType.DEFAULT or zt1ghidra.has_tag(f, "zt1:provisional") or overwrite:
            f.setName(name, SourceType.IMPORTED)
            f.setParentNamespace(zt1ghidra.namespace_for(currentProgram, cls))
            named += 1
        else:
            kept.append("%s: kept %s (the CSV says %s%s)" % (addr, f.getName(True), cls + "::" if cls else "", name))
            continue
        for t in list(f.getTags()):
            if t.getName().startswith("src:") or t.getName() == "zt1:provisional":
                f.removeTag(t.getName())
        f.addTag("src:" + source)
        if row.get("by"):
            f.addTag("by:" + row["by"])
        if row.get("signature") and not f.getComment():
            f.setComment("Signature (%s): %s" % (source, row["signature"]))
        e = db["functions"].get(zt1db.addr_key(va))
        if e and e.get("provisional"):
            zt1db.log(db, source, "Real name for %s: %s%s (was provisional %s)"
                      % (zt1db.addr_key(va), cls + "::" if cls else "", name, e.get("name")))
            e.update(name=name, provisional=False, name_by=source)
            e["class"] = cls
            real += 1

    if real:
        zt1db.save(root, db)
    for k in kept:
        print(k)
    for m in missing:
        print("No function could be made at " + m)
    print("Named %d, %d already so, %d kept as you had them, %d missing - tagged src:%s. "
          "%d provisional names replaced in the knowledge base." % (named, same, len(kept), len(missing), source, real))


main()
