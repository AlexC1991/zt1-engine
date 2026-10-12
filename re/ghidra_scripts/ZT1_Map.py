# Records what you've worked out about the spot under the cursor into the
# knowledge base (re/knowledge.json) and puts it in Ghidra at once:
#   name the function, a struct field (the offset under the cursor), what
#   a number means (the number under the cursor), a note, a question for
#   your AI assistant, an answer to a question or lead, or just what's known here.
# One form for each, filled in from what's under the cursor and what's
# known. Everything you add is under your name. Ctrl+Alt+M, in the
# listing or the decompiler.
#@author zt1-engine
#@category zt1-engine
#@keybinding ctrl alt M
#@menupath Tools.zt1-engine.Map this
#@runtime Jython

from __future__ import print_function
import json, os, sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db, zt1ghidra

from ghidra.features.base.values import GhidraValuesMap
from ghidra.program.model.listing import CodeUnit
from ghidra.program.model.symbol import SourceType
from java.lang import String
from jarray import array

NAME = "Name this function"
FIELD = "A struct field (the offset under the cursor)"
NUMBER = "What a number means (the number under the cursor)"
NOTE = "A note here"
ASK = "Ask your AI assistant about this"
ANSWER = "Answer a question or lead here"
SHOW = "Show what's known here"
MARK = "[zt1] "
NEW = "(a new struct: name it below)"
KINDS = ["message", "config", "id", "flag", "other"]


def form(title, message, fields):
    """One dialog: fields are (label, default) for text, (label, default,
    [choices]) for a list. The answers by label, text trimmed."""
    v = GhidraValuesMap()
    for fld in fields:
        if len(fld) == 3:
            v.defineChoice(fld[0], fld[1], array(fld[2], String))
        else:
            v.defineString(fld[0], fld[1] or "")
    askValues(title, message, v)
    out = {}
    for fld in fields:
        got = v.getChoice(fld[0]) if len(fld) == 3 else v.getString(fld[0])
        out[fld[0]] = (got or "").strip()
    return out


def message_text(root, n):
    """A message's text from re/private/strings.json (python re/msg.py makes it)."""
    p = zt1re.path(root, "re", "private", "strings.json")
    if n is None or not os.path.exists(p):
        return ""
    try:
        return json.load(open(p)).get(str(n), "")
    except:  # noqa: E722
        return ""


def refresh_plate(db, f):
    """The plate comment: your own lines kept (bar the one now in the
    knowledge base), then the knowledge base's."""
    va = f.getEntryPoint().getOffset()
    e = db["functions"].get(zt1db.addr_key(va), {})
    lines = [l for l in (f.getComment() or "").split("\n")
             if l and not l.startswith(MARK) and l.strip() != e.get("comment", "").strip()]
    lines += [MARK + l for l in zt1ghidra.knowledge_plate(db, va).split("\n") if l]
    f.setComment("\n".join(lines))


def name_function(db, me, f):
    va = f.getEntryPoint().getOffset()
    e = db["functions"].get(zt1db.addr_key(va), {})
    current = f.getName() if zt1re.is_named(f.getName()) else e.get("name", "")
    note = "Known: " + e["summary"] if e.get("summary") else "Nothing known about it yet."
    a = form(NAME, "%s at %s. %s" % (f.getName(True), f.getEntryPoint(), note), [
        ("Name (what it does, e.g. checkHunger)", current),
        ("Class (e.g. ZTAnimal; empty for none)", zt1re.class_of(f) or e.get("class", "")),
        ("What it does, in a line", e.get("comment") or e.get("summary", "")),
        ("How sure are you?", "likely", zt1db.CONFIDENCE)])
    name, cls = a["Name (what it does, e.g. checkHunger)"], a["Class (e.g. ZTAnimal; empty for none)"]
    summary, conf = a["What it does, in a line"], a["How sure are you?"]
    if not name:
        popup("No name given: nothing changed.")
        return False
    f.setName(name, SourceType.USER_DEFINED)
    f.setParentNamespace(zt1ghidra.namespace_for(currentProgram, cls))
    for t in list(f.getTags()):
        if t.getName().startswith("src:") or t.getName().startswith("by:") or t.getName() == "zt1:provisional":
            f.removeTag(t.getName())
    f.addTag("src:zt1-engine")
    f.addTag("by:" + me)
    old = e.get("name")
    e = zt1db.set_function(db, va, me, name=name, confidence=conf, provisional=False)
    e["class"] = cls
    e.pop("name_by", None)
    if summary and summary != e.get("summary"):
        e["comment"], e["comment_by"] = summary, me
    zt1db.log(db, me, "Named %s %s%s%s" % (zt1db.addr_key(va), cls + "::" if cls else "", name,
                                           " (was %s)" % old if old and old != name else ""))
    refresh_plate(db, f)
    return True


def map_field(db, me, root):
    structs = sorted(db["structs"].keys())
    state = zt1re.path(root, "re", "private", "last_struct.txt")
    last = open(state).read().strip() if os.path.exists(state) else ""
    n = zt1ghidra.number_under_cursor(currentLocation)
    a = form(FIELD, "A field at an offset in a struct: an animal is ZTAnimal, its species ZTAnimalType, "
             "an exhibit ZTHabitat.\nTypes: int, float, byte, short, char, bool, pointer, a struct's name, "
             "or a pointer to one (ZTAnimalType *).", [
                 ("Struct", last if last in structs else (structs[0] if structs else NEW), structs + [NEW]),
                 ("New struct's name (if new)", ""),
                 ("Offset (hex)", "0x%x" % n if n is not None else "0x"),
                 ("Field name (e.g. happiness)", ""),
                 ("Type", "int"),
                 ("What it is, in a line", ""),
                 ("How sure are you?", "likely", zt1db.CONFIDENCE)])
    pick = a["New struct's name (if new)"] if a["Struct"] == NEW else a["Struct"]
    off = zt1db.parse_int(a["Offset (hex)"])
    name = a["Field name (e.g. happiness)"]
    if not pick or off is None or not name:
        popup("A struct, an offset and a name are needed: nothing changed.")
        return False
    old = db["structs"].get(pick, {}).get("fields", {}).get(zt1db.off_key(off))
    if old and old.get("name") != name and not askYesNo(FIELD, "%s+%s is already %s (by %s). Replace it?"
                                                        % (pick, zt1db.off_key(off), old["name"], old.get("by"))):
        return False
    out = open(state, "w")
    out.write(pick)
    out.close()
    fld = zt1db.set_field(db, pick, off, me, name, a["Type"] or "int", a["What it is, in a line"],
                          a["How sure are you?"])
    dtm = currentProgram.getDataTypeManager()
    err = zt1ghidra.set_field(dtm, zt1ghidra.struct(dtm, pick), off, fld)
    if err:
        print("Saved to the knowledge base, but Ghidra said: " + err)
    zt1db.log(db, me, "Field %s+%s = %s (%s)" % (pick, zt1db.off_key(off), name, fld["type"]))
    print("%s+%s is %s. To see it in the decompiler: click the variable holding a %s, Ctrl+L, type  %s *"
          % (pick, zt1db.off_key(off), name, pick, pick))
    return True


def map_number(db, me, root):
    n = zt1ghidra.number_under_cursor(currentLocation)
    old = db["constants"].get(str(n), {}) if n is not None else {}
    text = message_text(root, n)
    a = form(NUMBER, "What a number in the code means. A message id's text: python re/msg.py <id>.", [
        ("Number", str(n) if n is not None else ""),
        ("What it means", old.get("meaning") or ("Message: " + text if text else "")),
        ("Kind", old.get("kind") or ("message" if text else "other"), KINDS)])
    v = zt1db.parse_int(a["Number"])
    if v is None or not a["What it means"]:
        popup("A number and what it means are needed: nothing changed.")
        return False
    zt1db.set_constant(db, v, me, a["What it means"], a["Kind"])
    ins = getInstructionContaining(currentAddress)
    if ins is not None and not (ins.getComment(CodeUnit.EOL_COMMENT) or "").strip():
        ins.setComment(CodeUnit.EOL_COMMENT, MARK + "%d: %s" % (v, a["What it means"]))
    zt1db.log(db, me, "Number %d = %s" % (v, a["What it means"]))
    return True


def map_note(db, me):
    va = currentAddress.getOffset()
    old = db["notes"].get(zt1db.addr_key(va), {})
    a = form(NOTE, "A note at %s, kept in the knowledge base." % currentAddress, [("Note", old.get("text", ""))])
    if not a["Note"]:
        return False
    zt1db.set_note(db, va, me, a["Note"])
    pre = [l for l in (getPreComment(currentAddress) or "").split("\n") if l and not l.startswith(MARK)]
    setPreComment(currentAddress, "\n".join(pre + [MARK + a["Note"] + "  (" + me + ")"]))
    zt1db.log(db, me, "Note at %s: %s" % (zt1db.addr_key(va), a["Note"]))
    return True


def ask_assistant(db, me, f):
    where = "%s (%s)" % (currentAddress, f.getName(True) if f else "no function")
    a = form(ASK, "A question about %s. ZT1_Report puts it in the report, with this function's code." % where,
             [("Question", "")])
    if not a["Question"]:
        return False
    q = zt1db.ask(db, me, currentAddress.getOffset(), f.getName(True) if f else "", a["Question"], who="assistant")
    createBookmark(currentAddress, "zt1-question", "%s %s (%s)" % (q["id"], a["Question"], me))
    zt1db.log(db, me, "Asked %s at %s: %s" % (q["id"], q["address"], a["Question"]))
    print("Asked as %s. Run ZT1_Report to write the report to send." % q["id"])
    return True


def answer_here(db, me, f):
    open_qs = []
    if f is not None:
        body = f.getBody()
        open_qs = [q for q in zt1db.questions_at(db, f.getEntryPoint().getOffset(),
                                                 lambda va: body.contains(toAddr(va))) if q["status"] == "open"]
    if not open_qs:
        open_qs = [q for q in db["questions"] if q["status"] == "open"]
    if not open_qs:
        popup("No open questions or leads.")
        return False
    labels = ["%s: %s" % (q["id"], q["question"][:90]) for q in open_qs]
    a = form(ANSWER, "Answer one of these (yours or a lead).", [("Which", labels[0], labels), ("Answer", "")])
    if not a["Answer"]:
        return False
    qid = a["Which"].split(":")[0]
    q = zt1db.answer(db, qid, me, a["Answer"])
    zt1db.log(db, me, "Answered %s: %s" % (qid, a["Answer"]))
    va = zt1db.parse_int(q.get("address"))
    if va is not None:
        createBookmark(toAddr(va), "zt1-answer", "%s %s -> %s (%s)" % (qid, q["question"], a["Answer"], me))
    return True


def show_known(db, f):
    out = []
    if f is not None:
        va = f.getEntryPoint().getOffset()
        out.append("%s at %s" % (f.getName(True), f.getEntryPoint()))
        out.append(zt1ghidra.knowledge_plate(db, va) or "(nothing in the knowledge base about this function)")
        body = f.getBody()
        for key, n in sorted(db["notes"].items()):
            if body.contains(toAddr(zt1db.parse_int(key))):
                out.append("Note at %s: %s (%s)" % (key, n["text"], n.get("by")))
        for q in zt1db.questions_at(db, va, lambda a: body.contains(toAddr(a))):
            out.append("%s [%s] %s%s" % (q["id"], q["status"], q["question"],
                                         " -> " + q["answer"] + " (" + q["answered_by"] + ")" if q["answer"] else ""))
    n = zt1ghidra.number_under_cursor(currentLocation)
    if n is not None and str(n) in db["constants"]:
        out.append("%d: %s" % (n, db["constants"][str(n)]["meaning"]))
    popup("\n".join(out) or "Nothing known here yet.")


def main():
    root = zt1re.repo_root(getSourceFile())
    db = zt1db.load(root)
    f = getFunctionContaining(currentAddress)
    what = askChoice("Map this", "At %s%s:" % (currentAddress, " in " + f.getName(True) if f else ""),
                     [NAME, FIELD, NUMBER, NOTE, ASK, ANSWER, SHOW], NAME if f else NOTE)
    if what == SHOW:
        show_known(db, f)
        return
    me = zt1re.me(root, askString)
    changed = False
    if what == NAME:
        if f is None:
            popup("There's no function here: press F at its start to make one first.")
            return
        changed = name_function(db, me, f)
    elif what == FIELD:
        changed = map_field(db, me, root)
    elif what == NUMBER:
        changed = map_number(db, me, root)
    elif what == NOTE:
        changed = map_note(db, me)
    elif what == ASK:
        changed = ask_assistant(db, me, f)
    elif what == ANSWER:
        changed = answer_here(db, me, f)
    if changed:
        zt1db.save(root, db)


main()
