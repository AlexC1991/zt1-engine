# zoo.exe's machine code for the Lab, where Ghidra has nothing to show:
#   code   - the instructions around an address Ghidra never made a function
#            of (code only reached through a table), from where that code
#            starts (a guess: just after the end of the code before it)
#   field  - every instruction touching an offset ("+ 0x3d6]"): who reads or
#            writes a field, whatever the function
#   value  - every instruction using a number as itself (push 0x282a): who
#            uses a message id, a constant
#   readers - every key the config loaders read, and where each goes (a
#            field or a global): re/lab/loaders.json says which are readers
#   strings - the text at addresses
#   vtable - a class's table of functions
# Runs as the Lab's helper (lab-app/main.js): one JSON request a line on stdin,
# one answer a line on stdout. zoo.exe's .text is read once (capstone, a
# straight sweep) and kept, and saved in the cache folder for the next start.
#   python zoobin.py <zoo.exe> [cache folder]
import bisect
import json
import os
import pickle
import re
import sys

import capstone
import pefile

EXE = sys.argv[1]
CACHE = sys.argv[2] if len(sys.argv) > 2 else None

pe = pefile.PE(EXE, fast_load=True)
BASE = pe.OPTIONAL_HEADER.ImageBase
TEXT = [s for s in pe.sections if s.Name.rstrip(b'\0') == b'.text'][0]


def sweep():
    key = '%s-%d' % (os.path.basename(EXE), os.path.getsize(EXE))
    path = os.path.join(CACHE, 'zoobin-%s.pkl' % key) if CACHE else None
    if path and os.path.exists(path):
        with open(path, 'rb') as f:
            return pickle.load(f)
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.skipdata = True
    at, text = [], []
    for i in md.disasm_lite(TEXT.get_data(), BASE + TEXT.VirtualAddress):
        at.append(i[0])
        text.append((i[2] + ' ' + i[3]).strip())
    if path:
        os.makedirs(CACHE, exist_ok=True)
        with open(path, 'wb') as f:
            pickle.dump((at, text), f)
    return at, text


AT, TXT = sweep()
HEX = re.compile(r'0x[0-9a-f]+')


def string_at(va):
    # (text the game's data holds there: printable, ended by a 0)
    try:
        b = pe.get_data(va - BASE, 80).split(b'\0')[0]
    except Exception:
        return ''
    if len(b) < 3 or any(c < 32 or c > 126 for c in b):
        return ''
    return b.decode('latin1')


def line(k):
    t = TXT[k]
    out = {'at': '0x%06x' % AT[k], 'text': t}
    for h in HEX.findall(t):
        v = int(h, 16)
        if 0x600000 <= v < 0x700000:
            s = string_at(v)
            if s:
                out['str'] = s
                break
    return out


def ends(t):
    return t == 'ret' or t.startswith('ret ') or (t.startswith('jmp ') and HEX.fullmatch(t[4:].strip()) is not None)


def code(q):
    a = int(q['addr'], 16)
    k = max(0, bisect.bisect_right(AT, a) - 1)
    # back to just after the code before it: a return or a jump away, then
    # any padding (int3, nop)
    s = k
    for j in range(k - 1, max(-1, k - 600), -1):
        if ends(TXT[j]):
            s = j + 1
            while s < k and (TXT[s] == 'int3' or TXT[s].startswith('nop')):
                s += 1
            break
    # (from the start, unless that's far above: then from 150 before it, so
    # the line asked about is always there, with what follows it)
    first = s if k - s <= 250 else k - 150
    n = int(q.get('count', 120))
    return {'start': '0x%06x' % AT[s], 'first': '0x%06x' % AT[first], 'at': '0x%06x' % AT[k],
            'lines': [line(j) for j in range(first, min(len(AT), k + n))]}


def field(q):
    off = int(q['off'], 16)
    pat = re.compile(r'[+-] 0x%x\]' % off)
    want = q.get('size', '')
    hits = []
    for k, t in enumerate(TXT):
        if pat.search(t):
            ops = t.split(' ', 1)[1] if ' ' in t else ''
            mnem = t.split(' ', 1)[0]
            # (the stack's own slots - [esp + 0x210] - aren't anyone's field)
            if re.search(r'\[esp [+-] 0x%x\]' % off, t):
                continue
            # (how much it touches: byte / word / dword, or its address taken)
            size = 'address' if mnem == 'lea' else next((z for z in ('qword', 'dword', 'word', 'byte') if z + ' ptr' in t), 'dword')
            if want and size != want:
                continue
            # (written: a mov, an and/or/add..., an inc - with it first; a call
            # through it is a call)
            first = ops.split(',')[0]
            if mnem == 'call':
                kind = 'call'
            elif mnem == 'lea':
                kind = 'address'
            else:
                kind = 'write' if pat.search(first) and mnem not in ('cmp', 'test', 'push') and not mnem.startswith('j') else 'read'
            h = line(k)
            h['kind'] = kind
            h['size'] = size
            hits.append(h)
            if len(hits) >= 500:
                break
    return {'off': '0x%x' % off, 'size': want, 'hits': hits}


def value(q):
    # every instruction using a number as itself - "push 0x282a", "mov eax,
    # 0x282a", "cmp ecx, 0x282a" - not as an offset in brackets: who uses a
    # message id, a key's number, a constant (value:10282)
    v = int(str(q['value']), 0) & 0xffffffff
    # (capstone writes 0-9 as decimal, the rest in hex)
    lit = str(v) if v < 10 else '0x%x' % v
    pat = re.compile(r'(?<![\w\[+-])(?<![+-] )%s(?![\w\]])' % re.escape(lit))
    hits = []
    for k, t in enumerate(TXT):
        if pat.search(t) and not re.search(r'\[[^\]]*%s[^\]]*\]' % re.escape(lit), t):
            h = line(k)
            h['kind'] = 'uses'
            hits.append(h)
            if len(hits) >= 500:
                break
    return {'value': '0x%x' % v, 'hits': hits}


PUSH = re.compile(r'^push (0x[0-9a-f]+|\d+|e[a-z]{2})$')
LEA = re.compile(r'^lea (e[a-z]{2}), \[(e[a-z]{2}) ([+-]) (0x[0-9a-f]+|\d+)\]$')
MOVI = re.compile(r'^mov (e[a-z]{2}), (0x[0-9a-f]+)$')
MOVR = re.compile(r'^mov (e[a-z]{2}), (e[a-z]{2})$')


def readers(q):
    # every key a config loader reads: a call to one of the readers (their
    # addresses: re/lab/loaders.json's "readers" - FUN_004b4803 an int,
    # FUN_004b486c a string...), with the pushes before it - nearest first
    # the file, the key ("cHungerCheck", its text), where it goes: a field
    # ([ebp + 0x1b4], lea'd into a register then pushed) or a global (pushed
    # as it is). What's the n-th push for each reader comes with it
    out = []
    for r in q['readers']:
        target = 'call 0x%x' % int(r['address'], 16)
        key_at, dest_at = int(r.get('key', 1)), int(r.get('dest', 2))
        for k, t in enumerate(TXT):
            if t != target:
                continue
            pushes = []
            j = k - 1
            while j >= 0 and k - j < 40 and len(pushes) < 6:
                m = PUSH.match(TXT[j])
                if m:
                    pushes.append((j, m.group(1)))
                if TXT[j].startswith('call ') or ends(TXT[j]):
                    break
                j -= 1
            if len(pushes) <= max(key_at, dest_at):
                continue
            kj, kv = pushes[key_at]
            key = string_at(int(kv, 16)) if kv.startswith('0x') else ''
            if not key:
                continue
            dj, dv = pushes[dest_at]
            dest = None
            if dv.startswith('0x'):
                dest = {'global': '0x%x' % int(dv, 16)}
            elif dv.startswith('e'):
                # (the register pushed: what was last put in it before)
                for i in range(dj - 1, max(-1, dj - 30), -1):
                    lm = LEA.match(TXT[i])
                    if lm and lm.group(1) == dv:
                        off = int(lm.group(4), 0) * (1 if lm.group(3) == '+' else -1)
                        if lm.group(2) not in ('esp',):
                            dest = {'field': '0x%x' % off, 'base': lm.group(2)}
                        break
                    mm = MOVI.match(TXT[i])
                    if mm and mm.group(1) == dv:
                        dest = {'global': '0x%x' % int(mm.group(2), 16)}
                        break
                    mr = MOVR.match(TXT[i])
                    if mr and mr.group(1) == dv:
                        # (a copy of the object itself: its offset 0)
                        dest = {'field': '0x0', 'base': mr.group(2)}
                        break
                    if TXT[i].split(' ')[0] in ('call',):
                        break
            out.append({'at': '0x%06x' % AT[k], 'reader': r['address'], 'kind': r.get('kind', 'int'), 'key': key, 'dest': dest,
                        'block': '0x%06x' % AT[block_start(k)]})
    return {'reads': out}


def block_start(k):
    # where the code an instruction's in starts: just after the code before
    # it ends (a return, a jump away) and any padding - for code Ghidra made
    # no function of, what it'd be
    for j in range(k - 1, max(-1, k - 20000), -1):
        if ends(TXT[j]):
            s = j + 1
            while s < k and (TXT[s] == 'int3' or TXT[s].startswith('nop')):
                s += 1
            return s
    return k


def strings(q):
    # the text at each address (data the code points at)
    return {'strings': {a: string_at(int(a, 16)) for a in q.get('addrs', [])}}


CODE_LO, CODE_HI = BASE + TEXT.VirtualAddress, BASE + TEXT.VirtualAddress + TEXT.Misc_VirtualSize
VTSTORE = re.compile(r'^mov dword ptr \[e[a-z]{2}\], (0x[0-9a-f]+)$')
STARTS = None


def code_ptr(a):
    try:
        v = pe.get_dword_at_rva(a - BASE)
    except Exception:
        return None
    return v if v is not None and CODE_LO <= v < CODE_HI else None


def table_starts():
    # where every class's table of functions begins: what a constructor
    # stores at an object's start ("mov dword ptr [esi], 0x630f88"), when
    # that's a list of pointers into the code
    global STARTS
    if STARTS is None:
        s = set()
        for t in TXT:
            m = VTSTORE.match(t)
            if m:
                a = int(m.group(1), 16)
                if not (CODE_LO <= a < CODE_HI) and code_ptr(a) is not None:
                    s.add(a)
        STARTS = sorted(s)
    return STARTS


def vtable(q):
    # a class's table of functions (vtable) in zoo.exe's data: each entry a
    # pointer into its code, from the table's address till one isn't - or
    # till the next table begins
    a = int(q['addr'], 16)
    starts = table_starts()
    k = bisect.bisect_right(starts, a)
    end = starts[k] if k < len(starts) else a + 4 * 2000
    slots = []
    for i in range(int(q.get('max', 1000))):
        at = a + 4 * i
        if at >= end:
            break
        v = code_ptr(at)
        if v is None:
            break
        slots.append('0x%06x' % v)
    return {'addr': '0x%06x' % a, 'slots': slots, 'next': '0x%06x' % end if k < len(starts) else ''}


def vtables(q):
    # every class's table of functions zoo.exe's constructors store, each with
    # how many entries it has
    out = []
    for a in table_starts():
        n = len(vtable({'addr': '0x%x' % a})['slots'])
        out.append({'addr': '0x%06x' % a, 'slots': n})
    return {'tables': out}


print(json.dumps({'ready': True, 'instructions': len(AT)}), flush=True)
for raw in sys.stdin:
    try:
        q = json.loads(raw)
    except ValueError:
        continue
    try:
        op = q.get('op')
        r = {'code': code, 'field': field, 'value': value, 'readers': readers, 'strings': strings, 'vtable': vtable,
             'vtables': vtables}.get(op, lambda _q: {'error': 'no such op'})(q)
    except Exception as e:  # (an answer either way: the page waits on it)
        r = {'error': str(e)}
    r['id'] = q.get('id')
    print(json.dumps(r), flush=True)
