# The Atlas: everything known about zoo.exe and our engine, joined into one
# searchable map - so anyone can ask "where's selling animals?" and get the
# functions, the actions they ran in, the config keys, the engine code, in
# plain words. And what's missing: each action recorded in the original,
# with how much of its own code our engine has.
#
# It joins:
#   re/knowledge.json         names, summaries, notes, structs, globals,
#                             loaders, classes, "seen running" (recordings)
#   re/private/recordings/    each recorded action and what ran in it
#   src/                      our engine's comments citing zoo.exe addresses
#                             (as re/scan_engine.py reads them), and every
#                             other comment block, for "where in our engine"
#   Ghidra (through the Lab)  every function, the text each one uses, the
#                             names its own debug text gives - passed as
#                             files; any missing is just left out
#
#   python re/lab/atlas.py [--functions f.json] [--fnstrings f.json]
#                          [--debugnames f.json] [--out re/private/atlas.json]
# The Lab (Explore > Rebuild the map) runs it with Ghidra's answers.
import argparse
import glob
import json
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
sys.path.insert(0, os.path.join(ROOT, "re"))
import scan_engine  # noqa: E402 (beside it: the engine's comment blocks)

# What an action's label says it's about, first match (the Explore view
# groups the actions by these)
AREAS = [
    ("Shows", r"\bshow|trick|grandstand|trainer|orca"),
    ("Tanks and marine", r"\btank|marine|porpoise|salin|drain|water"),
    ("Dinosaurs", r"dinosaur|allosaurus|paddock|\bdrt\b|helicopter|\begg"),
    ("Saving and scenarios", r"\bsav|\bload|scenario|tutorial|goal|main menu|freeform zoo"),
    ("Guests and buying", r"guest|buying|vending|price|admission|marketing"),
    ("Staff", r"\bhir|keeper|specialist|scientist|staff|assign"),
    ("Animals", r"animal|zebra|adopt|sell"),
    ("Building", r"build|fence|path|tree|rock|terrain|land|bulldoz|delet|undo|place"),
    ("Panels and info", r"open|panel|list|tab|zoo status|info|option|message|research"),
    ("Running the zoo", r"running|month|paus|escap|popup|loose"),
]
STOP = set("a an the of to in on and or for by with is it its at as from this that be when what where how which "
           "do does did are was were into then than there their them they you your i me my".split())
# A few words people use for what the code calls something else
SYNONYMS = {
    "sell": ["sold", "refund", "sale"], "money": ["cash", "price", "cost"], "visitor": ["guest"],
    "show": ["trick", "grandstand", "trainer"], "dino": ["dinosaur"], "delete": ["bulldoze", "remove"],
    "build": ["place", "construct"], "save": ["saved", "write"], "load": ["open", "read"],
    "happy": ["happiness"], "hungry": ["hunger", "food"], "thirsty": ["thirst", "drink"],
    "sick": ["health", "ill"], "keeper": ["zookeeper"], "staff": ["hire", "keeper", "maint"],
    "tank": ["aquarium", "marine"], "fence": ["wall", "barrier"], "rating": ["popularity"],
}
# A function's definition in our engine's source: "void World::sellAnimal(int id) {"
DEFN = re.compile(r"^(?:[A-Za-z_][\w:<>,\*& ]*?\s+[\*&]?)?((?:\w+::)*~?\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{\s*(?://.*)?$")
NOT_FN = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else"}
CAMEL = re.compile(r"(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])")
WORD = re.compile(r"[A-Za-z][A-Za-z0-9]*|\d+")


def stem(w):
    for suf in ("ings", "ing", "ies", "ed", "es", "s"):
        if len(w) > len(suf) + 2 and w.endswith(suf):
            return w[: -len(suf)] + ("y" if suf == "ies" else "")
    return w


def tokens(text):
    """Words, camelCase split (cHungerCheck -> c hunger check), lower case,
    stemmed, without the little words."""
    out = []
    for w in WORD.findall(text or ""):
        parts = CAMEL.split(w) if not w.isdigit() else [w]
        for p in parts + ([w] if len(parts) > 1 else []):
            p = p.lower()
            if len(p) < 2 or p in STOP:
                continue
            out.append(stem(p))
    return out


def load(path, default):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception:  # noqa: BLE001 (not there yet)
        return default


def hexkey(a):
    try:
        return "0x%06x" % int(str(a), 16)
    except ValueError:
        return str(a)


def area_of(label):
    low = label.lower()
    for name, pat in AREAS:
        if re.search(pat, low):
            return name
    return "Other"


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--functions")
    ap.add_argument("--fnstrings")
    ap.add_argument("--debugnames")
    ap.add_argument("--out", default=os.path.join(ROOT, "re", "private", "atlas.json"))
    a = ap.parse_args()

    kb = load(os.path.join(ROOT, "re", "knowledge.json"), {})
    ghidra = {hexkey(f["address"]): f for f in (load(a.functions, {}) or {}).get("functions", [])} if a.functions else {}
    texts = {hexkey(k): v for k, v in ((load(a.fnstrings, {}) or {}).get("functions", {}) if a.fnstrings else {}).items()}
    debug = {hexkey(k): v for k, v in ((load(a.debugnames, {}) or {}).get("functions", {}) if a.debugnames else {}).items()}

    # Our engine: every comment block, and the zoo.exe addresses each cites
    engine_cites = {}  # address -> ["src/x.cpp:12 text", ...]
    engine_docs = []
    for folder, _, files in os.walk(os.path.join(ROOT, "src")):
        if os.sep + "original" in folder or os.sep + "readers" in folder:
            continue
        for name in sorted(files):
            if not name.endswith((".cpp", ".hpp", ".h")):
                continue
            path = os.path.join(folder, name)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            with open(path, encoding="utf-8", errors="replace") as f:
                lines = f.read().split("\n")
            # (its functions too, by their names: World::sellAnimal reads as
            # "world sell animal", with the comment just above it)
            for k, line in enumerate(lines):
                m = DEFN.match(line)
                if not m or m.group(1).split("::")[-1] in NOT_FN:
                    continue
                above = []
                j = k - 1
                while j >= 0 and lines[j].lstrip().startswith("//") and len(above) < 6:
                    above.insert(0, lines[j].lstrip()[2:].strip())
                    j -= 1
                engine_docs.append({"kind": "engine", "id": "%s:%d" % (rel, k + 1),
                                    "title": "%s - %s" % (m.group(1), "%s:%d" % (rel, k + 1)),
                                    "text": (line.strip() + " " + " ".join(above))[:600], "cites": [], "fn": m.group(1)})
            for first, text in scan_engine.comment_blocks(lines):
                if len(text) < 40:
                    continue
                cited = sorted({"0x%06x" % int(m.group(1), 16) for m in scan_engine.ADDR.finditer(text)
                                if scan_engine.TEXT_LO <= int(m.group(1), 16) < scan_engine.TEXT_HI})
                for c in cited:
                    engine_cites.setdefault(c, []).append("%s:%d" % (rel, first))
                engine_docs.append({"kind": "engine", "id": "%s:%d" % (rel, first), "title": "%s, line %d" % (rel, first),
                                    "text": text[:600], "cites": cited})

    # Recordings: what ran when, and which functions are each action's own
    seen = kb.get("seen", {})
    ran_in = {}  # action -> set of functions
    for addr, rows in seen.items():
        for r in rows:
            ran_in.setdefault(r["when"], set()).add(hexkey(addr))
    count = {}
    for fns in ran_in.values():
        for f in fns:
            count[f] = count.get(f, 0) + 1
    files = {}
    for p in glob.glob(os.path.join(ROOT, "re", "private", "recordings", "*.json")):
        r = load(p, {})
        if r.get("label"):
            files[r["label"]] = os.path.relpath(p, ROOT).replace(os.sep, "/")

    funcs = kb.get("functions", {})
    # Our engine's own words for each action: its comment blocks scored
    # against the action's distinctive words (rarer words count more), so an
    # action the engine does without citing the original's functions still
    # shows - "in our engine, not traced to the original"
    import math
    eng_tokens = [set(tokens(d["text"] + " " + d["id"] + (" " + d["fn"]) * 2 if d.get("fn") else d["text"] + " " + d["id"]))
                  for d in engine_docs]
    df = {}
    for ts in eng_tokens:
        for t in ts:
            df[t] = df.get(t, 0) + 1
    n_eng = max(1, len(engine_docs))
    GENERIC = set(tokens("opening open panel placing place one tab info information button the zoo running "
                         "dragging confirm refused click list bottom right left "
                         "tutorial january february minute second freeform game new its own"))

    def idf(t):
        return math.log(n_eng / df.get(t, 0.5))

    def engine_match(label):
        # (what the action is, not its example: "Selling an animal (... the
        # harbor porpoise)" is matched as "selling an animal")
        core = label.split("(")[0]
        want = {t for t in tokens(core) if t not in GENERIC and len(t) > 2}
        if not want:
            return []
        total = sum(idf(t) for t in want)
        best = []
        for i, ts in enumerate(eng_tokens):
            hit = want & ts
            if len(hit) < min(2, len(want)):
                continue
            cover = sum(idf(t) for t in hit) / total
            best.append((cover, i, sorted(hit)))
        best.sort(reverse=True)
        return [{"at": engine_docs[i]["id"], "cover": round(cv, 2), "words": w, "text": engine_docs[i]["text"][:160]}
                for cv, i, w in best[:3]]

    verdicts = load(os.path.join(ROOT, "re", "lab", "verdicts.json"), {}).get("actions", {})

    actions = []
    for label, fns in ran_in.items():
        own = sorted(f for f in fns if count[f] == 1)
        rare = sorted(f for f in fns if count[f] <= 3)
        basis = own if len(own) >= 3 else rare
        done = [f for f in basis if f in engine_cites]
        share = len(done) / len(basis) if basis else 0.0
        found = engine_match(label)
        strong = bool(found) and found[0]["cover"] >= 0.75
        # copied: our engine cites at least half of what's this action's own;
        # traced: some of it; untraced: our engine talks about it but cites
        # none of it; missing: neither
        # (a word search never says "not in our engine" by itself: only a
        # person's checked verdict does - not found is "unknown", check it)
        status = ("copied" if basis and share >= 0.5 else "traced" if done else "untraced" if strong
                  else "unknown" if basis else "shared")
        # (someone's checked it: their verdict, over the guess)
        verdict = verdicts.get(label)
        if verdict:
            status = verdict.get("status", status)
        actions.append({
            "kind": "action", "id": label, "title": label, "area": area_of(label), "file": files.get(label, ""),
            "ran": len(fns), "own": len(own), "basis": len(basis), "basisKind": "own" if basis is own else "rare",
            "inEngine": len(done), "share": round(share, 3), "status": status, "engineWords": found, "verdict": verdict,
            "top": [{"address": f, "name": (funcs.get(f) or {}).get("name") or (ghidra.get(f) or {}).get("name", ""),
                     "inEngine": f in engine_cites} for f in basis[:40]],
        })
    actions.sort(key=lambda x: (x["area"], x["title"]))

    # One entry a function: everything said about it, in words
    docs = []
    addrs = set(funcs) | set(texts) | set(debug) | set(engine_cites) | set(count)
    addrs |= {k for k, f in ghidra.items() if not f.get("name", "").startswith("FUN_")}
    loaders = kb.get("loaders", {})
    classes = kb.get("classes", {})
    slot_of = {}
    for cname, c in classes.items():
        for slot, fa in c.get("slots", {}).items():
            slot_of.setdefault(hexkey(fa), []).append("%s slot %s" % (cname, slot))
    for addr in sorted(addrs):
        e = funcs.get(addr, {})
        g = ghidra.get(addr, {})
        name = e.get("name") or (g.get("name") if g.get("name") and not g["name"].startswith("FUN_") else "") or ""
        cls = e.get("class") or g.get("class") or ""
        dn = debug.get(addr, [])
        title = ((cls + "::") if cls else "") + name if name else (dn[0] if len(dn) == 1 else "FUN_%06x" % int(addr, 16))
        note = (kb.get("notes", {}).get(addr) or {}).get("text", "")
        whens = sorted({r["when"] for r in seen.get(addr, [])})
        engine = engine_cites.get(addr, [])
        status = "engine" if engine else "named" if name and not e.get("provisional") else "provisional" if name else "unknown"
        docs.append({
            "kind": "function", "id": addr, "title": title, "status": status,
            "summary": e.get("summary", ""), "note": note, "source": e.get("source", ""),
            "texts": [t for t in texts.get(addr, []) if len(t) > 2][:25], "debug": dn[:6], "ranIn": whens[:30],
            "engine": engine[:6], "slots": slot_of.get(addr, [])[:6],
            "loader": (loaders.get(addr) or {}).get("struct", ""), "size": g.get("size"),
        })

    # Config keys: what each reads into (from the loaders and structs)
    for sname, s in kb.get("structs", {}).items():
        docs.append({"kind": "struct", "id": sname, "title": sname, "summary": s.get("summary", ""),
                     "fields": ["+%s %s" % (k, f.get("name")) for k, f in sorted(s.get("fields", {}).items(),
                                                                       key=lambda kv: int(kv[0], 16))][:400]})
        for off, f in s.get("fields", {}).items():
            n = f.get("name", "")
            if n.startswith("c") or n[:1].islower():
                docs.append({"kind": "key", "id": "%s+%s" % (sname, off), "title": n, "struct": sname, "offset": off,
                             "summary": f.get("summary", "")})
    for addr, gl in kb.get("globals", {}).items():
        docs.append({"kind": "key", "id": addr, "title": gl.get("name", ""), "global": addr, "summary": gl.get("summary", "")})
    for cname, c in classes.items():
        docs.append({"kind": "class", "id": cname, "title": cname, "vtable": c.get("vtable"), "summary": c.get("summary", ""),
                     "slotCount": len(c.get("slots", {}))})
    for act in actions:
        docs.append({k: act[k] for k in ("kind", "id", "title", "area", "status", "own", "inEngine", "basis", "file")})
    docs.extend(engine_docs)

    # Each entry's words, for the search: the name counts most, then its
    # summary and the actions it ran in, then the rest
    for d in docs:
        strong = d.get("title", "") + " " + " ".join(d.get("debug", []))
        # (the actions it ran in say what it's for only when they're few:
        # one that runs for everything isn't about any of them)
        ran = d.get("ranIn", [])
        mid = " ".join([d.get("summary", ""), d.get("note", ""), " ".join(ran) if len(ran) <= 3 else "", d.get("area", ""),
                        d.get("struct", ""), d.get("loader", "")])
        weak = " ".join([" ".join(d.get("texts", [])), d.get("text", ""), " ".join(d.get("fields", [])),
                         d.get("source", ""), " ".join(d.get("slots", []))])
        d["w"] = " ".join(tokens(strong) * 3 + tokens(mid) * 2 + tokens(weak))

    out = {"built": __import__("datetime").datetime.now().isoformat(timespec="seconds"),
           "synonyms": {stem(k): [stem(x) for x in v] for k, v in SYNONYMS.items()},
           "counts": {"functions": sum(1 for d in docs if d["kind"] == "function"), "actions": len(actions),
                      "engine": len(engine_docs), "keys": sum(1 for d in docs if d["kind"] == "key"),
                      "ghidraFunctions": len(ghidra), "withText": len(texts)},
           "actions": actions, "docs": docs}
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as f:
        json.dump(out, f, ensure_ascii=False, separators=(",", ":"))
    print(json.dumps(out["counts"]))


if __name__ == "__main__":
    main()
