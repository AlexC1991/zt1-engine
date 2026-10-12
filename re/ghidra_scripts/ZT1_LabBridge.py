# Starts (or stops) the bridge between this Ghidra and the ZT1 Lab
# (zt1-engine.exe --lab): a small web server on this PC only
# (127.0.0.1:18489) that the Lab asks for what Ghidra knows - every
# function, its decompiled C and assembly, what calls it and what it calls,
# the strings and numbers it uses - and that moves Ghidra's cursor to what
# you pick in the Lab. Run it once per session; run it again to stop it.
#@author zt1-engine
#@category zt1-engine
#@menupath Tools.zt1-engine.Start Lab bridge
#@runtime Jython

from __future__ import print_function
import io
import json
import os
import threading

from com.sun.net.httpserver import HttpHandler, HttpServer
from ghidra.app.decompiler import DecompInterface
from ghidra.app.services import GoToService
from ghidra.util.task import TaskMonitor
from java.lang import System, Runnable
from java.net import InetSocketAddress, URLDecoder
from java.util.concurrent import Executors
from javax.swing import SwingUtilities

PORT = 18489
KEY = "zt1.lab.bridge"

program = currentProgram
tool = state.getTool()
fm = program.getFunctionManager()
listing = program.getListing()
lock = threading.Lock()
decomp = DecompInterface()
decomp.openProgram(program)
# (the script's own monitor ends with it; the bridge outlives the script)
MON = TaskMonitor.DUMMY


def addr(text):
    return program.getAddressFactory().getDefaultAddressSpace().getAddress(int(text, 16))


def fn_at(text):
    a = addr(text)
    return fm.getFunctionContaining(a), a


def fn_row(f):
    return {"address": "0x%06x" % f.getEntryPoint().getOffset(), "name": f.getName(),
            "class": "" if f.getParentNamespace().isGlobal() else f.getParentNamespace().getName(True),
            "size": f.getBody().getNumAddresses(),
            "tags": sorted(t.getName() for t in f.getTags())}


def h_status(q):
    return {"program": program.getName(), "functions": fm.getFunctionCount(), "sha256": program.getExecutableSHA256()}


def h_functions(q):
    out = []
    for f in fm.getFunctions(True):
        if not f.isThunk():
            out.append(fn_row(f))
    return {"functions": out}


def h_function(q):
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    row = fn_row(f)
    row["signature"] = f.getSignature().getPrototypeString()
    row["comment"] = f.getComment() or ""
    row["callers"] = [fn_row(c) for c in f.getCallingFunctions(MON)]
    row["callees"] = [fn_row(c) for c in f.getCalledFunctions(MON)]
    # the strings and numbers it uses
    strings, scalars = [], []
    for ins in listing.getInstructions(f.getBody(), True):
        for ref in ins.getReferencesFrom():
            d = listing.getDataAt(ref.getToAddress())
            if d is not None and d.hasStringValue():
                strings.append({"at": "0x%06x" % ins.getAddress().getOffset(), "text": str(d.getValue())[:200]})
        for i in range(ins.getNumOperands()):
            sc = ins.getScalar(i)
            if sc is not None and abs(sc.getSignedValue()) >= 16:
                scalars.append({"at": "0x%06x" % ins.getAddress().getOffset(), "value": sc.getSignedValue()})
    row["strings"] = strings[:200]
    row["scalars"] = scalars[:400]
    return row


def h_decompile(q):
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    with lock:
        r = decomp.decompileFunction(f, 60, MON)
    d = r.getDecompiledFunction() if r is not None else None
    return {"address": "0x%06x" % f.getEntryPoint().getOffset(), "name": f.getName(True),
            "c": d.getC() if d is not None else "// (the decompiler couldn't: %s)" % (r.getErrorMessage() if r else "?")}


def h_disasm(q):
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    lines = []
    for ins in listing.getInstructions(f.getBody(), True):
        eol = ins.getComment(0) or ""  # (CodeUnit.EOL_COMMENT)
        lines.append({"at": "0x%06x" % ins.getAddress().getOffset(), "text": str(ins), "comment": eol})
        if len(lines) >= 4000:
            break
    return {"address": "0x%06x" % f.getEntryPoint().getOffset(), "lines": lines}


def h_xrefs(q):
    a = addr(q["addr"])
    out = []
    for ref in program.getReferenceManager().getReferencesTo(a):
        f = fm.getFunctionContaining(ref.getFromAddress())
        out.append({"from": "0x%06x" % ref.getFromAddress().getOffset(), "type": str(ref.getReferenceType()),
                    "function": f.getName(True) if f else ""})
        if len(out) >= 500:
            break
    return {"refs": out}


def h_goto(q):
    a = addr(q["addr"])

    class Go(Runnable):
        def run(self):
            svc = tool.getService(GoToService) if tool else None
            if svc:
                svc.goTo(a)
    SwingUtilities.invokeLater(Go())
    return {"ok": True}


def h_search(q):
    needle = q.get("q", "").lower()
    out = []
    for f in fm.getFunctions(True):
        if needle in f.getName(True).lower():
            out.append(fn_row(f))
            if len(out) >= 300:
                break
    return {"functions": out}


def h_rename(q):
    """A name (and class) for a function, set in Ghidra as yours."""
    from ghidra.program.model.symbol import SourceType
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    name, cls, by = q.get("name", "").strip(), q.get("class", "").strip(), q.get("by", "").strip()
    if not name:
        return {"error": "no name"}
    tx = program.startTransaction("ZT1 Lab: name " + name)
    ok = False
    try:
        f.setName(name, SourceType.USER_DEFINED)
        st = program.getSymbolTable()
        ns = program.getGlobalNamespace()
        for part in [p for p in cls.split("::") if p.strip()]:
            found = st.getNamespace(part.strip(), ns)
            ns = found if found is not None else st.createNameSpace(ns, part.strip(), SourceType.USER_DEFINED)
        f.setParentNamespace(ns)
        for t in list(f.getTags()):
            if t.getName().startswith("src:") or t.getName().startswith("by:") or t.getName() == "zt1:provisional":
                f.removeTag(t.getName())
        f.addTag("src:zt1-engine")
        if by:
            f.addTag("by:" + by)
        ok = True
    finally:
        program.endTransaction(tx, ok)
    return {"ok": ok, "name": f.getName(True)}


# Functions the Lab made (code Ghidra's analysis missed - reached only
# through a table - so it had no C): re/made_functions.json lists their
# starts, so they're made again each time the bridge starts (it opens the
# program read only) and ZT1_Apply makes them for good in your project
import zt1re  # noqa: E402 (beside this script)
RE_DIR = os.path.join(str(zt1re.repo_root(getSourceFile())), "re")
MADE = os.path.join(RE_DIR, "made_functions.json")


def made_list():
    try:
        with io.open(MADE, encoding="utf-8") as fh:
            return json.load(fh)
    except Exception:  # noqa: BLE001 (none yet)
        return []


def make_fn(a):
    from ghidra.app.cmd.function import CreateFunctionCmd
    f = fm.getFunctionAt(a)
    if f is not None:
        return f, ""
    cmd = CreateFunctionCmd(a)
    tx = program.startTransaction("ZT1 Lab: make a function")
    ok = False
    try:
        ok = cmd.applyTo(program, MON)
        if ok:
            fm.getFunctionAt(a).addTag("zt1:made")
    finally:
        program.endTransaction(tx, ok)
    return (fm.getFunctionAt(a) if ok else None), (cmd.getStatusMsg() or "")


def h_makefn(q):
    """A function made at addr (its C can then be decompiled), and kept in
    made_functions.json."""
    a = addr(q["addr"])
    inside = fm.getFunctionContaining(a)
    if inside is not None and inside.getEntryPoint() != a:
        return {"error": "that's inside %s already" % inside.getName(True), "function": fn_row(inside)}
    f, why = make_fn(a)
    if f is None:
        return {"error": "Ghidra couldn't make a function there" + (": " + why if why else "")}
    made = made_list()
    key = "0x%06x" % a.getOffset()
    if not any(m.get("addr") == key for m in made):
        made.append({"addr": key, "by": q.get("by", ""), "size": f.getBody().getNumAddresses()})
        made.sort(key=lambda m: m["addr"])
        with io.open(MADE, "w", encoding="utf-8") as fh:
            fh.write(json.dumps(made, indent=1, ensure_ascii=False) + u"\n")
    return {"ok": True, "function": fn_row(f)}


def h_owners(q):
    """The function each address is in, by Ghidra (a function's code needn't
    be in one piece: where it starts and its size don't say)."""
    out = {}
    for t in [x for x in q.get("addrs", "").split(",") if x]:
        f = fm.getFunctionContaining(addr(t))
        out[t] = {"address": "0x%06x" % f.getEntryPoint().getOffset(), "name": f.getName(True)} if f else None
    return {"owners": out}


STRINGS = []  # (address, text), read once


def h_strings(q):
    """The program's strings containing q (any case), each with the
    functions that use it: how a search for "credits" finds the code."""
    needle = q.get("q", "").lower()
    if len(needle) < 2:
        return {"strings": []}
    if not STRINGS:
        for d in listing.getDefinedData(True):
            if d.hasStringValue():
                try:
                    STRINGS.append((d.getAddress(), str(d.getValue())))
                except:  # noqa: E722
                    pass
    out = []
    rm = program.getReferenceManager()
    for a, text in STRINGS:
        if needle not in text.lower():
            continue
        users = []
        for ref in rm.getReferencesTo(a):
            f = fm.getFunctionContaining(ref.getFromAddress())
            if f is not None:
                users.append({"address": "0x%06x" % f.getEntryPoint().getOffset(), "name": f.getName(True),
                              "at": "0x%06x" % ref.getFromAddress().getOffset()})
        out.append({"address": "0x%06x" % a.getOffset(), "text": text[:200], "users": users[:20]})
        if len(out) >= 60:
            break
    return {"strings": out, "total": len(STRINGS)}


DEBUG_NAME = None


def h_debugnames(q):
    """Every function using the game's own debug text that names a method -
    "GXVidMan::setupWindowed - SetCooperativeLevel" - with those names: what
    the developers called it, often. {"0x52f1d0": ["GXVidMan::setupWindowed"]}"""
    global DEBUG_NAME
    import re
    if DEBUG_NAME is None:
        DEBUG_NAME = re.compile(r"^\s*([A-Za-z_]\w*(?:::~?[A-Za-z_]\w*)+)\s*(?:-|:|\(|$)")
    if not STRINGS:
        for d in listing.getDefinedData(True):
            if d.hasStringValue():
                try:
                    STRINGS.append((d.getAddress(), str(d.getValue())))
                except:  # noqa: E722
                    pass
    out = {}
    rm = program.getReferenceManager()
    for a, text in STRINGS:
        m = DEBUG_NAME.match(text)
        if not m:
            continue
        for ref in rm.getReferencesTo(a):
            f = fm.getFunctionContaining(ref.getFromAddress())
            if f is None:
                continue
            k = "0x%06x" % f.getEntryPoint().getOffset()
            names = out.setdefault(k, [])
            if m.group(1) not in names:
                names.append(m.group(1))
    return {"functions": out}


def h_fnstrings(q):
    """Every function's text (the strings it uses, a few each): what it's
    about, when nothing names it - "ui/mapselect/..." in a menu's code.
    {"0x41b100": ["ui/...", ...]}"""
    if not STRINGS:
        for d in listing.getDefinedData(True):
            if d.hasStringValue():
                try:
                    STRINGS.append((d.getAddress(), str(d.getValue())))
                except:  # noqa: E722
                    pass
    out = {}
    rm = program.getReferenceManager()
    for a, text in STRINGS:
        t = text.strip()
        if len(t) < 3:
            continue
        for ref in rm.getReferencesTo(a):
            f = fm.getFunctionContaining(ref.getFromAddress())
            if f is None:
                continue
            k = "0x%06x" % f.getEntryPoint().getOffset()
            texts = out.setdefault(k, [])
            if len(texts) < 8 and t[:100] not in texts:
                texts.append(t[:100])
    return {"functions": out}


SCALARS = {}


def h_scalar(q):
    """The functions using a number (an instruction operand): a message id
    (10028, "%s is not happy."), a task number (0x75), a UI element (0x11cb).
    The index is made the first time it's asked (every instruction once).
    {"value": n, "uses": [{"address", "name", "at"}]}"""
    if not SCALARS:
        for ins in listing.getInstructions(True):
            for i in range(ins.getNumOperands()):
                sc = ins.getScalar(i)
                if sc is None:
                    continue
                v = sc.getUnsignedValue() & 0xffffffff
                if v < 16:
                    continue
                SCALARS.setdefault(v, []).append(ins.getAddress())
    v = int(q.get("value", "0"), 0) & 0xffffffff
    uses, seen = [], set()
    for a in SCALARS.get(v, []):
        f = fm.getFunctionContaining(a)
        key = (f.getEntryPoint().getOffset() if f else None, a.getOffset())
        if key in seen:
            continue
        seen.add(key)
        uses.append({"address": "0x%06x" % f.getEntryPoint().getOffset() if f else "",
                     "name": f.getName(True) if f else "", "at": "0x%06x" % a.getOffset()})
        if len(uses) >= 300:
            break
    return {"value": v, "uses": uses}


def h_fncallers(q):
    """How many places call each function (the functions calling it, each
    once): one caller and it's that one's part; many, a shared helper.
    {"0x4a84f3": 5, ...} - only functions something calls"""
    out = {}
    rm = program.getReferenceManager()
    for f in fm.getFunctions(True):
        callers = set()
        for ref in rm.getReferencesTo(f.getEntryPoint()):
            if not ref.getReferenceType().isCall():
                continue
            c = fm.getFunctionContaining(ref.getFromAddress())
            if c is not None:
                callers.add(c.getEntryPoint().getOffset())
        if callers:
            out["0x%06x" % f.getEntryPoint().getOffset()] = len(callers)
    return {"functions": out}


ROUTES = {"/scalar": h_scalar, "/fncallers": h_fncallers, "/owners": h_owners, "/debugnames": h_debugnames, "/fnstrings": h_fnstrings, "/makefn": h_makefn, "/rename": h_rename, "/strings": h_strings,"/status": h_status, "/functions": h_functions, "/function": h_function, "/decompile": h_decompile,
          "/disasm": h_disasm, "/xrefs": h_xrefs, "/goto": h_goto, "/search": h_search}


class Handler(HttpHandler):
    def handle(self, ex):
        try:
            q = {}
            raw = ex.getRequestURI().getRawQuery()
            if raw:
                for part in raw.split("&"):
                    k, _, v = part.partition("=")
                    q[k] = URLDecoder.decode(v, "UTF-8")
            route = ROUTES.get(ex.getRequestURI().getPath())
            body = route(q) if route else {"error": "no such request"}
            code = 200 if route else 404
        except:  # noqa: E722 (Java's exceptions too)
            import sys
            body, code = {"error": str(sys.exc_info()[1])}, 500
        data = json.dumps(body).encode("utf-8")
        ex.getResponseHeaders().add("Content-Type", "application/json")
        ex.sendResponseHeaders(code, len(data))
        out = ex.getResponseBody()
        out.write(data)
        out.close()


def main():
    old = System.getProperties().get(KEY)
    if old is not None:
        old.stop(0)
        System.getProperties().remove(KEY)
        print("ZT1 Lab bridge stopped.")
        return
    # (the functions the Lab made before, made again: the program's read
    # only here, so last session's are gone)
    again = 0
    for m in made_list():
        f, _ = make_fn(addr(m["addr"]))
        again += 1 if f is not None else 0
    server = HttpServer.create(InetSocketAddress("127.0.0.1", PORT), 0)
    server.createContext("/", Handler())
    server.setExecutor(Executors.newFixedThreadPool(2))
    server.start()
    System.getProperties().put(KEY, server)
    print("ZT1 Lab bridge on 127.0.0.1:%d for %s (%d functions, %d made by the Lab). Run this again to stop it."
          % (PORT, program.getName(), fm.getFunctionCount(), again))


main()
