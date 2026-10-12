# The knowledge base: re/knowledge.json, everything worked out about zoo.exe.
# Shared by the Ghidra scripts (Jython, Python 2.7) and re/kb.py (Python 3),
# so it's written for both.
#
#   functions  "0x4378df": name, class, summary, engine, by, confidence,
#              provisional (a name we gave it till its real one is known)
#   structs    "ZTAnimalType": summary, fields {"0x20c": name, type, summary, by}
#   constants  "10560": meaning, kind (message, config...), by
#   notes      "0x5cda56": text, by - something about one spot in the code
#   globals    "0x639024": name, summary, by, kind - a global the code keeps a
#              value in (cUIThreshold, from a config key: kind "key")
#   loaders    "0x4c4cb9": struct, file, by - a config loader and the struct
#              its keys go into (the Lab's loader mapper)
#   classes    "ZTGuest": vtable, slots {"0x268": "0x43aedc"}, by - a class's
#              table of functions (the Lab's class tables)
#   functions also take: source ("gx/gxvidman.cpp", the original's file,
#              from its text)
#   seen       "0x4378df": [{when, on, by, from}] - when it was seen running in
#              the original game (the Lab's recorder, Play original): what was
#              being done, and the function it was first called from
#   questions  [{id, for (assistant|user), address, function, question, by, on,
#                status (open|answered), answer, answered_by, answered_on}]
#   log        [{on, by, what}] - who added what, when
from __future__ import print_function
import io, json, os, time

CONFIDENCE = ["sure", "likely", "guess"]


def db_path(root):
    return os.path.join(root, "re", "knowledge.json")


def empty():
    return {"functions": {}, "structs": {}, "constants": {}, "notes": {}, "seen": {}, "globals": {}, "loaders": {},
            "classes": {}, "questions": [], "log": []}


def load(root):
    p = db_path(root)
    if not os.path.exists(p):
        return empty()
    f = io.open(p, "r", encoding="utf-8")
    try:
        db = json.loads(f.read())
    finally:
        f.close()
    for k, v in empty().items():
        db.setdefault(k, v)
    return db


def save(root, db):
    text = json.dumps(db, indent=1, sort_keys=True, ensure_ascii=True, separators=(",", ": "))
    f = io.open(db_path(root), "w", encoding="utf-8", newline="\n")
    try:
        f.write(u"%s\n" % text)
    finally:
        f.close()


def today():
    return time.strftime("%Y-%m-%d")


def addr_key(va):
    return "0x%06x" % va


def off_key(off):
    return "0x%03x" % off


def parse_int(text):
    """0x2a8, 2a8h, 680 -> int (None if it isn't a number)."""
    t = (text or "").strip().lower().rstrip("h")
    if not t:
        return None
    try:
        return int(t, 16) if t.startswith("0x") else int(t)
    except ValueError:
        try:
            return int(t, 16)
        except ValueError:
            return None


def log(db, by, what):
    db["log"].append({"on": today(), "by": by, "what": what})


def set_function(db, va, by, **fields):
    e = db["functions"].setdefault(addr_key(va), {})
    for k, v in fields.items():
        if v is not None:
            e[k] = v
    e["by"] = by
    return e


def set_field(db, struct, off, by, name, type_name="int", summary="", confidence="sure"):
    s = db["structs"].setdefault(struct, {"summary": "", "fields": {}})
    s["fields"][off_key(off)] = {"name": name, "type": type_name or "int", "summary": summary or "",
                                 "by": by, "confidence": confidence}
    return s["fields"][off_key(off)]


def set_constant(db, value, by, meaning, kind=""):
    db["constants"][str(value)] = {"meaning": meaning, "kind": kind, "by": by}


def set_note(db, va, by, text):
    db["notes"][addr_key(va)] = {"text": text, "by": by, "on": today()}


def set_global(db, va, by, name, summary="", kind=""):
    """A global's name (a label in Ghidra): left as it is if someone else
    named it something else."""
    e = db["globals"].get(addr_key(va))
    if e and e.get("by") != by and e.get("name") != name:
        return None
    db["globals"][addr_key(va)] = {"name": name, "summary": summary, "kind": kind, "by": by}
    return db["globals"][addr_key(va)]


def add_field_if_new(db, struct, off, by, name, type_name="int", summary="", confidence="sure"):
    """A struct field, unless that offset's named already (by anyone)."""
    s = db["structs"].setdefault(struct, {"summary": "", "fields": {}})
    if off_key(off) in s["fields"]:
        return None
    return set_field(db, struct, off, by, name, type_name, summary, confidence)


def class_of_function(db, va):
    """The classes whose table of functions holds va, each with its slot."""
    out = []
    for name, c in sorted(db["classes"].items()):
        for slot, fa in c.get("slots", {}).items():
            if parse_int(fa) == va:
                out.append((name, slot))
    return out


def add_seen(db, va, by, when, called_from=None):
    """It ran while "when" was being done (once each: the latest date kept)."""
    rows = db["seen"].setdefault(addr_key(va), [])
    for r in rows:
        if r.get("when") == when:
            r["on"] = today()
            return r
    r = {"when": when, "on": today(), "by": by}
    if called_from is not None:
        r["from"] = addr_key(called_from)
    rows.append(r)
    return r


def seen_text(db, va):
    """"Ran when: placing a fence; hiring a keeper" - or ""."""
    rows = db["seen"].get(addr_key(va)) or []
    return "Ran when: " + "; ".join(r["when"] for r in rows) if rows else ""


def ask(db, by, va, function, question, who="assistant"):
    n = 1 + max([int(q["id"][1:]) for q in db["questions"]] or [0])
    q = {"id": "q%d" % n, "for": who, "address": addr_key(va) if va is not None else "", "function": function or "",
         "question": question, "by": by, "on": today(), "status": "open", "answer": "", "answered_by": "",
         "answered_on": ""}
    db["questions"].append(q)
    return q


def answer(db, qid, by, text):
    for q in db["questions"]:
        if q["id"] == qid:
            q.update(status="answered", answer=text, answered_by=by, answered_on=today())
            return q
    return None


def questions_at(db, entry_va, body_contains=None):
    """Questions about a function (asked at its entry or anywhere in it)."""
    out = []
    for q in db["questions"]:
        va = parse_int(q.get("address"))
        if va is None:
            continue
        if va == entry_va or (body_contains is not None and body_contains(va)):
            out.append(q)
    return out


def describe_function(db, va):
    """What's known about the function at va, as text."""
    e = db["functions"].get(addr_key(va))
    if not e:
        return ""
    lines = ["%s%s%s" % (e.get("class") + "::" if e.get("class") else "", e.get("name", "?"),
                         "  (provisional name)" if e.get("provisional") else
                         "  (name from %s)" % e["name_by"] if e.get("name_by") else "")]
    if e.get("summary"):
        lines.append(e["summary"])
    if e.get("engine"):
        lines.append("Engine: " + " ".join(e["engine"]))
    if e.get("source"):
        lines.append("Source file: " + e["source"])
    slots = class_of_function(db, va)
    if slots:
        lines.append("In the tables of: " + ", ".join("%s (slot %s)" % s for s in slots))
    lines.append("By %s, %s" % (e.get("by", "?"), e.get("confidence", "sure")))
    return "\n".join(lines)
