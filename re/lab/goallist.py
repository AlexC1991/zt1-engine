"""The original's goal list for a scenario, as zoo.exe builds it (goalsLoad
0x58de8a), with the number of saved goal states (one per goal, a composite's
children after it).

python re/lab/goallist.py freeform/sm_aqua.scn [--list]

- a ScenarioGoal for the scenario itself first (ctor 0x58e218);
- then each [goals] goal= of the scenario's own file (makeGoal 0x58e066);
- then each [start] extragoals= entry: split at its '.', every resource whose
  path starts with the part before it and ends with the part after (0x4b41f2)
  - freeform/unlock.scn brings unlock01..unlock20 too - each file's goals;
- a goal with rtype 1 or 2 is a composite: its child= goals follow it.
The game's archives are searched in zoo.ini's [resource] path order; the first
copy of a path wins, as the resource manager keeps one file per path.
A zoo of version 85 or before holds only the states before the first
composite: a composite reads none then (0x5950fd)."""
import configparser, os, re, sys, zipfile

GAME = os.environ.get("ZT1_GAME", r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")


def archives():
    ini = open(os.path.join(GAME, "zoo.ini"), encoding="latin1").read()
    m = re.search(r"(?im)^\s*path\s*=\s*(.+)$", ini)
    paths = [p.strip() for p in m.group(1).split(";")] if m else ["."]
    out = []
    for p in paths:
        d = os.path.normpath(os.path.join(GAME, p))
        if not os.path.isdir(d):
            continue
        for f in sorted(os.listdir(d)):
            if f.lower().endswith(".ztd"):
                out.append(os.path.join(d, f))
    return out


def files():
    """path -> text, the first copy in search order."""
    seen = {}
    for a in archives():
        try:
            z = zipfile.ZipFile(a)
        except zipfile.BadZipFile:
            continue
        for n in z.namelist():
            k = n.lower().replace("\\", "/")
            if k.endswith(".scn") and k not in seen:
                seen[k] = z.read(n).decode("latin1")
    return seen


def sections(text):
    out, cur = {}, None
    for line in text.splitlines():
        line = line.split(";")[0].strip()
        m = re.match(r"\[(.+)\]", line)
        if m:
            cur = m.group(1).strip().lower()
            out.setdefault(cur, [])
            continue
        if cur is not None and "=" in line:
            k, v = line.split("=", 1)
            out[cur].append((k.strip().lower(), v.strip()))
    return out


def get(sec, key):
    return [v for k, v in sec if k == key]


def goal_tree(secs, name, depth=0):
    sec = secs.get(name.lower(), [])
    rtype = int((get(sec, "rtype") or ["0"])[0] or 0)
    node = {"name": name, "rtype": rtype, "children": []}
    if rtype in (1, 2):
        for child in get(sec, "child"):
            node["children"].append(goal_tree(secs, child, depth + 1))
    return node


def size(node):
    return 1 + sum(size(c) for c in node["children"])


def goal_list(scn, all_files):
    secs = sections(all_files[scn.lower()])
    goals = [{"name": "(scenario)", "rtype": -1, "children": [], "file": scn}]
    for g in get(secs.get("goals", []), "goal"):
        n = goal_tree(secs, g)
        n["file"] = scn
        goals.append(n)
    for extra in get(secs.get("start", []), "extragoals"):
        stem, dot, ext = extra.lower().rpartition(".")
        found = sorted(p for p in all_files if p.startswith(stem) and p.endswith("." + ext))
        for f in found:
            fs = sections(all_files[f])
            for g in get(fs.get("goals", []), "goal"):
                n = goal_tree(fs, g)
                n["file"] = f
                goals.append(n)
    return goals


if __name__ == "__main__":
    scn = sys.argv[1] if len(sys.argv) > 1 else "freeform/sm_aqua.scn"
    allf = files()
    goals = goal_list(scn, allf)
    print(f"{scn}: {len(goals)} goals, {sum(size(g) for g in goals)} saved states")
    if "--list" in sys.argv:
        def show(n, d=0):
            print("  " * d + f"{n['name']} ({n.get('file', '')}) rtype {n['rtype']}")
            for c in n["children"]:
                show(c, d + 1)
        for g in goals:
            show(g)


def flatten(goals):
    """Each saved state's goal, in the order they're written."""
    out = []
    def walk(n, d):
        out.append((n["name"], n.get("file", ""), d))
        for c in n["children"]:
            walk(c, d + 1)
    for g in goals:
        walk(g, 0)
    return out
