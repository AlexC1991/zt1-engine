# How close our engine is to zoo.exe, both ways round:
#
# - Our side: every function in the engine's gameplay, UI and drawing code,
#   and where it came from - a marker in its comment or body:
#     zoo.exe 0x48d21d   copied from the original's code (any address in
#                        zoo.exe's code counts, as re/scan_engine.py reads them)
#     measured: ...      measured from the original's behaviour (say how)
#     ours: ...          the original has nothing like it (tools, plumbing)
#     addition: ...      a new feature - behind a Features.hpp switch
#   A function with none is "unmarked": nobody's said where it came from.
#   Readers of the game's files and our own tools (the Lab, mods, memory
#   tracking...) are whole files of "ours"/"reader" and need no markers.
#
# - The original's side: every function the knowledge base names in zoo.exe's
#   gameplay (tasks, jobs, needs, money, placement, scenarios...), and whether
#   our engine cites it anywhere ("ported") or not ("missing"). The C runtime,
#   the containers, the graphics and sound layers, the UI framework are left
#   out: our engine has its own.
#
#   python re/lab/fidelity.py [--out re/private/fidelity.json] [--quiet]
import argparse
import collections
import json
import os
import re

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
SRC = os.path.join(ROOT, "src")
KB = os.path.join(ROOT, "re", "knowledge.json")
OUT = os.path.join(ROOT, "re", "private", "fidelity.json")
TEXT_LO, TEXT_HI = 0x401000, 0x62c824
ADDR = re.compile(r"\b0x([4-6][0-9a-fA-F]{5})\b")
DEFN = re.compile(r"^(?:[A-Za-z_][\w:<>,\*& ]*?\s+[\*&]?)?((?:\w+::)*~?\w+)\s*\([^;{}]*\)\s*(?:const\s*)?(?:override\s*)?\{\s*(?://.*)?$")
NOT_FN = {"if", "for", "while", "switch", "catch", "return", "sizeof", "else"}

# What each file is. Gameplay, UI and drawing must say where each function
# came from; the rest is ours or reads the game's files.
GAMEPLAY = {"Animals", "Guests", "Staff", "World", "Fences", "ZooSim", "Research", "ScenarioManager",
            "ScenarioDatabase", "Goals", "Pathfinder", "PlacedObjects", "TerrainTool", "ZooItems", "Ambient",
            "ItemCatalog", "Walkways", "EntityManager", "WorldIndex", "WorldMap", "main"}
UI = {"UiButton", "UiGameScreen", "UiGraph", "UiImage", "UiLayout", "UiListBox", "UiMiniMap", "UiScrollBar",
      "UiScrollingRegion", "UiStatusImage", "UiText"}
DRAWING = {"WorldRenderer", "Animation", "SpriteManager", "Sound"}
READERS = {"AniFile", "ZtdFile", "IniReader", "PalletManager", "PeFile", "PngLoader", "LangText", "ZooReader",
           "ResourceManager", "SpriteDatabase", "ShowsCfgReader", "Config", "SaveGame"}


def kind_of(rel):
    stem = os.path.splitext(os.path.basename(rel))[0]
    if stem in GAMEPLAY:
        return "gameplay"
    if stem in UI:
        return "ui"
    if stem in DRAWING:
        return "drawing"
    if stem in READERS:
        return "reader"
    return "ours"


def marker(text):
    """Where a function came from, by the marker in its comments or body."""
    for m in ADDR.finditer(text):
        if TEXT_LO <= int(m.group(1), 16) < TEXT_HI:
            return "copied"
    low = text.lower()
    if "measured:" in low:
        return "measured"
    if "addition:" in low or "features::" in low:
        return "addition"
    if "ours:" in low:
        return "ours"
    return None


# A number with a point in it (0.8, 3.5f, 30.0) on a line that doesn't say
# where it's from (a zoo.exe address, a config key cSomething, "measured:",
# "ours:", Original::): the kind of number that belongs in
# OriginalConstants.hpp or comes from the game's files. Whole numbers are
# left out (indexes, ids, counts), and the plainest (0.5, 1.0, 100.0...)
BARE = re.compile(r"(?<![\w.])(\d+\.\d*|\.\d+)f?(?![\w.])")
SAYS = re.compile(r"0x[4-6][0-9a-fA-F]{5}|\bc[A-Z]\w+|measured:|ours:|addition:|Original::")
STRING = re.compile(r'"(?:[^"\\]|\\.)*"')
TRIVIAL_NUM = {"0.0", "1.0", "0.", "1.", ".0", "0.5", "2.0", "100.0", "0.01", "1000.0", "255.0", "0.25"}


def bare_numbers(body, first):
    found = []
    for n, line in enumerate(body):
        if SAYS.search(line):
            continue
        code = STRING.sub('""', line.split("//")[0])
        for m in BARE.finditer(code):
            if m.group(1) not in TRIVIAL_NUM:
                found.append({"line": first + n, "number": m.group(1)})
    return found


def functions(path):
    """(name, line, marker, body lines) for each function defined in a file."""
    with open(path, encoding="utf-8", errors="replace") as f:
        lines = f.read().split("\n")
    out = []
    i = 0
    while i < len(lines):
        line = lines[i]
        m = DEFN.match(line)
        if not m or m.group(1).split("::")[-1] in NOT_FN or line.startswith((" ", "\t")):
            i += 1
            continue
        # its comment block: the // lines straight above it
        j = i - 1
        above = []
        while j >= 0 and lines[j].strip().startswith("//"):
            above.append(lines[j])
            j -= 1
        # its body: to the brace that closes it
        depth, k = 0, i
        while k < len(lines):
            depth += lines[k].count("{") - lines[k].count("}")
            if depth <= 0 and k > i or (depth == 0 and k == i and "}" in lines[k]):
                break
            k += 1
        body = lines[i:k + 1]
        mk = marker("\n".join(above + body))
        out.append({"name": m.group(1), "line": i + 1, "marker": mk, "lines": len(body),
                    "bare": bare_numbers(body, i + 1)})
        i = k + 1
    return out


# The original's systems, by the knowledge base's names
FRAMEWORK = re.compile(r"^(crt_|stl_|str[A-Z]|small|surface|sprite|setCursor|window|mouse|ime|ui[A-Z]|list[A-Z]|"
                       r"createSurface|returns|returnArg|returnTrue|nop|setFirstField|setFlag|get_|set_|anim[A-Z]|"
                       r"icon|register_|registerManager|zooView|theGame|selectedObject|unitTile|gameText|"
                       r"formatMoney|formatGameText|messageBar|postEvent|dsound|appMessage|mainWindow|spriteRefresh|"
                       r"spriteRestart|spriteAnimate|setActiveTab|closePanel|closeOther|bulldozeToolOff|"
                       r"setPlaceItem|zooViewMouse|placementCheck|stopMoving)")
FRAMEWORK_CLS = {"DX8Sound", "DX8SndMgr", "GXCanvas", "GXVidMan", "GXLLE", "UIManager", "UILayout", "UIControl",
                 "UIElement", "UIListBox", "UIListBoxEntry"}
SYSTEMS = [
    ("screens and buttons", re.compile(r"(_click$|_show$|_hide$|^on[A-Z]\w*(Click|Show|Hide)$)")),
    ("money", re.compile(r"(book|ledger|spendCash|monthly|sellValue|Price|price|wage|admission|Refund|refund|cash|"
                         r"marketing|^roll|Roll)")),
    ("tasks", re.compile(r"^Task")),
    ("jobs", re.compile(r"^Job")),
    ("behaviour scripts", re.compile(r"^(f[A-Z]|behaviourRun|startBehaviour|setBehaviourSet|playAscend|heliIdle|"
                                     r"caughtSetFor|moveFnBySpeed)")),
    ("scenarios and goals", re.compile(r"(scenario|goal|Goal|applyStart|startMap|takeWon)", re.I)),
    ("guests", re.compile(r"(guest|Guest|thought|needsTick|holdItem|holdTrash|releaseItem|chooseAttraction)")),
    ("animals", re.compile(r"(animal|Animal|fTarget|egg|Egg|prey|Prey|predator|Predator|chase|Chase)")),
    ("staff", re.compile(r"(keeper|Keeper|staff|Staff|maint|Maint|guide|Guide|heli|Heli|tranquil)")),
    ("exhibits and tanks", re.compile(r"(habitat|Habitat|exhibit|Exhibit|tank|Tank|fence|Fence|water|Water)")),
    ("placing and removing", re.compile(r"(place|Place|remove|Remove|bulldoze|sell|Sell|canPlace|undo|world[A-Z])")),
    ("research", re.compile(r"research", re.I)),
    ("zoo", re.compile(r"(zoo|Zoo|rating|Rating|advis|announce|Message|chance)", re.I)),
]


def system_of(name, cls):
    full = (cls + "::" if cls else "") + name
    if cls in FRAMEWORK_CLS or FRAMEWORK.match(name):
        return None
    for sysname, rx in SYSTEMS:
        if rx.search(full):
            return sysname
    if cls in ("ZTGuest",):
        return "guests"
    if cls in ("ZTAnimal", "ZTAnimalType"):
        return "animals"
    if cls in ("ZTStaff", "ZTKeeper", "ZTMaintenanceWorker", "ZTTourGuide", "ZTHelicopter"):
        return "staff"
    if cls in ("ZTGameMgr",):
        return "money"
    return "other gameplay" if cls.startswith("ZT") else None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=OUT)
    ap.add_argument("--quiet", action="store_true")
    a = ap.parse_args()

    # our side
    files, cited = [], collections.defaultdict(list)
    for folder, _, names in os.walk(SRC):
        for n in sorted(names):
            if not n.endswith(".cpp"):
                continue
            path = os.path.join(folder, n)
            rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
            kind = kind_of(rel)
            fns = functions(path)
            with open(path, encoding="utf-8", errors="replace") as f:
                text = f.read()
            for m in ADDR.finditer(text):
                va = int(m.group(1), 16)
                if TEXT_LO <= va < TEXT_HI:
                    cited["0x%06x" % va].append(rel)
            if kind in ("reader", "ours"):
                for fn in fns:
                    fn["marker"] = fn["marker"] or kind
            files.append({"file": rel, "kind": kind, "functions": fns})
    for folder, _, names in os.walk(SRC):  # headers cite too
        for n in names:
            if n.endswith((".hpp", ".h")):
                path = os.path.join(folder, n)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                with open(path, encoding="utf-8", errors="replace") as f:
                    for m in ADDR.finditer(f.read()):
                        va = int(m.group(1), 16)
                        if TEXT_LO <= va < TEXT_HI:
                            cited["0x%06x" % va].append(rel)

    ours = collections.OrderedDict()
    for f in files:
        if f["kind"] not in ("gameplay", "ui", "drawing"):
            continue
        c = collections.Counter(fn["marker"] or "unmarked" for fn in f["functions"] if fn["lines"] > 3)
        bare = [dict(b, fn=fn["name"]) for fn in f["functions"] for b in fn["bare"]]
        ours[f["file"]] = {"kind": f["kind"], "counts": dict(c), "bare": bare,
                           "unmarked": [{"name": fn["name"], "line": fn["line"]} for fn in f["functions"]
                                        if fn["lines"] > 3 and not fn["marker"]]}

    # the original's side
    with open(KB, encoding="utf-8") as fh:
        kb = json.load(fh)
    systems = collections.OrderedDict((s, {"ported": [], "missing": []}) for s, _ in SYSTEMS)
    systems["other gameplay"] = {"ported": [], "missing": []}
    for addr, e in sorted(kb["functions"].items()):
        s = system_of(e.get("name", ""), e.get("class", ""))
        if not s:
            continue
        row = {"address": addr, "name": (e.get("class") + "::" if e.get("class") else "") + e.get("name", ""),
               "provisional": bool(e.get("provisional")), "summary": (e.get("summary") or "")[:160]}
        if addr in cited:
            row["where"] = sorted(set(cited[addr]))[:3]
            systems[s]["ported"].append(row)
        else:
            systems[s]["missing"].append(row)

    totals = collections.Counter()
    for v in ours.values():
        for k, n in v["counts"].items():
            totals[k] += n
    out = {"ours": ours, "oursTotals": dict(totals), "original": systems,
           "originalTotals": {s: {"ported": len(v["ported"]), "missing": len(v["missing"])} for s, v in systems.items()}}
    os.makedirs(os.path.dirname(a.out), exist_ok=True)
    with open(a.out, "w", encoding="utf-8") as fh:
        json.dump(out, fh, indent=1)
    if not a.quiet:
        print("Our gameplay, UI and drawing functions (over 3 lines):", dict(totals))
        for f, v in ours.items():
            n = sum(v["counts"].values())
            print("  %-28s %-8s %4d  unmarked %-4d bare numbers %d" % (f, v["kind"], n, v["counts"].get("unmarked", 0),
                                                                      len(v["bare"])))
        print("The original's gameplay, by system (ported / named):")
        for s, v in out["originalTotals"].items():
            print("  %-24s %4d / %d" % (s, v["ported"], v["ported"] + v["missing"]))
        print("->", a.out)


if __name__ == "__main__":
    main()
