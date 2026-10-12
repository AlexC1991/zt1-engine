# Puts everything in re/knowledge.json into Ghidra:
# - functions named (a provisional name - one we gave it till its real one
#   is known - tagged zt1:provisional), what they do as their plate comment
# - structs made in the Data Type Manager under /zt1, with every known field
# - notes as comments where they are
# - where functions were seen running (the Lab's recorder): plate comments
#   and zt1-seen bookmarks
# - named globals as labels (the Lab's loader mapper: cUIThreshold...), and
#   the classes' tables of functions in plate comments (the Lab's class tables)
# - questions and answers as bookmarks (zt1-question, zt1-answer)
# - optionally, each known number marked where the code uses it
# Names you set yourself are never changed: clashes are listed.
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Apply knowledge
#@runtime Jython

from __future__ import print_function
import sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db, zt1ghidra

from ghidra.program.model.listing import BookmarkType, CodeUnit
from ghidra.program.model.symbol import SourceType

MARK = "[zt1] "


def apply_functions(db, clashes):
    done = 0
    for key, e in sorted(db["functions"].items()):
        va = zt1db.parse_int(key)
        f = zt1ghidra.function_at(currentProgram, monitor, toAddr(va))
        if f is None:
            print("No function at %s for %s" % (key, e.get("name")))
            continue
        name, cls = e.get("name", ""), e.get("class", "")
        src = f.getSymbol().getSource()
        mine = src == SourceType.DEFAULT or zt1ghidra.has_tag(f, "zt1:provisional") or \
            zt1ghidra.has_tag(f, "src:zt1-engine")
        if name and (f.getName() != name or zt1re.class_of(f) != cls):
            if mine:
                f.setName(name, SourceType.IMPORTED)
                f.setParentNamespace(zt1ghidra.namespace_for(currentProgram, cls))
            elif e.get("provisional"):
                pass  # (a real name - someone's - beats one we made up)
            else:
                clashes.append("%s: kept %s (the knowledge base says %s)" % (key, f.getName(True), name))
        if mine:
            if e.get("provisional"):
                f.addTag("zt1:provisional")
            else:
                f.removeTag("zt1:provisional")
            f.addTag("src:zt1-engine")
        plate = zt1ghidra.knowledge_plate(db, va)
        old = f.getComment() or ""
        kept = [l for l in old.split("\n") if l and not l.startswith(MARK)]
        f.setComment("\n".join(kept + [MARK + l for l in plate.split("\n") if l]))
        if e.get("signature"):
            zt1ghidra.apply_signature(currentProgram, monitor, f, e["signature"])
        done += 1
    return done


def apply_structs(db):
    dtm = currentProgram.getDataTypeManager()
    # (all made first, so a field can point at another)
    for name in db["structs"]:
        zt1ghidra.struct(dtm, name)
    n, bad = 0, []
    for name, s in sorted(db["structs"].items()):
        st = zt1ghidra.struct(dtm, name)
        if s.get("summary"):
            st.setDescription(s["summary"])
        zt1ghidra.at_least(st, s.get("size", 0))
        for key, fld in sorted(s["fields"].items(), key=lambda kv: zt1db.parse_int(kv[0])):
            err = zt1ghidra.set_field(dtm, st, zt1db.parse_int(key), fld)
            if err:
                bad.append("%s+%s %s: %s" % (name, key, fld.get("name"), err))
            else:
                n += 1
    return n, bad


def apply_notes(db):
    for key, note in db["notes"].items():
        addr = toAddr(zt1db.parse_int(key))
        old = getPreComment(addr) or ""
        kept = [l for l in old.split("\n") if l and not l.startswith(MARK)]
        setPreComment(addr, "\n".join(kept + [MARK + note["text"] + "  (" + note.get("by", "") + ")"]))
    return len(db["notes"])


def apply_seen(db):
    """Where each function was seen running in the original (the Lab's
    recorder): "Ran when: ..." in its plate comment - with what's known of it,
    for one that isn't in the knowledge base's functions (those got theirs
    above) - and a zt1-seen bookmark, for Ghidra's Bookmarks window."""
    bm = currentProgram.getBookmarkManager()
    bm.removeBookmarks(currentProgram.getMemory(), BookmarkType.NOTE, "zt1-seen", monitor)
    n = 0
    for key in db["seen"]:
        va = zt1db.parse_int(key)
        f = zt1ghidra.function_at(currentProgram, monitor, toAddr(va))
        if f is None:
            continue
        if key not in db["functions"]:
            old = f.getComment() or ""
            kept = [l for l in old.split("\n") if l and not l.startswith(MARK)]
            plate = zt1ghidra.knowledge_plate(db, va)
            f.setComment("\n".join(kept + [MARK + l for l in plate.split("\n") if l]))
        bm.setBookmark(f.getEntryPoint(), BookmarkType.NOTE, "zt1-seen", zt1db.seen_text(db, va)[:200])
        n += 1
    return n


def apply_globals(db):
    """Each named global as a label where it is (the code's DAT_00639024
    reads cUIThreshold), its summary as a comment beside it."""
    st = currentProgram.getSymbolTable()
    n = 0
    for key, g in sorted(db["globals"].items()):
        addr = toAddr(zt1db.parse_int(key))
        name = g.get("name", "")
        if not name:
            continue
        mine = [s for s in st.getSymbols(addr) if s.getName() == name]
        if not mine:
            try:
                s = st.createLabel(addr, name, SourceType.IMPORTED)
                s.setPrimary()
            except:  # noqa: E722 (a name Ghidra won't take)
                print("Couldn't label %s %s" % (key, name))
                continue
        if g.get("summary"):
            old = getEOLComment(addr) or ""
            kept = [l for l in old.split("\n") if l and not l.startswith(MARK)]
            setEOLComment(addr, "\n".join(kept + [MARK + g["summary"] + "  (" + g.get("by", "") + ")"]))
        n += 1
    return n


def apply_classes(db):
    """Functions in a known class's table of functions: what's known of them
    in their plate comment (those with no entry of their own - the rest got
    theirs above)."""
    done = set(db["functions"].keys()) | set(db["seen"].keys())
    n = 0
    for name, c in db["classes"].items():
        for slot, fa in c.get("slots", {}).items():
            va = zt1db.parse_int(fa)
            key = zt1db.addr_key(va)
            if key in done:
                continue
            done.add(key)
            f = zt1ghidra.function_at(currentProgram, monitor, toAddr(va))
            if f is None:
                continue
            old = f.getComment() or ""
            kept = [l for l in old.split("\n") if l and not l.startswith(MARK)]
            plate = zt1ghidra.knowledge_plate(db, va)
            f.setComment("\n".join(kept + [MARK + l for l in plate.split("\n") if l]))
            n += 1
    return n


def apply_questions(db):
    bm = currentProgram.getBookmarkManager()
    for cat in ("zt1-question", "zt1-answer", "zt1-lead"):
        bm.removeBookmarks(currentProgram.getMemory(), BookmarkType.NOTE, cat, monitor)
    n = 0
    for q in db["questions"]:
        va = zt1db.parse_int(q.get("address"))
        if va is None:
            continue
        if q["status"] == "answered":
            cat, text = "zt1-answer", "%s %s -> %s (%s)" % (q["id"], q["question"], q["answer"], q["answered_by"])
        else:
            cat = "zt1-question" if q.get("for") == "assistant" else "zt1-lead"
            text = "%s %s (%s)" % (q["id"], q["question"], q["by"])
        createBookmark(toAddr(va), cat, text[:900])
        n += 1
    return n


def mark_constants(db):
    """Each known number's meaning as an end-of-line comment where it's used."""
    known = dict((zt1db.parse_int(k), v["meaning"]) for k, v in db["constants"].items())
    known.pop(None, None)
    n = 0
    listing = currentProgram.getListing()
    for ins in listing.getInstructions(True):
        if monitor.isCancelled():
            break
        for i in range(ins.getNumOperands()):
            sc = ins.getScalar(i)
            if sc is None:
                continue
            v = sc.getUnsignedValue()
            if v in known:
                old = ins.getComment(CodeUnit.EOL_COMMENT) or ""
                if not old or old.startswith(MARK):
                    ins.setComment(CodeUnit.EOL_COMMENT, MARK + "%d: %s" % (v, known[v]))
                    n += 1
    return n


def make_functions(root):
    """The functions the Lab made (re/made_functions.json: code Ghidra's
    analysis missed, reached only through a table), made here for good - first,
    so names in the knowledge base can land on them."""
    import io
    import json
    import os
    from ghidra.app.cmd.function import CreateFunctionCmd
    try:
        with io.open(os.path.join(str(root), "re", "made_functions.json"), encoding="utf-8") as fh:
            made = json.load(fh)
    except Exception:  # noqa: BLE001 (none made yet)
        return 0
    fm = currentProgram.getFunctionManager()
    space = currentProgram.getAddressFactory().getDefaultAddressSpace()
    n = 0
    for m in made:
        a = space.getAddress(int(m["addr"], 16))
        if fm.getFunctionAt(a) is None and CreateFunctionCmd(a).applyTo(currentProgram, monitor):
            fm.getFunctionAt(a).addTag("zt1:made")
            n += 1
    return n


def main():
    root = zt1re.repo_root(getSourceFile())
    if not zt1re.check_binary(currentProgram, askYesNo):
        return
    db = zt1db.load(root)
    numbers = askYesNo("Known numbers", "Also mark every known number (%d, e.g. message ids) where the code "
                       "uses it? It goes through the whole program: a minute or two." % len(db["constants"]))
    clashes = []
    nm = make_functions(root)
    if nm:
        print("Made %d functions the Lab found (re/made_functions.json)" % nm)
    nf = apply_functions(db, clashes)
    ns, bad = apply_structs(db)
    nn = apply_notes(db)
    nseen = apply_seen(db)
    ng = apply_globals(db)
    ncl = apply_classes(db)
    nq = apply_questions(db)
    nc = mark_constants(db) if numbers else 0
    for c in clashes + bad:
        print(c)
    print("Applied: %d functions, %d struct fields, %d notes, %d seen running, %d globals, %d in class tables, "
          "%d questions/leads%s" % (nf, ns, nn, nseen, ng, ncl, nq, ", %d uses of known numbers" % nc if numbers else ""))


main()
