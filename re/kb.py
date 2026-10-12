"""The knowledge base (re/knowledge.json) from the command line: look things
up, answer questions, add what's been worked out without Ghidra open.
Anything added is under --by (default: your name in re/private/me.txt).
Run ZT1_Apply in Ghidra afterwards to see it there.

python re/kb.py stats
python re/kb.py show 0x4378df | ZTAnimalType | 10560
python re/kb.py questions [--all]
python re/kb.py answer q3 "It's the happiness cap" [--by batty251]
python re/kb.py lead 0x4378df "What's species+0x22c?"      (a question for you to chase)
python re/kb.py ask 0x4378df "What's this loop for?"        (a question for your AI assistant)
python re/kb.py func 0x4378df updateHappiness "What it does" [--class ZTAnimal] [--confidence likely] [--provisional]
python re/kb.py field ZTAnimal 0x2a8 happiness int "What it is" [--confidence likely]
python re/kb.py const 10560 "Message: ..." [--kind message]
python re/kb.py size ZTAnimal 0x400                         (a struct's size, at least)
python re/kb.py note 0x5cda56 "The 'deeper tank' refusal"
python re/kb.py seen 0x4378df "Placing a fence"             (seen running then, in the original)
python re/kb.py seen-file re/private/recordings/<file>.json  (a Lab recording: every function in it)
python re/kb.py global 0x639024 cUIThreshold "From guests.ai/animals.ai"  (a global, a label in Ghidra)
python re/kb.py source 0x52f1d0 gx/gxvidman.cpp               (the original's source file of a named function)
python re/kb.py loader-file <file>.json   (the Lab's loader mapper: a loader's fields and globals)
python re/kb.py class-file <file>.json    (the Lab's class tables: a class's table of functions)
python re/kb.py text-file <file>.json     (names and source files from the game's own text)
python re/kb.py log [n]
"""
import argparse, os, sys

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "ghidra_scripts"))
import zt1db  # noqa: E402


def default_by():
    p = os.path.join(HERE, "private", "me.txt")
    return open(p).read().strip() if os.path.exists(p) else "unknown"


def show(db, what):
    va = zt1db.parse_int(what) if what.lower().startswith("0x") else None
    if va is not None:
        k = zt1db.addr_key(va)
        if k in db["functions"]:
            print(zt1db.describe_function(db, va))
            e = db["functions"][k]
            if e.get("comment"):
                print("%s  (%s)" % (e["comment"], e.get("comment_by")))
            if e.get("signature"):
                print("Signature: " + e["signature"])
        if k in db["notes"]:
            n = db["notes"][k]
            print("Note: %s (%s, %s)" % (n["text"], n["by"], n.get("on")))
        if db["seen"].get(k):
            print(zt1db.seen_text(db, va))
        for q in db["questions"]:
            if zt1db.parse_int(q.get("address")) == va:
                print("%s [%s, for %s] %s%s" % (q["id"], q["status"], q["for"], q["question"],
                                                " -> " + q["answer"] if q["answer"] else ""))
        return
    if what in db["structs"]:
        s = db["structs"][what]
        print("%s: %s%s" % (what, s.get("summary", ""), "  (size 0x%x)" % s["size"] if s.get("size") else ""))
        for k, f in sorted(s["fields"].items(), key=lambda kv: zt1db.parse_int(kv[0])):
            print("  +%s %-28s %-16s %s (%s, %s)" % (k, f["name"], f["type"], f.get("summary", ""), f.get("confidence"),
                                                    f.get("by")))
        return
    if what in db["constants"]:
        c = db["constants"][what]
        print("%s: %s [%s] (%s)" % (what, c["meaning"], c.get("kind"), c.get("by")))
        return
    # a name or a field name anywhere
    w = what.lower()
    for k, e in sorted(db["functions"].items()):
        if w in e.get("name", "").lower():
            print("%s %s" % (k, e.get("name")))
    for sn, s in sorted(db["structs"].items()):
        for k, f in s["fields"].items():
            if w in f["name"].lower():
                print("%s+%s %s" % (sn, k, f["name"]))


def main():
    p = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    p.add_argument("command")
    p.add_argument("args", nargs="*")
    p.add_argument("--by", default=None)
    p.add_argument("--all", action="store_true")
    p.add_argument("--class", dest="cls", default=None)
    p.add_argument("--confidence", default="likely", choices=zt1db.CONFIDENCE)
    p.add_argument("--kind", default="other")
    p.add_argument("--provisional", action="store_true")
    a = p.parse_args()
    by = a.by or default_by()
    db = zt1db.load(ROOT)
    c, args = a.command, a.args
    changed = True
    if c == "stats":
        changed = False
        print("%d functions (%d provisional names), %d structs with %d fields, %d numbers, %d notes"
              % (len(db["functions"]), sum(1 for e in db["functions"].values() if e.get("provisional")),
                 len(db["structs"]), sum(len(s["fields"]) for s in db["structs"].values()), len(db["constants"]),
                 len(db["notes"])))
        print("%d questions open for the AI assistant, %d leads open, %d answered" % (
            sum(1 for q in db["questions"] if q["status"] == "open" and q["for"] == "assistant"),
            sum(1 for q in db["questions"] if q["status"] == "open" and q["for"] == "user"),
            sum(1 for q in db["questions"] if q["status"] == "answered")))
    elif c == "show":
        changed = False
        show(db, args[0])
    elif c == "questions":
        changed = False
        for q in db["questions"]:
            if a.all or q["status"] == "open":
                print("%s [%s, for %s] %s %s: %s (%s)%s" % (q["id"], q["status"], q["for"], q["address"], q["function"],
                                                           q["question"], q["by"],
                                                           "\n    -> %s (%s)" % (q["answer"], q["answered_by"])
                                                           if q["answer"] else ""))
    elif c == "answer":
        q = zt1db.answer(db, args[0], by, args[1])
        if not q:
            sys.exit("No question " + args[0])
        zt1db.log(db, by, "Answered %s: %s" % (args[0], args[1]))
    elif c in ("lead", "ask"):
        va = zt1db.parse_int(args[0])
        e = db["functions"].get(zt1db.addr_key(va), {}) if va is not None else {}
        q = zt1db.ask(db, by, va, e.get("name", ""), args[1], who="user" if c == "lead" else "assistant")
        zt1db.log(db, by, "%s %s at %s: %s" % ("Lead" if c == "lead" else "Asked", q["id"], q["address"], args[1]))
        print(q["id"])
    elif c == "func":
        va = zt1db.parse_int(args[0])
        e = zt1db.set_function(db, va, by, name=args[1], summary=args[2] if len(args) > 2 else None,
                               confidence=a.confidence, provisional=a.provisional)
        if a.cls is not None:
            e["class"] = a.cls
        zt1db.log(db, by, "Named %s %s" % (zt1db.addr_key(va), args[1]))
    elif c == "field":
        zt1db.set_field(db, args[0], zt1db.parse_int(args[1]), by, args[2], args[3] if len(args) > 3 else "int",
                        args[4] if len(args) > 4 else "", a.confidence)
        zt1db.log(db, by, "Field %s+%s = %s" % (args[0], args[1], args[2]))
    elif c == "size":
        s = db["structs"].setdefault(args[0], {"summary": "", "fields": {}})
        s["size"] = zt1db.parse_int(args[1])
        zt1db.log(db, by, "Size of %s = %s" % (args[0], args[1]))
    elif c == "const":
        zt1db.set_constant(db, int(args[0], 0), by, args[1], a.kind)
        zt1db.log(db, by, "Number %s = %s" % (args[0], args[1]))
    elif c == "note":
        zt1db.set_note(db, zt1db.parse_int(args[0]), by, args[1])
        zt1db.log(db, by, "Note at %s: %s" % (args[0], args[1]))
    elif c == "seen":
        zt1db.add_seen(db, zt1db.parse_int(args[0]), by, args[1])
        zt1db.log(db, by, "Seen %s running: %s" % (args[0], args[1]))
    elif c == "seen-file":
        import json
        rec = json.load(open(args[0], encoding="utf-8"))
        for h in rec["functions"]:
            zt1db.add_seen(db, zt1db.parse_int(h["address"]), by, rec["label"], zt1db.parse_int(h.get("from") or ""))
        zt1db.log(db, by, "Seen %d functions running: %s" % (len(rec["functions"]), rec["label"]))
        print("%d functions seen running: %s" % (len(rec["functions"]), rec["label"]))
    elif c == "global":
        e = zt1db.set_global(db, zt1db.parse_int(args[0]), by, args[1], args[2] if len(args) > 2 else "", a.kind or "")
        if e is None:
            sys.exit("%s is named already (by someone else): kb.py show it" % args[0])
        zt1db.log(db, by, "Global %s = %s" % (args[0], args[1]))
    elif c == "source":
        e = db["functions"].get(zt1db.addr_key(zt1db.parse_int(args[0])))
        if not e:
            sys.exit("Name %s first (kb.py func): a source file goes on a named function" % args[0])
        e["source"] = args[1]
        zt1db.log(db, by, "Source of %s: %s" % (args[0], args[1]))
    elif c == "loader-file":
        # A loader the Lab mapped: {loader, struct, file, fields [{off, name,
        # type, summary}], globals [{addr, name, summary}]} - fields only where
        # nothing's named yet, globals likewise
        import json
        m = json.load(open(args[0], encoding="utf-8"))
        nf = ng = 0
        for f in m.get("fields", []):
            nf += zt1db.add_field_if_new(db, m["struct"], zt1db.parse_int(f["off"]), by, f["name"], f.get("type", "int"),
                                         f.get("summary", ""), a.confidence or "sure") is not None
        for g in m.get("globals", []):
            va = zt1db.parse_int(g["addr"])
            if zt1db.addr_key(va) not in db["globals"]:
                ng += zt1db.set_global(db, va, by, g["name"], g.get("summary", ""), "key") is not None
        if m.get("loader"):
            lva = zt1db.parse_int(m["loader"])
            db["loaders"][zt1db.addr_key(lva)] = {"struct": m.get("struct", ""), "file": m.get("file", ""), "by": by}
            # (the loader itself named, where nobody has: ZTAnimalType::loadConfig
            # - loadConfig2... for a struct's other loaders - or, globals only,
            # load<Name>)
            if m.get("struct") and zt1db.addr_key(lva) not in db["functions"]:
                st = m["struct"]
                if m.get("fields"):
                    taken = {e.get("name") for e in db["functions"].values() if e.get("class") == st}
                    name, n = "loadConfig", 1
                    while name in taken:
                        n += 1
                        name = "loadConfig%d" % n
                    e = zt1db.set_function(db, lva, by, name=name, confidence="likely", provisional=True,
                                           summary="Reads %s's config keys (%s...) into its fields" % (
                                               st, ", ".join(f["name"] for f in m["fields"][:3])))
                    e["class"] = st
                else:
                    keys = [g["name"] for g in m.get("globals", [])[:3]] or m.get("keys", [])[:3]
                    zt1db.set_function(db, lva, by, name="load" + st, confidence="likely", provisional=True,
                                       summary="Reads config keys (%s...) into %s" % (
                                           ", ".join(keys), "globals" if m.get("globals") else "what it sets up"))
        zt1db.log(db, by, "Loader %s: %d fields of %s, %d globals" % (m.get("loader"), nf, m.get("struct"), ng))
        print("%d new fields of %s, %d new globals" % (nf, m.get("struct"), ng))
    elif c == "class-file":
        # A class's table of functions: {name, vtable, slots {"0x0": "0x43aedc"}}
        import json
        m = json.load(open(args[0], encoding="utf-8"))
        db["classes"][m["name"]] = {"vtable": m["vtable"], "slots": m["slots"], "summary": m.get("summary", ""), "by": by}
        zt1db.log(db, by, "Class %s: %d slots (table %s)" % (m["name"], len(m["slots"]), m["vtable"]))
        print("%s: %d slots" % (m["name"], len(m["slots"])))
    elif c == "text-file":
        # Names and source files the game's own text gives: [{address, name,
        # cls, source, summary}] - provisional names only where none's set,
        # source files on any function (named, or named now)
        import json
        rows = json.load(open(args[0], encoding="utf-8"))
        nn = ns = 0
        for r in rows:
            va = zt1db.parse_int(r["address"])
            k = zt1db.addr_key(va)
            e = db["functions"].get(k)
            if not e and r.get("name"):
                e = zt1db.set_function(db, va, by, name=r["name"], summary=r.get("summary", ""), confidence=a.confidence or "likely",
                                       provisional=True)
                if r.get("cls"):
                    e["class"] = r["cls"]
                nn += 1
            if not e and r.get("source"):
                # (no name, but its file: gxcanvas_4014f5 till it has one - in
                # Ghidra, a function's module at a glance)
                stem = os.path.splitext(os.path.basename(r["source"]))[0].lower()
                e = zt1db.set_function(db, va, by, name="%s_%x" % (stem, va), confidence="guess", provisional=True,
                                       summary="In the original's %s (its own text gives the path); not named yet" % r["source"])
                nn += 1
            if e and r.get("source") and not e.get("source"):
                e["source"] = r["source"]
                ns += 1
        zt1db.log(db, by, "From the game's text: %d names, %d source files" % (nn, ns))
        print("%d names, %d source files" % (nn, ns))
    elif c == "log":
        changed = False
        for l in db["log"][-int(args[0]) if args else -20:]:
            print("%s %s: %s" % (l["on"], l["by"], l["what"]))
    else:
        sys.exit(__doc__)
    if changed:
        zt1db.save(ROOT, db)
        print("Saved (by %s). Run ZT1_Apply in Ghidra to see it there." % by)


if __name__ == "__main__":
    main()
