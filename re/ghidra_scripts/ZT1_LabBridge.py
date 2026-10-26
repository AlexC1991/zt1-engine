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


# ---------------------------------------------------------------------------
# Deeper questions (after rea's Ghidra bridge, github.com/morluto/rea, MIT,
# Copyright (c) 2026 morluto - its ideas, written again for this bridge):
# a data type's layout, a function's blocks and switch tables, what each of
# its calls goes to, a byte pattern with wildcards, where a value comes from
# or goes to, and the functions marked as never returning.
# ---------------------------------------------------------------------------

def hexa(a):
    return "0x%06x" % a.getOffset()


def fn_name_at(a):
    f = fm.getFunctionContaining(a)
    return f.getName(True) if f else ""


def h_datatype(q):
    """A data type by name (a struct, union, enum or typedef): its size and
    fields; a part of a name lists the types that match."""
    from ghidra.program.model.data import Structure, Union, TypeDef
    from ghidra.program.model.data import Enum as GEnum
    want = q.get("name", "").strip().lower()
    if not want:
        return {"error": "name is needed"}
    exact, near = [], []
    it = program.getDataTypeManager().getAllDataTypes()
    while it.hasNext():
        dt = it.next()
        n = dt.getName().lower()
        if n == want:
            exact.append(dt)
        elif want in n and len(near) < 60:
            near.append(dt)
    if not exact:
        return {"matches": [{"name": d.getName(), "path": d.getCategoryPath().getPath(), "size": d.getLength()} for d in near]}
    dt = exact[0]
    out = {"name": dt.getName(), "path": dt.getCategoryPath().getPath(), "size": dt.getLength(), "kind": dt.__class__.__name__}
    if isinstance(dt, (Structure, Union)):
        out["fields"] = [{"offset": "0x%x" % c.getOffset(), "size": c.getLength(), "name": c.getFieldName() or "",
                          "type": c.getDataType().getDisplayName(), "comment": c.getComment() or ""}
                         for c in dt.getDefinedComponents()]
    elif isinstance(dt, GEnum):
        out["values"] = [{"name": n, "value": dt.getValue(n)} for n in dt.getNames()]
    elif isinstance(dt, TypeDef):
        out["of"] = dt.getBaseDataType().getDisplayName()
    if len(exact) > 1:
        out["others"] = [d.getCategoryPath().getPath() for d in exact[1:]]
    return out


def h_blocks(q):
    """A function's basic blocks (where each goes) and its switch tables: each
    case's value and where it jumps (the decompiler's jump tables)."""
    from ghidra.program.model.block import BasicBlockModel
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    blocks = []
    it = BasicBlockModel(program).getCodeBlocksContaining(f.getBody(), MON)
    while it.hasNext() and len(blocks) < 2000:
        b = it.next()
        to = []
        di = b.getDestinations(MON)
        while di.hasNext():
            d = di.next()
            to.append({"to": hexa(d.getDestinationAddress()), "flow": str(d.getFlowType())})
        blocks.append({"start": hexa(b.getMinAddress()), "end": hexa(b.getMaxAddress()), "flow": str(b.getFlowType()), "to": to})
    switches = []
    with lock:
        r = decomp.decompileFunction(f, 60, MON)
    hf = r.getHighFunction() if r is not None else None
    if hf is not None:
        for jt in hf.getJumpTables():
            cases = jt.getCases()
            labels = jt.getLabelValues()
            switches.append({"at": hexa(jt.getSwitchAddress()),
                             "cases": [{"value": (int(labels[i]) if labels is not None and i < len(labels) else None), "to": hexa(cases[i])}
                                       for i in range(len(cases))]})
    return {"address": hexa(f.getEntryPoint()), "name": f.getName(True), "blocks": blocks, "switches": switches}


def h_calltargets(q):
    """Every call a function makes and where it goes: direct calls their
    function; an import its name; an indirect call through a class table
    ([reg + 0x16c]) its register and slot, for the Lab to match against the
    class tables it knows."""
    import re
    f, a = fn_at(q["addr"])
    if f is None:
        return {"error": "no function at " + q["addr"]}
    out = []
    for ins in listing.getInstructions(f.getBody(), True):
        if not ins.getFlowType().isCall():
            continue
        row = {"at": hexa(ins.getAddress()), "text": str(ins)}
        flows = ins.getFlows()
        if flows is not None and len(flows) > 0 and not ins.getFlowType().isComputed():
            row["kind"] = "direct"
            row["to"] = [{"address": hexa(t), "name": fn_name_at(t)} for t in flows]
        else:
            row["kind"] = "indirect"
            m = re.search(r"\[(\w+)(?:\s*\+\s*(0x[0-9a-fA-F]+))?\]", str(ins))
            if m and not m.group(1).startswith("0x"):
                row["base"] = m.group(1)
                row["slot"] = m.group(2) or "0x0"
            to = []
            for ref in ins.getReferencesFrom():
                t = ref.getToAddress()
                sym = program.getSymbolTable().getPrimarySymbol(t)
                tf = fm.getFunctionAt(t)
                if tf is not None:
                    to.append({"address": hexa(t), "name": tf.getName(True)})
                elif sym is not None:
                    to.append({"address": hexa(t), "name": sym.getName(True)})
            if to:
                row["to"] = to
        out.append(row)
    return {"address": hexa(f.getEntryPoint()), "name": f.getName(True), "calls": out}


def h_bytes(q):
    """A byte pattern anywhere in zoo.exe ("8b 0d ?? ?? 63 00": ?? any byte),
    each hit with its instruction and function."""
    from jarray import array
    parts = q.get("pattern", "").replace(",", " ").split()
    if not parts:
        return {"error": "pattern is needed, e.g. 8b 0d ?? ?? 63 00"}
    vals, mask = [], []
    for p in parts:
        if p in ("?", "??"):
            vals.append(0)
            mask.append(0)
        else:
            v = int(p, 16)
            vals.append(v - 256 if v > 127 else v)
            mask.append(-1)
    limit = min(int(q.get("limit", "200")), 2000)
    mem = program.getMemory()
    b, m = array(vals, "b"), array(mask, "b")
    hits = []
    at = mem.findBytes(mem.getMinAddress(), b, m, True, MON)
    while at is not None and len(hits) < limit:
        ins = listing.getInstructionContaining(at)
        hits.append({"at": hexa(at), "instruction": str(ins) if ins else "", "function": fn_name_at(at)})
        try:
            at = mem.findBytes(at.add(1), b, m, True, MON)
        except:  # noqa: E722 (past the end)
            break
    return {"pattern": " ".join(parts), "hits": hits, "more": at is not None}


def vn_text(v):
    if v is None:
        return ""
    if v.isConstant():
        return "0x%x" % v.getOffset()
    hv = v.getHigh()
    name = hv.getName() if hv is not None else None
    if name and name != "UNNAMED":
        return name
    if v.isAddress():
        s = program.getSymbolTable().getPrimarySymbol(v.getAddress())
        return s.getName(True) if s else hexa(v.getAddress())
    return str(v)


def op_row(op, depth, vn=None):
    row = {"at": hexa(op.getSeqnum().getTarget()), "op": op.getMnemonic(), "depth": depth,
           "out": vn_text(op.getOutput()), "in": [vn_text(i) for i in op.getInputs()]}
    if op.getMnemonic() in ("CALL", "CALLIND"):
        t = op.getInput(0)
        if op.getMnemonic() == "CALL" and t.isAddress():
            row["calls"] = fn_name_at(t.getAddress()) or hexa(t.getAddress())
        if vn is not None:
            for i in range(1, op.getNumInputs()):
                if op.getInput(i) == vn:
                    row["argument"] = i
    return row


def h_flow(q):
    """Where a value goes, or where it comes from, inside one function (the
    decompiler's def-use chains): from the instruction at "at", forward - the
    values it makes, through each use, to calls (which argument), stores and
    the return - or back - each input to what made it."""
    at = addr(q["at"])
    f = fm.getFunctionContaining(at)
    if f is None:
        return {"error": "no function holds " + q["at"]}
    back = q.get("dir", "forward") == "back"
    with lock:
        r = decomp.decompileFunction(f, 60, MON)
    hf = r.getHighFunction() if r is not None else None
    if hf is None:
        return {"error": "the decompiler couldn't"}
    start = list(hf.getPcodeOps(at))
    if not start:
        return {"error": "no p-code at " + q["at"] + " (an instruction's own address is needed)"}
    rows, seen = [], set()
    todo = []
    for op in start:
        rows.append(op_row(op, 0))
        seen.add(op.getSeqnum())
        if back:
            todo += [(v, 1) for v in op.getInputs()]
        elif op.getOutput() is not None:
            todo.append((op.getOutput(), 1))
    stop = ("CALL", "CALLIND", "STORE", "RETURN", "BRANCHIND", "CBRANCH")
    while todo and len(rows) < 200:
        vn, depth = todo.pop(0)
        if depth > 12:
            continue
        nexts = [vn.getDef()] if back else list(vn.getDescendants())
        for op in nexts:
            if op is None or op.getSeqnum() in seen:
                continue
            seen.add(op.getSeqnum())
            rows.append(op_row(op, depth, None if back else vn))
            if back:
                todo += [(v, depth + 1) for v in op.getInputs() if not v.isConstant()]
            elif op.getMnemonic() not in stop and op.getOutput() is not None:
                todo.append((op.getOutput(), depth + 1))
    return {"function": f.getName(True), "address": hexa(f.getEntryPoint()), "dir": "back" if back else "forward", "steps": rows}


def h_noreturn(q):
    """The functions Ghidra has marked as never returning (a wrong one cuts
    the decompiled C of every caller short), with how many call it."""
    out = []
    for f in fm.getFunctions(True):
        if f.hasNoReturn():
            out.append({"address": hexa(f.getEntryPoint()), "name": f.getName(True), "callers": len(f.getCallingFunctions(MON)),
                        "thunk": f.isThunk()})
    return {"functions": out}


ROUTES = {"/datatype": h_datatype, "/blocks": h_blocks, "/calltargets": h_calltargets, "/bytes": h_bytes, "/flow": h_flow, "/noreturn": h_noreturn,
          "/scalar": h_scalar, "/fncallers": h_fncallers, "/owners": h_owners, "/debugnames": h_debugnames, "/fnstrings": h_fnstrings, "/makefn": h_makefn, "/rename": h_rename, "/strings": h_strings,"/status": h_status, "/functions": h_functions, "/function": h_function, "/decompile": h_decompile,
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
