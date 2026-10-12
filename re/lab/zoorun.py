#!/usr/bin/env python3
"""zoo.exe's own code, run on what's picked in the ZT1 Lab's Scene.

The Lab sends a function's address and the objects it works on (the
selected animal laid out as the knowledge base says ZTAnimal is, its
species as ZTAnimalType, ...) with their live values. This runs the
original x86 code on them (Unicorn), sandboxed, and says back everything
it did, instruction by instruction:

- what it read and wrote of the objects: a field the knowledge base knows
  (named, with its value), or one it doesn't (an error: "it wants
  ZTAnimal+0x24 and nobody's mapped it");
- the globals it used (as they are in the file, not a running game);
- pointers it followed to nothing the Lab gave it (errors too);
- the functions it called: run as well, or skipped (returning 0);
- what it returned: eax, and ST0 for a function returning a float; and
  any fields asked for ('readback': [[object, offset, type], ...]) as they
  were left.

Run by lab-app/main.js; a request a line on stdin, its answer a line on
stdout (JSON). Needs: pip install unicorn capstone.
"""
import json
import math
import struct
import sys

from capstone import CS_ARCH_X86, CS_MODE_32, Cs
from capstone.x86 import X86_OP_IMM, X86_OP_MEM, X86_OP_REG
from unicorn import (UC_ARCH_X86, UC_HOOK_CODE, UC_HOOK_MEM_READ, UC_HOOK_MEM_UNMAPPED, UC_HOOK_MEM_WRITE,
                     UC_MEM_FETCH_UNMAPPED, UC_MEM_WRITE, UC_MEM_WRITE_UNMAPPED, UC_MODE_32, Uc, UcError)
from unicorn import x86_const
from unicorn.x86_const import (UC_X86_REG_EAX, UC_X86_REG_EBP, UC_X86_REG_EBX, UC_X86_REG_ECX, UC_X86_REG_EDI,
                               UC_X86_REG_EDX, UC_X86_REG_EIP, UC_X86_REG_ESI, UC_X86_REG_ESP, UC_X86_REG_FPSW)

PAGE = 0x1000
STACK_BASE, STACK_SIZE = 0x00200000, 0x00100000
OBJ_BASE, OBJ_SPAN = 0x30000000, 0x00010000
STUB_BASE, STUB_SIZE = 0x7E000000, 0x00010000
SENTINEL = 0x7F000000  # (the return address it's called with: back there, it's done)

REGS = [('eax', UC_X86_REG_EAX), ('ecx', UC_X86_REG_ECX), ('edx', UC_X86_REG_EDX), ('ebx', UC_X86_REG_EBX),
        ('esi', UC_X86_REG_ESI), ('edi', UC_X86_REG_EDI), ('esp', UC_X86_REG_ESP), ('ebp', UC_X86_REG_EBP)]
FP_REGS = [getattr(x86_const, 'UC_X86_REG_FP%d' % i) for i in range(8)]


def align(v, a=PAGE):
    return (v + a - 1) & ~(a - 1)


class Image:
    """zoo.exe as Windows would lay it out in memory, and what it imports"""

    def __init__(self, path):
        data = open(path, 'rb').read()
        self.data = data
        pe = struct.unpack_from('<I', data, 0x3C)[0]
        nsec = struct.unpack_from('<H', data, pe + 6)[0]
        opt = pe + 24
        optsize = struct.unpack_from('<H', data, pe + 20)[0]
        self.base = struct.unpack_from('<I', data, opt + 28)[0]
        self.size = align(struct.unpack_from('<I', data, opt + 56)[0])
        self.headers = struct.unpack_from('<I', data, opt + 60)[0]
        imp_rva = struct.unpack_from('<I', data, opt + 96 + 8)[0]
        self.sections = []
        for i in range(nsec):
            s = pe + 24 + optsize + i * 40
            name = data[s:s + 8].rstrip(b'\0').decode('latin-1')
            vsize, va, rawsize, raw = struct.unpack_from('<IIII', data, s + 8)
            chars = struct.unpack_from('<I', data, s + 36)[0]
            self.sections.append({'name': name, 'va': self.base + va, 'size': max(vsize, rawsize),
                                  'raw': raw, 'rawsize': rawsize, 'code': bool(chars & 0x20)})
        self.imports = self.read_imports(imp_rva)

    def rva_off(self, rva):
        for s in self.sections:
            if s['va'] - self.base <= rva < s['va'] - self.base + s['rawsize']:
                return s['raw'] + rva - (s['va'] - self.base)
        return None

    def cstr(self, rva):
        off = self.rva_off(rva)
        if off is None:
            return '?'
        end = self.data.index(b'\0', off)
        return self.data[off:end].decode('latin-1')

    def read_imports(self, rva):
        """[(the IAT slot's address, "DLL!Name")]"""
        out = []
        off = self.rva_off(rva) if rva else None
        while off is not None:
            oft, _, _, name_rva, ft = struct.unpack_from('<IIIII', self.data, off)
            if not name_rva:
                break
            dll = self.cstr(name_rva).upper().replace('.DLL', '')
            thunk = oft or ft
            i = 0
            while True:
                t_off = self.rva_off(thunk + i * 4)
                v = struct.unpack_from('<I', self.data, t_off)[0] if t_off is not None else 0
                if not v:
                    break
                fn = ('#%d' % (v & 0xFFFF)) if v & 0x80000000 else self.cstr(v + 2)
                out.append((self.base + ft + i * 4, dll + '!' + fn))
                i += 1
            off += 20
        return out

    def section_of(self, addr):
        for s in self.sections:
            if s['va'] <= addr < s['va'] + s['size']:
                return s
        return None

    def load(self, uc):
        uc.mem_map(self.base, self.size)
        uc.mem_write(self.base, self.data[:self.headers])
        for s in self.sections:
            if s['rawsize']:
                uc.mem_write(s['va'], self.data[s['raw']:s['raw'] + s['rawsize']])


# How many bytes a field takes, by its type in the knowledge base
def type_size(t):
    t = (t or 'int').strip().lower()
    if t.endswith('*') or t in ('pointer', 'ptr', 'int', 'uint', 'dword', 'float', 'unsigned int', 'long'):
        return 4
    if t in ('byte', 'char', 'bool', 'uchar', 'uint8'):
        return 1
    if t in ('short', 'word', 'ushort', 'int16'):
        return 2
    if t in ('double', 'int64', 'qword'):
        return 8
    return 4


def pack(t, v, size):
    t = (t or 'int').strip().lower()
    if t in ('float',):
        return struct.pack('<f', float(v))
    if t in ('double',):
        return struct.pack('<d', float(v))
    v = int(round(float(v))) if v is not None else 0
    return (v & ((1 << (8 * size)) - 1)).to_bytes(size, 'little')


def unpack(t, b):
    t = (t or 'int').strip().lower()
    if t == 'float' and len(b) == 4:
        return struct.unpack('<f', b)[0]
    if t == 'double' and len(b) == 8:
        return struct.unpack('<d', b)[0]
    v = int.from_bytes(b, 'little')
    if len(b) in (1, 2, 4) and v >= 1 << (8 * len(b) - 1) and not t.endswith('*') and t != 'pointer':
        v -= 1 << (8 * len(b))
    return v


class Run:
    def __init__(self, image, req):
        self.img = image
        self.req = req
        self.md = Cs(CS_ARCH_X86, CS_MODE_32)
        self.md.detail = True
        self.names = {int(k, 16): v for k, v in (req.get('names') or {}).items()}
        self.call_mode = req.get('calls', 'skip')  # skip | run
        self.max_depth = int(req.get('maxDepth', 3))
        self.limit = int(req.get('limit', 20000))
        self.keep = int(req.get('keep', 4000))
        self.steps, self.errors, self.calls = [], [], {}
        self.globals = {}  # addr -> {reads, writes, value, first}
        self.touched = {}  # (object index, off) -> record
        self.auto_pages = []
        self.count = 0
        self.depth = 0
        self.cur = None
        self.done = False
        self.insn_cache = {}
        self.ret_cache = {}
        self.pushes = {}  # depth -> arguments pushed since its last call

    # ---------------------------------------------------------------- set up
    def setup(self):
        uc = Uc(UC_ARCH_X86, UC_MODE_32)
        self.uc = uc
        self.img.load(uc)
        uc.mem_map(STACK_BASE, STACK_SIZE)
        uc.mem_map(STUB_BASE, STUB_SIZE)
        uc.mem_write(STUB_BASE, b'\xC3' * STUB_SIZE)
        uc.mem_map(SENTINEL, PAGE)
        # Imports: each slot of the import table points at a stub of its own
        self.stubs = {}
        for i, (slot, name) in enumerate(self.img.imports):
            stub = STUB_BASE + i * 16
            self.stubs[stub] = name
            uc.mem_write(slot, struct.pack('<I', stub))
        # The objects: each in its own span, its known fields filled in
        self.objects = self.req.get('objects') or []
        for i, o in enumerate(self.objects):
            o['base'] = OBJ_BASE + i * OBJ_SPAN
            o['size'] = min(int(o.get('size') or 0x400), OBJ_SPAN)
            uc.mem_map(o['base'], OBJ_SPAN)
            o['fields'] = sorted(o.get('fields') or [], key=lambda f: f['off'])
            for f in o['fields']:
                f['bytes'] = type_size(f.get('type'))
                if f.get('to') is not None and 0 <= f['to'] < len(self.objects):
                    val = OBJ_BASE + f['to'] * OBJ_SPAN
                    uc.mem_write(o['base'] + f['off'], struct.pack('<I', val))
                elif f.get('value') is not None:
                    uc.mem_write(o['base'] + f['off'], pack(f.get('type'), f['value'], f['bytes']))
        # The stack: the arguments, then where it returns to
        esp = STACK_BASE + STACK_SIZE - 0x1000
        args = [self.arg(a) for a in (self.req.get('args') or [])]
        for a in reversed(args):
            esp -= 4
            uc.mem_write(esp, struct.pack('<I', a & 0xFFFFFFFF))
        esp -= 4
        uc.mem_write(esp, struct.pack('<I', SENTINEL))
        uc.reg_write(UC_X86_REG_ESP, esp)
        uc.reg_write(UC_X86_REG_EBP, esp + 0x100)
        # thiscall / fastcall: ecx (and edx) are the first arguments
        uc.reg_write(UC_X86_REG_ECX, self.arg(self.req.get('ecx', 'obj:0')) & 0xFFFFFFFF)
        uc.reg_write(UC_X86_REG_EDX, self.arg(self.req.get('edx', 0)) & 0xFFFFFFFF)
        uc.hook_add(UC_HOOK_CODE, self.on_code)
        uc.hook_add(UC_HOOK_MEM_READ | UC_HOOK_MEM_WRITE, self.on_mem)
        uc.hook_add(UC_HOOK_MEM_UNMAPPED, self.on_unmapped)

    def arg(self, a):
        if isinstance(a, str) and a.startswith('obj:'):
            i = int(a[4:])
            return OBJ_BASE + i * OBJ_SPAN if 0 <= i < len(self.objects) else 0
        try:
            return int(a, 0) if isinstance(a, str) else int(a)
        except (TypeError, ValueError):
            return 0

    # ------------------------------------------------------------- reading
    def insn(self, addr):
        i = self.insn_cache.get(addr)
        if i is None:
            code = bytes(self.uc.mem_read(addr, 16))
            i = next(self.md.disasm(code, addr, 1), None)
            self.insn_cache[addr] = i
        return i

    def ret_imm(self, target):
        """What a function pops off the stack as it returns (its ret n)"""
        if target in self.ret_cache:
            return self.ret_cache[target]
        n = 0
        try:
            code = bytes(self.uc.mem_read(target, 0x3000))
            for i in self.md.disasm(code, target):
                if i.mnemonic == 'ret':
                    n = i.operands[0].imm if i.operands else 0
                    break
        except UcError:
            pass
        self.ret_cache[target] = n
        return n

    def name_of(self, addr):
        if addr in self.stubs:
            return self.stubs[addr]
        return self.names.get(addr) or 'FUN_%08x' % addr

    def where(self, addr):
        """What an address is: an object's field, the stack, a global..."""
        for i, o in enumerate(self.objects):
            if o['base'] <= addr < o['base'] + OBJ_SPAN:
                return ('obj', i, addr - o['base'])
        if STACK_BASE <= addr < STACK_BASE + STACK_SIZE:
            return ('stack', None, None)
        if STUB_BASE <= addr < STUB_BASE + STUB_SIZE or SENTINEL <= addr < SENTINEL + PAGE:
            return ('lab', None, None)
        s = self.img.section_of(addr)
        if s:
            return ('code' if s['code'] else 'global', s['name'], None)
        return ('nothing', None, None)

    # --------------------------------------------------------------- hooks
    def on_code(self, uc, addr, size, _):
        if addr == SENTINEL:
            self.done = True
            uc.emu_stop()
            return
        self.count += 1
        # (code only runs from zoo.exe's code: anywhere else, it's lost)
        kind = self.where(addr)[0]
        if kind != 'code' and addr not in self.stubs:
            self.error('pointer', "It jumped to 0x%08x, which isn't code: a function pointer nobody filled in "
                       "(a virtual method through an object's vtable at +0?)" % addr, addr)
            uc.emu_stop()
            return
        if self.count > self.limit:
            self.error('limit', 'Stopped after %d instructions (a loop that never ends, or a long one: raise the limit)' % self.limit, addr)
            uc.emu_stop()
            return
        # A Windows function (an import): not run, 0 back
        if addr in self.stubs:
            name = self.stubs[addr]
            esp = uc.reg_read(UC_X86_REG_ESP)
            ret = struct.unpack('<I', uc.mem_read(esp, 4))[0]
            self.note_call(addr, name, skipped=True, why='a Windows function: not run, 0 back')
            uc.reg_write(UC_X86_REG_ESP, esp + 4 + WINAPI_ARGS.get(name.split('!')[-1], 0) * 4)
            uc.reg_write(UC_X86_REG_EAX, 0)
            uc.reg_write(UC_X86_REG_EIP, ret)
            return
        i = self.insn(addr)
        text = ('%s %s' % (i.mnemonic, i.op_str)).strip() if i else '(?)'
        step = {'a': addr, 't': text, 'd': self.depth, 'r': [uc.reg_read(r) for _, r in REGS], 'acc': []}
        self.cur = step
        if len(self.steps) < self.keep:
            self.steps.append(step)
        if not i:
            return
        # (the arguments for the next call: the pushes just before it - a
        # mov or lea between them sets one up; anything else, and what was
        # pushed was something else: saved registers)
        if i.mnemonic == 'push':
            self.pushes[self.depth] = self.pushes.get(self.depth, 0) + 1
        elif i.mnemonic not in ('mov', 'lea', 'call'):
            self.pushes[self.depth] = 0
        if i.mnemonic == 'ret':
            if self.depth > 0:
                self.depth -= 1
            return
        if i.mnemonic == 'call':
            target = self.call_target(i)
            pushed = self.pushes.get(self.depth, 0)
            self.pushes[self.depth] = 0
            if not target or self.where(target)[0] != 'code' and target not in self.stubs:
                op = i.op_str
                self.error('pointer', 'It calls through %s, which is empty: a function pointer nobody filled in%s' % (
                    op, " (a virtual method: its object's vtable at +0 isn't filled in)" if '[' in op else ''), addr)
            if target in self.stubs:
                return  # (the stub's hook above handles it)
            skip = self.call_mode == 'skip' or self.depth >= self.max_depth or target is None or \
                (self.where(target)[0] != 'code')
            if skip:
                # what the callee would pop: its ret n; not known, the
                # arguments pushed for it, unless the caller cleans up after
                if target and self.where(target)[0] == 'code':
                    purge = self.ret_imm(target)
                else:
                    after = self.insn(addr + i.size)
                    purge = 0 if after and after.mnemonic == 'add' and after.op_str.startswith('esp') else pushed * 4
                self.note_call(target, self.name_of(target) if target else '(computed)', skipped=True,
                               why='skipped: 0 back' if self.call_mode == 'skip' else 'too deep: skipped, 0 back')
                step['call'] = {'to': target, 'name': self.name_of(target) if target else '?', 'skipped': True}
                uc.reg_write(UC_X86_REG_ESP, uc.reg_read(UC_X86_REG_ESP) + purge)
                uc.reg_write(UC_X86_REG_EAX, 0)
                uc.reg_write(UC_X86_REG_EIP, addr + i.size)
            else:
                self.note_call(target, self.name_of(target), skipped=False, why='run')
                step['call'] = {'to': target, 'name': self.name_of(target), 'skipped': False}
                self.depth += 1

    def call_target(self, i):
        if not i.operands:
            return None
        op = i.operands[0]
        try:
            if op.type == X86_OP_IMM:
                return op.imm & 0xFFFFFFFF
            if op.type == X86_OP_REG:
                return self.uc.reg_read(self.reg_id(i.reg_name(op.reg)))
            if op.type == X86_OP_MEM:
                addr = op.mem.disp & 0xFFFFFFFF
                if op.mem.base:
                    addr = (addr + self.uc.reg_read(self.reg_id(i.reg_name(op.mem.base)))) & 0xFFFFFFFF
                if op.mem.index:
                    addr = (addr + self.uc.reg_read(self.reg_id(i.reg_name(op.mem.index))) * op.mem.scale) & 0xFFFFFFFF
                return struct.unpack('<I', self.uc.mem_read(addr, 4))[0]
        except (UcError, KeyError):
            return None
        return None

    @staticmethod
    def reg_id(name):
        return dict(REGS)[name]

    def note_call(self, target, name, skipped, why):
        k = target or 0
        c = self.calls.setdefault(k, {'to': target, 'name': name, 'count': 0, 'skipped': skipped, 'why': why})
        c['count'] += 1

    def on_mem(self, uc, access, addr, size, value, _):
        write = access == UC_MEM_WRITE
        kind, which, off = self.where(addr)
        if kind in ('stack', 'lab', 'code'):
            return
        at = uc.reg_read(UC_X86_REG_EIP)
        if write:
            new = value & ((1 << (8 * size)) - 1)
            old = bytes(uc.mem_read(addr, size))
        else:
            old = bytes(uc.mem_read(addr, size))
            new = None
        if kind == 'obj':
            o = self.objects[which]
            f = next((f for f in o['fields'] if f['off'] <= off < f['off'] + f['bytes']), None)
            key = (which, f['off'] if f else off)
            rec = self.touched.get(key)
            if not rec:
                rec = {'obj': which, 'struct': o.get('struct', '?'), 'off': f['off'] if f else off,
                       'name': f.get('name') if f else None, 'type': f.get('type') if f else ('int' if size == 4 else 'byte' if size == 1 else 'short'),
                       'known': bool(f), 'live': bool(f and f.get('live')), 'size': size,
                       'reads': 0, 'writes': 0, 'first': None, 'last': None, 'firstAt': at}
                self.touched[key] = rec
                if not f:
                    self.error('unmapped', '%s+0x%x: it %s this and nobody\'s mapped it' % (
                        o.get('struct', '?'), off, 'writes' if write else 'reads'), at, struct=o.get('struct'), off=off, obj=which)
            t = rec['type']
            if write:
                rec['writes'] += 1
                val = unpack(t, new.to_bytes(size, 'little'))
                if rec['first'] is None:
                    rec['first'] = unpack(t, old)
                rec['last'] = val
            else:
                rec['reads'] += 1
                val = unpack(t, old)
                if rec['first'] is None:
                    rec['first'] = val
            if self.cur is not None:
                self.cur['acc'].append({'w': write, 'k': 'obj', 'o': which, 'off': off, 'name': rec['name'],
                                        'known': rec['known'], 'v': val})
        elif kind == 'global':
            g = self.globals.setdefault(addr, {'addr': addr, 'section': which, 'reads': 0, 'writes': 0, 'value': None,
                                                'name': self.names.get(addr)})
            val = unpack('int', new.to_bytes(size, 'little') if write else old)
            g['writes' if write else 'reads'] += 1
            g['value'] = val
            if self.cur is not None:
                self.cur['acc'].append({'w': write, 'k': 'global', 'a': addr, 'v': val, 'name': g['name']})
        else:
            # memory nobody gave it (a page made when it went there)
            if self.cur is not None:
                self.cur['acc'].append({'w': write, 'k': 'nothing', 'a': addr, 'v': unpack('int', old)})

    def on_unmapped(self, uc, access, addr, size, value, _):
        if access == UC_MEM_FETCH_UNMAPPED:
            self.error('pointer', 'It jumped to 0x%08x, which is nothing: a function pointer nobody filled in' % addr, addr)
            return False
        page = addr & ~(PAGE - 1)
        what = 'writes to' if access == UC_MEM_WRITE_UNMAPPED else 'reads'
        hint = ' (a null pointer: something the Lab didn\'t fill in)' if addr < 0x10000 else \
            ' (a pointer the Lab didn\'t fill in)'
        self.error('pointer', 'It %s 0x%08x, which is nothing it was given%s' % (what, addr, hint), uc.reg_read(UC_X86_REG_EIP), addr=addr)
        try:
            uc.mem_map(page, PAGE)
            self.auto_pages.append(page)
            return True
        except UcError:
            return False

    def error(self, kind, text, at, **more):
        if len(self.errors) < 200:
            self.errors.append({'kind': kind, 'text': text, 'at': at, **more})

    def st0(self):
        """The top of the FPU's stack - where a function returning a float
        leaves it (eax means nothing then).
        Unicorn's FP0-7 are the physical registers: ST0 is the one at TOP."""
        sw = self.uc.reg_read(UC_X86_REG_FPSW)
        top = (sw >> 11) & 7
        # (Unicorn's tag word goes by what's in a register - 01 for zero - so
        # an empty stack can't be told from a 0.0: ST0 is read whatever it is,
        # and means something only for a function that returns a float)
        mant, exp = self.uc.reg_read(FP_REGS[top])
        sign = -1.0 if exp & 0x8000 else 1.0
        e = exp & 0x7FFF
        if e == 0 and mant == 0:
            return 0.0
        if e == 0x7FFF:
            return None if mant & ((1 << 63) - 1) else sign * float('inf')
        return sign * math.ldexp(mant, e - 16383 - 63)

    # ----------------------------------------------------------------- run
    def go(self):
        start = int(self.req['addr'], 16) if isinstance(self.req['addr'], str) else int(self.req['addr'])
        try:
            self.setup()
            self.uc.emu_start(start, SENTINEL, count=self.limit + 10)
        except UcError as e:
            self.error('crash', 'It stopped: %s' % e, self.uc.reg_read(UC_X86_REG_EIP) if hasattr(self, 'uc') else start)
        eax = self.uc.reg_read(UC_X86_REG_EAX) if hasattr(self, 'uc') else 0
        st0 = self.st0() if hasattr(self, 'uc') else None
        # (it stops just before the address it returns to: back there, done)
        if hasattr(self, 'uc') and self.uc.reg_read(UC_X86_REG_EIP) == SENTINEL:
            self.done = True
        # bytes of a global read one after another: text, shown as such
        texts, run = [], []
        for g in sorted(self.globals.values(), key=lambda g: g['addr']):
            if run and g['addr'] == run[-1]['addr'] + 1 and 0 <= (g['value'] or 0) < 256:
                run.append(g)
                continue
            if len(run) >= 4:
                texts.append(run)
            run = [g] if 0 <= (g['value'] or 0) < 256 else []
        if len(run) >= 4:
            texts.append(run)
        strings = []
        for r in texts:
            b = bytes((g['value'] or 0) & 0xFF for g in r)
            if sum(32 <= c < 127 for c in b) >= len(b) * 0.8:
                strings.append({'addr': r[0]['addr'], 'text': b.split(b'\0')[0].decode('latin-1')})
                for g in r:
                    self.globals.pop(g['addr'], None)
        # each object's fields at the end (what it changed)
        for o in self.objects:
            for f in o.get('fields') or []:
                if f.get('to') is None:
                    try:
                        f['end'] = unpack(f.get('type'), bytes(self.uc.mem_read(o['base'] + f['off'], f['bytes'])))
                    except UcError:
                        pass
        # what was asked to be read back at the end ([object, offset, type]
        # each): a field's value once it's returned
        readback = []
        for ob, off, t in self.req.get('readback') or []:
            try:
                base = self.objects[ob]['base']
                readback.append(unpack(t, bytes(self.uc.mem_read(base + off, type_size(t)))))
            except (UcError, IndexError, KeyError, TypeError, AttributeError):
                readback.append(None)
        touched = sorted(self.touched.values(), key=lambda r: (r['obj'], r['off']))
        return {
            'ok': self.done and not any(e['kind'] in ('crash', 'limit') for e in self.errors),
            'returned': self.done,
            'eax': eax,
            'st0': st0,
            'readback': readback,
            'count': self.count,
            'steps': self.steps,
            'truncated': self.count > len(self.steps),
            'touched': touched,
            'globals': sorted(self.globals.values(), key=lambda g: g['addr']),
            'strings': strings,
            'calls': sorted(self.calls.values(), key=lambda c: -c['count']),
            'errors': self.errors,
            'regs': [n for n, _ in REGS],
            'objects': [{'name': o.get('name'), 'struct': o.get('struct'), 'base': o['base']} for o in self.objects],
        }


# Windows functions zoo.exe uses and how many arguments they pop (stdcall)
WINAPI_ARGS = {
    'GetTickCount': 0, 'timeGetTime': 0, 'GetLastError': 0, 'Sleep': 1, 'GetCurrentThreadId': 0,
    'EnterCriticalSection': 1, 'LeaveCriticalSection': 1, 'InitializeCriticalSection': 1,
    'DeleteCriticalSection': 1, 'InterlockedIncrement': 1, 'InterlockedDecrement': 1,
    'QueryPerformanceCounter': 1, 'QueryPerformanceFrequency': 1, 'OutputDebugStringA': 1,
    'lstrlenA': 1, 'lstrcpyA': 2, 'lstrcatA': 2, 'lstrcmpiA': 2, 'lstrcmpA': 2, 'wsprintfA': 0,
    'MessageBoxA': 4, 'GetAsyncKeyState': 1, 'GetKeyState': 1, 'PostMessageA': 4, 'SendMessageA': 4,
    'LoadStringA': 4, 'GetPrivateProfileIntA': 4, 'GetPrivateProfileStringA': 6,
    'HeapAlloc': 3, 'HeapFree': 3, 'GlobalAlloc': 2, 'GlobalFree': 1, 'LocalAlloc': 2, 'LocalFree': 1,
}


def main():
    if len(sys.argv) < 2:
        print('usage: zoorun.py <zoo.exe>', file=sys.stderr)
        return 2
    try:
        image = Image(sys.argv[1])
    except OSError as e:
        print(json.dumps({'ready': False, 'error': 'zoo.exe: %s' % e}), flush=True)
        return 1
    print(json.dumps({'ready': True, 'base': image.base, 'imports': len(image.imports)}), flush=True)
    for line in sys.stdin:
        line = line.strip()
        if not line:
            continue
        try:
            req = json.loads(line)
            out = Run(image, req).go()
            out['id'] = req.get('id')
        except Exception as e:  # (a bad request: said, and on to the next)
            out = {'id': None, 'ok': False, 'errors': [{'kind': 'crash', 'text': 'zoorun: %s' % e, 'at': 0}]}
            try:
                out['id'] = json.loads(line).get('id')
            except ValueError:
                pass
        sys.stdout.write(json.dumps(out) + '\n')
        sys.stdout.flush()
    return 0


if __name__ == '__main__':
    sys.exit(main())
