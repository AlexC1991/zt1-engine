"""Disassembles one zoo.exe function: follows its jumps (not calls) and gathers
its far chunks. Calls are labelled with the names worked out in Ghidra
(re/knowledge.json, and re/private/all_names.csv if ZT1_Sync wrote it), float
constants with their values, pushed message ids with their text (if
re/private/strings.pkl is there).

python re/rd.py <hex address> [outfile]
(pip install pefile capstone; ZT1_EXE=<path> for a zoo.exe elsewhere)"""
import sys, struct, pefile, capstone, pickle, os, csv
EXE = os.environ.get("ZT1_EXE", r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon\zoo.exe")
HERE = os.path.dirname(os.path.abspath(__file__))
pe = pefile.PE(EXE, fast_load=True)
base = pe.OPTIONAL_HEADER.ImageBase
data = pe.get_memory_mapped_image()
md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
md.detail = True
SP = os.path.join(HERE, "private", "strings.pkl")
strs = pickle.load(open(SP, "rb")) if os.path.exists(SP) else {}
def load_names():
    """address -> "Class::name", ours over the private list's."""
    names = {}
    for f in (os.path.join(HERE, "private", "all_names.csv"),):
        if not os.path.exists(f):
            continue
        with open(f, newline="", encoding="utf-8") as fh:
            for row in csv.DictReader(fh):
                row = {k.strip().lower(): (v or "").strip() for k, v in row.items() if k}
                try:
                    va = int(row.get("address", "").lower().replace("0x", ""), 16)
                except ValueError:
                    continue
                if row.get("name"):
                    names[va] = (row["class"] + "::" if row.get("class") else "") + row["name"]
    kb = os.path.join(HERE, "knowledge.json")
    if os.path.exists(kb):
        import json
        for k, e in json.load(open(kb, encoding="utf-8"))["functions"].items():
            if e.get("name"):
                names[int(k, 16)] = (e["class"] + "::" if e.get("class") else "") + e["name"]
    return names
names = load_names()
def insn_at(va):
    off = va - base
    for i in md.disasm(data[off:off+16], va):
        return i
    return None
def f32(va): return struct.unpack('<f', data[va-base:va-base+4])[0]
def f64(va): return struct.unpack('<d', data[va-base:va-base+8])[0]
import re
def annotate(i):
    s = f"{i.mnemonic} {i.op_str}"
    notes = []
    for m in re.finditer(r'(qword|dword) ptr \[(0x[0-9a-f]{6})\]', i.op_str):
        va = int(m.group(2), 16)
        if 0x5f0000 <= va < 0x700000:
            try:
                if m.group(1) == 'qword': notes.append(f"={f64(va)!r}")
                elif i.mnemonic.startswith('f'): notes.append(f"={f32(va)!r}")
            except Exception: pass
    if i.mnemonic in ('call', 'jmp') and i.op_str.startswith('0x'):
        t = int(i.op_str, 16)
        if t in names: notes.append(names[t])
    if i.mnemonic == 'push' and i.op_str.startswith('0x'):
        v = int(i.op_str, 16)
        if v in strs and v > 100: notes.append(f"str{v}={strs[v][:60]!r}")
    if notes: s += "    ; " + " ".join(notes)
    return s
def func(entry, limit=20000):
    seen = {}; work = [entry]; targets = set([entry])
    while work and len(seen) < limit:
        va = work.pop()
        while va not in seen:
            i = insn_at(va)
            if i is None: break
            seen[va] = i
            m = i.mnemonic
            if m.startswith('j') or m.startswith('loop'):
                op = i.operands[0] if i.operands else None
                if op is not None and op.type == capstone.x86.X86_OP_IMM:
                    t = op.imm; targets.add(t); work.append(t)
                if m == 'jmp': break
            if m in ('ret', 'retf', 'int3'): break
            va = va + i.size
    return seen, targets
if __name__ == "__main__":
    if len(sys.argv) < 2: sys.exit(__doc__)
    e = int(sys.argv[1], 16)
    if e in names: print(f"; {names[e]}")
    seen, targets = func(e)
    out = open(sys.argv[2], 'w') if len(sys.argv) > 2 else sys.stdout
    prev = None
    for va in sorted(seen):
        i = seen[va]
        if prev is not None and va != prev: out.write("        ; ----- gap -----\n")
        if va in targets: out.write(f"L_{va:x}:\n")
        out.write(f"{va:08x}  {annotate(i)}\n")
        prev = va + i.size
