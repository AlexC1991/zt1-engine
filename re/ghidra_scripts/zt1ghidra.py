# Ghidra helpers for the ZT1_*.py scripts: functions, structs, types,
# signatures, the decompiler, what's under the cursor.
from __future__ import print_function
import re, sys

import zt1db

from ghidra.app.cmd.disassemble import DisassembleCommand
from ghidra.app.cmd.function import ApplyFunctionSignatureCmd, CreateFunctionCmd
from ghidra.app.decompiler import DecompInterface
from ghidra.app.util.parser import FunctionSignatureParser
from ghidra.program.model.data import (BooleanDataType, ByteDataType, CategoryPath, CharDataType, DataTypeConflictHandler,
                                       DoubleDataType, DWordDataType, FloatDataType, IntegerDataType, PointerDataType,
                                       ShortDataType, StructureDataType, Undefined4DataType, UnsignedIntegerDataType,
                                       UnsignedShortDataType, VoidDataType, WordDataType)
from ghidra.program.model.symbol import SourceType
from java.util import ArrayList

CATEGORY = CategoryPath("/zt1")
SIMPLE = {
    "int": IntegerDataType.dataType, "uint": UnsignedIntegerDataType.dataType, "float": FloatDataType.dataType,
    "double": DoubleDataType.dataType, "byte": ByteDataType.dataType, "char": CharDataType.dataType,
    "bool": BooleanDataType.dataType, "short": ShortDataType.dataType, "ushort": UnsignedShortDataType.dataType,
    "dword": DWordDataType.dataType, "word": WordDataType.dataType, "pointer": PointerDataType.dataType,
    "ptr": PointerDataType.dataType, "void": VoidDataType.dataType, "undefined4": Undefined4DataType.dataType,
}


def has_tag(f, name):
    for t in f.getTags():
        if t.getName() == name:
            return True
    return False


def namespace_for(program, cls):
    """The namespace "A::B" (made if it isn't there), the global one for ""."""
    st = program.getSymbolTable()
    ns = program.getGlobalNamespace()
    for part in [p.strip() for p in (cls or "").split("::") if p.strip()]:
        found = st.getNamespace(part, ns)
        ns = found if found is not None else st.createNameSpace(ns, part, SourceType.IMPORTED)
    return ns


def function_at(program, monitor, addr):
    """The function starting at addr, made there if Ghidra's analysis missed it."""
    fm = program.getFunctionManager()
    f = fm.getFunctionAt(addr)
    if f is not None:
        return f
    if program.getListing().getInstructionAt(addr) is None:
        DisassembleCommand(addr, None, True).applyTo(program, monitor)
    if CreateFunctionCmd(addr).applyTo(program, monitor):
        return fm.getFunctionAt(addr)
    return None


def knowledge_plate(db, va):
    """The plate comment text for a function: what's known, and your comment."""
    text = zt1db.describe_function(db, va)
    e = db["functions"].get(zt1db.addr_key(va), {})
    if e.get("comment"):
        text += "\n%s  (%s)" % (e["comment"], e.get("comment_by", ""))
    seen = zt1db.seen_text(db, va)
    if seen:
        text = (text + "\n" if text else "") + seen
    slots = zt1db.class_of_function(db, va)
    if slots:
        text = (text + "\n" if text else "") + "In the function tables of: " + ", ".join(
            "%s (+%s)" % (c, s) for c, s in slots[:8]) + (" and %d more" % (len(slots) - 8) if len(slots) > 8 else "")
    return text


def apply_signature(program, monitor, f, sig):
    try:
        fdt = FunctionSignatureParser(program.getDataTypeManager(), None).parse(f.getSignature(), sig)
        if not ApplyFunctionSignatureCmd(f.getEntryPoint(), fdt, SourceType.USER_DEFINED).applyTo(program, monitor):
            print("Couldn't apply the signature of %s: %s" % (f.getName(), sig))
    except:  # noqa: E722
        print("Couldn't read the signature of %s (%s): %s" % (f.getName(), sig, sys.exc_info()[1]))


def struct(dtm, name):
    """The struct /zt1/<name>, made (empty) if it isn't there."""
    dt = dtm.getDataType(CATEGORY, name)
    if dt is None:
        dt = dtm.addDataType(StructureDataType(CATEGORY, name, 0, dtm), DataTypeConflictHandler.DEFAULT_HANDLER)
    return dt


def parse_type(dtm, text):
    """int, float, byte, ..., a /zt1 struct's name, "X *" a pointer to one."""
    t = (text or "int").strip()
    if t.endswith("*"):
        return dtm.getPointer(parse_type(dtm, t[:-1]))
    if t in SIMPLE:
        return SIMPLE[t]
    dt = dtm.getDataType(CATEGORY, t)
    if dt is not None:
        return dt
    found = ArrayList()
    dtm.findDataTypes(t, found)
    return found.get(0) if found.size() else None


def at_least(st, size):
    """Grows a struct to size bytes (so the decompiler doesn't read an offset
    past its last known field as the next struct along: param_1[1].field_0x4)."""
    have = 0 if st.isZeroLength() else st.getLength()
    if size and have < size:
        st.growStructure(size - have)


def set_field(dtm, st, off, fld):
    """One field into a struct, growing it if need be; an error message or None."""
    dt = parse_type(dtm, fld.get("type"))
    if dt is None:
        return "no type called %s" % fld.get("type")
    length = dt.getLength()
    if length <= 0:
        return "%s has no size" % fld.get("type")
    size = 0 if st.isZeroLength() else st.getLength()  # (an empty one says 1)
    if size < off + length:
        st.growStructure(off + length - size)
    comment = "%s (%s, by %s)" % (fld.get("summary", ""), fld.get("confidence", "sure"), fld.get("by", "?"))
    try:
        st.replaceAtOffset(off, dt, length, fld.get("name"), comment)
    except:  # noqa: E722 (Jython: Java's exceptions aren't Exception)
        return str(sys.exc_info()[1])
    return None


def decompile(program, f, monitor, timeout=60):
    di = DecompInterface()
    try:
        di.openProgram(program)
        r = di.decompileFunction(f, timeout, monitor)
        d = r.getDecompiledFunction() if r is not None else None
        return d.getC() if d is not None else ""
    finally:
        di.dispose()


def number_under_cursor(location):
    """The number under the cursor, in the decompiler or the listing (None if none)."""
    text = None
    try:
        tok = location.getToken()
        text = tok.getText() if tok is not None else None
    except:  # noqa: E722
        pass
    if text is None:
        try:
            text = location.getOperandRepresentation()
        except:  # noqa: E722
            text = None
    if not text:
        return None
    m = re.search(r"0x[0-9a-fA-F]+|\b\d+\b", str(text))
    return zt1db.parse_int(m.group(0)) if m else None
