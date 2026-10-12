# Writes re/private/report.md for your AI assistant (or anyone helping): your open
# questions, each with its function's decompiled C, what's been added since
# the last report, the answers so far, and who's done what. Send it, or just
# say "read the report": it's in the repo folder (not committed).
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Write report
#@runtime Jython

from __future__ import print_function
import io, sys
sys.path.insert(0, getSourceFile().getParentFile().getAbsolutePath())
for _m in ("zt1re", "zt1db", "zt1ghidra"):  # (fresh each run: Jython keeps modules)
    sys.modules.pop(_m, None)
import zt1re, zt1db, zt1ghidra

MAX_C_LINES = 150


def count_by(items):
    out = {}
    for by in items:
        out[by] = out.get(by, 0) + 1
    return out


def main():
    root = zt1re.repo_root(getSourceFile())
    me = zt1re.me(root, askString)
    db = zt1db.load(root)
    since = db.get("last_report", "")
    w = []
    w.append("# zoo.exe reverse engineering report")
    w.append("")
    w.append("By %s, %s. Knowledge base: re/knowledge.json." % (me, zt1db.today()))
    w.append("")

    asks = [q for q in db["questions"] if q["status"] == "open" and q.get("for") == "assistant"]
    w.append("## Questions for the AI assistant (%d open)" % len(asks))
    w.append("")
    for q in asks:
        w.append("### %s at %s %s" % (q["id"], q["address"] or "-", q["function"]))
        w.append("")
        w.append("%s  " % q["question"])
        w.append("(asked by %s, %s)" % (q["by"], q["on"]))
        w.append("")
        va = zt1db.parse_int(q.get("address"))
        f = getFunctionContaining(toAddr(va)) if va is not None else None
        if f is not None:
            known = zt1ghidra.knowledge_plate(db, f.getEntryPoint().getOffset())
            if known:
                w.append("Known: " + known.replace("\n", " / "))
                w.append("")
            c = zt1ghidra.decompile(currentProgram, f, monitor).strip().split("\n")
            more = len(c) - MAX_C_LINES
            w.append("```c")
            w.extend(c[:MAX_C_LINES])
            if more > 0:
                w.append("// ... %d more lines" % more)
            w.append("```")
            w.append("")

    leads = [q for q in db["questions"] if q["status"] == "open" and q.get("for") == "user"]
    w.append("## Leads for you (%d open)" % len(leads))
    w.append("")
    for q in leads:
        w.append("- %s %s: %s (from %s)" % (q["id"], q["address"] or "", q["question"], q["by"]))
    w.append("")

    news = [l for l in db["log"] if l["on"] >= since] if since else db["log"]
    w.append("## Added since the last report%s (%d)" % (" (" + since + ")" if since else "", len(news)))
    w.append("")
    for l in news:
        w.append("- %s %s: %s" % (l["on"], l["by"], l["what"]))
    w.append("")

    done = [q for q in db["questions"] if q["status"] == "answered"]
    w.append("## Answered (%d)" % len(done))
    w.append("")
    for q in done:
        w.append("- %s %s: %s -> %s (%s, %s)" % (q["id"], q["address"], q["question"], q["answer"],
                                                q["answered_by"], q["answered_on"]))
    w.append("")

    w.append("## Who's done what")
    w.append("")
    funcs = count_by(e.get("by", "?") for e in db["functions"].values())
    fields = count_by(f.get("by", "?") for s in db["structs"].values() for f in s["fields"].values())
    nums = count_by(c.get("by", "?") for c in db["constants"].values())
    for who in sorted(set(list(funcs) + list(fields) + list(nums))):
        w.append("- %s: %d functions, %d struct fields, %d numbers" % (who, funcs.get(who, 0), fields.get(who, 0),
                                                                      nums.get(who, 0)))
    w.append("")

    out = zt1re.path(root, "re", "private", "report.md")
    f = io.open(out, "w", encoding="utf-8", newline="\n")
    f.write(u"\n".join(unicode_lines(w)) + u"\n")
    f.close()
    db["last_report"] = zt1db.today()
    zt1db.save(root, db)
    print("Wrote %s: %d questions for the AI assistant, %d leads, %d changes since the last report" % (out, len(asks), len(leads),
                                                                                          len(news)))


def unicode_lines(lines):
    out = []
    for l in lines:
        try:
            out.append(l if not isinstance(l, bytes) else l.decode("utf-8", "replace"))
        except:  # noqa: E722
            out.append(u"%s" % l)
    return out


main()
