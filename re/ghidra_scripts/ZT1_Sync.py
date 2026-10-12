# Brings what you've done straight in Ghidra (not through Map this) into the
# knowledge base, re/knowledge.json, under your name:
# - functions you renamed (L): a provisional name you replace becomes yours
# - your own plate comments on functions the knowledge base knows
# - signatures you set (Ctrl+L on a function's name, or Edit Function)
# - fields you named or retyped in the /zt1 structs (Data Type Manager)
# Optionally every named function, OpenZT's too, to re/private/all_names.csv
# (not committed).
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Sync to knowledge base
#@runtime Jython

from __future__ import print_function
import sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db, zt1ghidra

from ghidra.program.model.data import Structure
from ghidra.program.model.symbol import SourceType

MARK = "[zt1] "


def sync_functions(db, me):
    changes = 0
    for f in currentProgram.getFunctionManager().getFunctions(True):
        if f.isThunk() or f.isExternal():
            continue
        va = f.getEntryPoint().getOffset()
        key = zt1db.addr_key(va)
        e = db["functions"].get(key)
        if f.getSymbol().getSource() == SourceType.USER_DEFINED and zt1re.is_named(f.getName()) and \
                zt1re.class_of(f).find("switchD") < 0:
            cls = zt1re.class_of(f)
            if not e or e.get("name") != f.getName() or e.get("class", "") != cls:
                old = e.get("name") if e else None
                e = zt1db.set_function(db, va, me, name=f.getName(), provisional=False)
                e["class"] = cls
                e.setdefault("confidence", "likely")
                f.removeTag("zt1:provisional")
                for t in list(f.getTags()):
                    if t.getName().startswith("src:") or t.getName().startswith("by:"):
                        f.removeTag(t.getName())
                f.addTag("src:zt1-engine")
                f.addTag("by:" + me)
                zt1db.log(db, me, "Named %s %s%s%s" % (key, cls + "::" if cls else "", f.getName(),
                                                       " (was %s)" % old if old and old != f.getName() else ""))
                changes += 1
        if not e:
            continue
        own = "\n".join(l for l in (f.getComment() or "").split("\n") if l.strip() and not l.startswith(MARK))
        if own and own != e.get("comment", ""):
            e["comment"], e["comment_by"] = own, me
            zt1db.log(db, me, "Comment on %s: %s" % (key, own.split("\n")[0]))
            changes += 1
        if f.getSignatureSource() == SourceType.USER_DEFINED:
            sig = f.getSignature().getPrototypeString()
            if sig != e.get("signature"):
                e["signature"] = sig
                zt1db.log(db, me, "Signature of %s: %s" % (key, sig))
                changes += 1
    return changes


def sync_structs(db, me):
    changes = 0
    dtm = currentProgram.getDataTypeManager()
    cat = dtm.getCategory(zt1ghidra.CATEGORY)
    if cat is None:
        return 0
    for dt in cat.getDataTypes():
        if not isinstance(dt, Structure):
            continue
        s = db["structs"].setdefault(dt.getName(), {"summary": dt.getDescription() or "", "fields": {}})
        if not dt.isZeroLength() and dt.getLength() > s.get("size", 0):
            s["size"] = dt.getLength()  # (grown in Ghidra: kept)
        for c in dt.getDefinedComponents():
            name = c.getFieldName()
            if not name:
                continue
            key = zt1db.off_key(c.getOffset())
            type_name = c.getDataType().getDisplayName()
            old = s["fields"].get(key)
            if old and old.get("name") == name and old.get("type") == type_name:
                continue
            summary = old.get("summary", "") if old else (c.getComment() or "")
            zt1db.set_field(db, dt.getName(), c.getOffset(), me, name, type_name, summary,
                            old.get("confidence", "likely") if old else "likely")
            zt1db.log(db, me, "Field %s+%s = %s (%s)" % (dt.getName(), key, name, type_name))
            changes += 1
    return changes


def all_names(root):
    rows = []
    for f in currentProgram.getFunctionManager().getFunctions(True):
        if f.isThunk() or f.isExternal() or not zt1re.is_named(f.getName()):
            continue
        if zt1re.class_of(f).startswith("switchD"):
            continue
        src = zt1re.tag_value(f, "src:") or (
            "zt1-engine" if f.getSymbol().getSource() == SourceType.USER_DEFINED else "ghidra")
        rows.append({"address": "0x%06x" % f.getEntryPoint().getOffset(), "class": zt1re.class_of(f),
                     "name": f.getName(), "signature": f.getSignature().getPrototypeString(),
                     "by": zt1re.tag_value(f, "by:") or src, "source": src})
    zt1re.write_csv(zt1re.path(root, "re", "private", "all_names.csv"),
                    ["address", "class", "name", "signature", "by", "source"], rows)
    return len(rows)


def main():
    root = zt1re.repo_root(getSourceFile())
    if not zt1re.check_binary(currentProgram, askYesNo):
        return
    me = zt1re.me(root, askString)
    everything = askYesNo("Everything too?", "Also write every named function, OpenZT's included, "
                          "to re/private/all_names.csv (not committed)? rd.py uses it for names.")
    db = zt1db.load(root)
    nf = sync_functions(db, me)
    ns = sync_structs(db, me)
    zt1db.save(root, db)
    print("Synced %d function changes and %d struct fields to re/knowledge.json as %s" % (nf, ns, me))
    if everything:
        print("%d named functions -> re/private/all_names.csv" % all_names(root))


main()
