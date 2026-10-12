"""ZT1 Check: shows whether what's mapped in re/knowledge.json is right.

  Loader   (no game needed) each struct field named for a config key
           (cHungerThreshold...) against zoo.exe's own loader code: does the
           code reading that key store it at the mapped offset?
  Species  a species' .ai values laid out at the mapped offsets - what the
           game's memory should hold
  Live     (the original game running) finds each species in its memory by
           those values, every field ticked or crossed; the animals pointing
           at it; any object's fields watched as they change, unmapped ones
           as raw numbers, and an offset mapped straight into the knowledge
           base under your name

python re/check.py                 the window
python re/check.py window lion f   the window, on a species (and subtype)
python re/check.py loader          the loader check, printed
python re/check.py species zebra   a species' values at the mapped offsets
python re/check.py live zebra      find it in the running game, printed
"""
import ctypes, ctypes.wintypes as wt, glob, os, re, struct, sys, time, zipfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(HERE)
sys.path.insert(0, os.path.join(HERE, "ghidra_scripts"))
import zt1db  # noqa: E402

GAME = os.environ.get("ZT1_DIR", r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon")
EXE = os.path.join(GAME, "zoo.exe")
SPECIES = "ZTAnimalType"
KEY = re.compile(r"^c[A-Z][A-Za-z0-9]+$")


def me():
    p = os.path.join(HERE, "private", "me.txt")
    return open(p).read().strip() if os.path.exists(p) else "unknown"


# ---------------------------------------------------------------- game data
class GameFiles:
    """The game's archives in its own order (zoo.ini [resource] path: an
    earlier folder wins; in a folder, a later archive by name wins)."""

    def __init__(self):
        self.where = {}
        path = "./dlupdate;./updates;./xpack2/loc;./xpack2;./dupdate;./zupdate1;./xpack1/loc;./xpack1;./zupdate;./loc;."
        ini = os.path.join(GAME, "zoo.ini")
        if os.path.exists(ini):
            m = re.search(r"^\s*path\s*=\s*(.+)$", open(ini, encoding="latin1").read(), re.M | re.I)
            if m:
                path = m.group(1).strip()
        for folder in path.split(";"):
            d = os.path.normpath(os.path.join(GAME, folder.strip()))
            if not os.path.isdir(d):
                continue
            here = {}
            for z in sorted(glob.glob(os.path.join(d, "*.ztd")) + glob.glob(os.path.join(d, "*.ZTD")),
                            key=lambda p: os.path.basename(p).lower()):
                try:
                    for n in zipfile.ZipFile(z).namelist():
                        here[n.lower().replace("\\", "/")] = (z, n)
                except zipfile.BadZipFile:
                    pass
            for k, v in here.items():
                self.where.setdefault(k, v)

    def read(self, name):
        hit = self.where.get(name.lower())
        if not hit:
            return None
        return zipfile.ZipFile(hit[0]).read(hit[1]).decode("latin1")

    def species(self):
        return sorted(k[len("animals/"):-3] for k in self.where if re.match(r"^animals/[^/]+\.ai$", k))


def ini_section(text, section):
    """key -> value (first word) of one [section]."""
    out, on = {}, False
    for line in text.splitlines():
        s = line.split(";")[0].strip()
        if s.startswith("["):
            on = s[1:-1].strip().lower().replace("\\", "/") == section.lower()
            continue
        if on and "=" in s:
            k, v = s.split("=", 1)
            v = v.strip().split()
            if v:
                out[k.strip()] = v[0]
    return out


def species_values(files, name, sub="m"):
    """A species' [Characteristics/Integers] for a subtype: the adult (m)
    section; f and y over it."""
    text = files.read("animals/%s.ai" % name)
    if text is None:
        return {}
    # (the main values are under the species' DefaultSubtype - m for a
    # zebra, f for a lion - and another subtype's go over them)
    default = ini_section(text, "Global").get("DefaultSubtype", "m") or "m"
    vals = ini_section(text, "%s/Characteristics/Integers" % default)
    if sub != default:
        vals.update(ini_section(text, "%s/Characteristics/Integers" % sub))
    out = {}
    for k, v in vals.items():
        try:
            out[k] = int(v, 0)
        except ValueError:
            pass
    return out


def expected(db, files, name, sub="m"):
    """[(offset, field, value from the .ai)] for the species' mapped fields."""
    vals = species_values(files, name, sub)
    out = []
    for key, f in sorted(db["structs"].get(SPECIES, {}).get("fields", {}).items(),
                         key=lambda kv: zt1db.parse_int(kv[0])):
        if f["name"] in vals:
            out.append((zt1db.parse_int(key), f["name"], vals[f["name"]]))
    return out


# ------------------------------------------------------------ loader check
def loader_check(db):
    """Every config-key field, against the code reading that key:
    [(struct, offset, name, status, offsets the code uses)]."""
    import pefile
    import capstone
    pe = pefile.PE(EXE, fast_load=True)
    base = pe.OPTIONAL_HEADER.ImageBase
    img = pe.get_memory_mapped_image()
    text = [s for s in pe.sections if s.Name.startswith(b".text")][0]
    t0, t1 = text.VirtualAddress, text.VirtualAddress + text.Misc_VirtualSize
    md = capstone.Cs(capstone.CS_ARCH_X86, capstone.CS_MODE_32)
    md.detail = True
    out = []
    for sname, s in sorted(db["structs"].items()):
        for key, f in sorted(s["fields"].items(), key=lambda kv: zt1db.parse_int(kv[0])):
            name = f["name"]
            if not KEY.match(name):
                continue
            off = zt1db.parse_int(key)
            used = set()
            for m in re.finditer(re.escape(name.encode()) + b"\x00", img):
                if m.start() > 0 and img[m.start() - 1] not in (0,):
                    continue  # (the end of a longer name)
                va = base + m.start()
                push = b"\x68" + struct.pack("<I", va)
                at = img.find(push, t0, t1)
                while at >= 0:
                    used |= stored_offsets(md, img, base, at)
                    at = img.find(push, at + 1, t1)
            if not used:
                status = "?"  # (no code reads it like a loader)
            elif off in used:
                status = "ok"
            else:
                status = "WRONG"
            out.append((sname, off, name, status, sorted(used)))
    return out


def stored_offsets(md, img, base, at):
    """Where a loader puts a key's value: it takes the field's address
    (lea ecx, [ebp + 0x20c]) and pushes it just before the key's name
    (push "cHungerThreshold"), both going to the reader. The last such lea
    before the push."""
    import capstone
    # (x86 can't be read backwards: start a little before and keep the
    # reading that lands exactly on the push)
    for start in range(at - 40, at):
        chain = []
        for ins in md.disasm(img[start:at], base + start):
            chain.append(ins)
        if not chain or chain[-1].address + chain[-1].size != base + at:
            continue
        for ins in reversed(chain[-8:]):
            if ins.mnemonic == "call":
                break  # (that's the previous key's)
            if ins.mnemonic == "lea" and len(ins.operands) == 2:
                op = ins.operands[1]
                if op.type == capstone.x86.X86_OP_MEM and op.mem.base not in (0, capstone.x86.X86_REG_ESP) \
                        and op.mem.index == 0:
                    return {op.mem.disp}
        return set()
    return set()


# --------------------------------------------------------------- live game
PROCESS_VM_READ, PROCESS_QUERY = 0x0010, 0x0400
MEM_COMMIT, PAGE_GUARD, PAGE_NOACCESS = 0x1000, 0x100, 0x01


class MBI(ctypes.Structure):
    _fields_ = [("BaseAddress", ctypes.c_void_p), ("AllocationBase", ctypes.c_void_p),
                ("AllocationProtect", wt.DWORD), ("PartitionId", wt.WORD), ("RegionSize", ctypes.c_size_t),
                ("State", wt.DWORD), ("Protect", wt.DWORD), ("Type", wt.DWORD)]


class Process:
    """The running zoo.exe, read-only."""

    def __init__(self, pid=None):
        self.k = ctypes.WinDLL("kernel32", use_last_error=True)
        self.k.OpenProcess.restype = wt.HANDLE
        self.k.VirtualQueryEx.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.POINTER(MBI), ctypes.c_size_t]
        self.k.ReadProcessMemory.argtypes = [wt.HANDLE, ctypes.c_void_p, ctypes.c_void_p, ctypes.c_size_t,
                                             ctypes.POINTER(ctypes.c_size_t)]
        self.pid = pid or int(os.environ.get("ZT1_PID", 0)) or find_pid("zoo.exe")
        if not self.pid:
            raise RuntimeError("zoo.exe isn't running")
        self.h = self.k.OpenProcess(PROCESS_VM_READ | PROCESS_QUERY, False, self.pid)
        if not self.h:
            raise RuntimeError("Can't open zoo.exe (error %d)" % ctypes.get_last_error())

    def regions(self):
        """(base, bytes) of each readable, committed private or image region."""
        addr, mbi = 0, MBI()
        while addr < 0x7FFF0000:
            if not self.k.VirtualQueryEx(self.h, addr, ctypes.byref(mbi), ctypes.sizeof(mbi)):
                break
            base, size = mbi.BaseAddress or 0, mbi.RegionSize
            if mbi.State == MEM_COMMIT and not (mbi.Protect & (PAGE_GUARD | PAGE_NOACCESS)) and mbi.Protect:
                data = self.read(base, size)
                if data:
                    yield base, data
            addr = base + size

    def read(self, addr, size):
        buf = ctypes.create_string_buffer(size)
        got = ctypes.c_size_t(0)
        if not self.k.ReadProcessMemory(self.h, addr, buf, size, ctypes.byref(got)):
            return b""
        return buf.raw[:got.value]


def find_pid(exe):
    import subprocess
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh", "/fi", "IMAGENAME eq " + exe],
                         capture_output=True, text=True).stdout
    m = re.search(r'"%s","(\d+)"' % re.escape(exe), out, re.I)
    return int(m.group(1)) if m else None


def find_by_values(proc, want, need=0.8):
    """Objects whose fields hold the wanted values: [(address, [(offset,
    name, value, found)], matches)], best first. want: [(offset, name, value)]."""
    if len(want) < 3:
        return []
    # (search by the rarest-looking value, then check the rest)
    pivot = max(want, key=lambda w: (abs(w[2]) not in (0, 1), abs(w[2])))
    pat = struct.pack("<i", pivot[2])
    hits = []
    for base, data in proc.regions():
        i = data.find(pat)
        while i >= 0:
            start = i - pivot[0]
            if start >= 0 and start % 4 == 0:
                rows, good = [], 0
                for off, name, val in want:
                    j = start + off
                    got = struct.unpack_from("<i", data, j)[0] if j + 4 <= len(data) else None
                    good += got == val
                    rows.append((off, name, val, got))
                if good >= need * len(want):
                    hits.append((base + start, rows, good))
            i = data.find(pat, i + 1)
    hits.sort(key=lambda h: -h[2])
    return hits


def find_pointers(proc, target, at_offset=None):
    """Where memory holds a pointer to target; with at_offset, the objects
    (address - offset) that would hold it there."""
    pat = struct.pack("<I", target)
    out = []
    for base, data in proc.regions():
        i = data.find(pat)
        while i >= 0:
            if i % 4 == 0:
                a = base + i
                out.append(a - at_offset if at_offset is not None else a)
            i = data.find(pat, i + 1)
    return out


def decode(raw, type_name):
    if len(raw) < 4:
        return ""
    i, u, f = struct.unpack("<i", raw[:4])[0], struct.unpack("<I", raw[:4])[0], struct.unpack("<f", raw[:4])[0]
    t = (type_name or "").strip()
    if t == "float":
        return "%g" % f
    if t in ("byte", "bool", "char"):
        return str(raw[0])
    if t in ("short", "ushort", "word"):
        return str(struct.unpack("<h" if t == "short" else "<H", raw[:2])[0])
    if t.endswith("*") or t in ("pointer", "ptr"):
        return "0x%08x" % u
    return str(i)


# ---------------------------------------------------------------- printing
def cli(args):
    db = zt1db.load(ROOT)
    if args[0] == "loader":
        rows = loader_check(db)
        for sname, off, name, status, used in rows:
            if status != "ok":
                print("%-6s %s+0x%03x %-28s code uses: %s" % (status, sname, off, name,
                                                            ", ".join("0x%x" % u for u in used) or "-"))
        n = dict((s, sum(1 for r in rows if r[3] == s)) for s in ("ok", "WRONG", "?"))
        print("%d fields checked: %d ok, %d wrong, %d not read by a loader" % (len(rows), n["ok"], n["WRONG"], n["?"]))
    elif args[0] == "species":
        files = GameFiles()
        sub = args[2] if len(args) > 2 else "m"
        for off, name, val in expected(db, files, args[1], sub):
            print("+0x%03x %-28s %d" % (off, name, val))
    elif args[0] == "live":
        files = GameFiles()
        want = expected(db, files, args[1], args[2] if len(args) > 2 else "m")
        proc = Process()
        hits = find_by_values(proc, want)
        if not hits:
            print("No %s in the game's memory with those values (is one in the zoo?)" % args[1])
        for addr, rows, good in hits[:5]:
            print("%s at 0x%08x: %d of %d fields match" % (args[1], addr, good, len(rows)))
            for off, name, val, got in rows:
                if got != val:
                    print("   x +0x%03x %-26s .ai %s, memory %s" % (off, name, val, got))
            animals = find_pointers(proc, addr, at_offset=0x128)
            print("   %d objects point at it from +0x128 (ZTAnimal.type): %s" % (
                len(animals), " ".join("0x%08x" % a for a in animals[:8])))
    else:
        print(__doc__)


# ------------------------------------------------------------------ window
def window(start_species=None, start_sub=None):
    import tkinter as tk
    from tkinter import ttk, messagebox

    db = {"v": zt1db.load(ROOT)}
    files = GameFiles()
    root = tk.Tk()
    root.title("ZT1 Check - is what's mapped right?")
    root.geometry("1100x720")
    style = ttk.Style()
    style.configure("Treeview", rowheight=20)
    nb = ttk.Notebook(root)
    nb.pack(fill="both", expand=True)

    def table(parent, cols, widths):
        frame = ttk.Frame(parent)
        tv = ttk.Treeview(frame, columns=cols, show="headings")
        for c, w in zip(cols, widths):
            tv.heading(c, text=c)
            tv.column(c, width=w, anchor="w")
        sb = ttk.Scrollbar(frame, orient="vertical", command=tv.yview)
        tv.configure(yscrollcommand=sb.set)
        tv.pack(side="left", fill="both", expand=True)
        sb.pack(side="right", fill="y")
        tv.tag_configure("ok", foreground="#1a7f37")
        tv.tag_configure("bad", foreground="#cf222e")
        tv.tag_configure("unknown", foreground="#9a6700")
        tv.tag_configure("changed", background="#fff3b0")
        tv.tag_configure("unmapped", foreground="#6e7781")
        return frame, tv

    # --- Loader
    tab1 = ttk.Frame(nb)
    nb.add(tab1, text="Loader check (no game needed)")
    bar1 = ttk.Frame(tab1)
    bar1.pack(fill="x", padx=6, pady=6)
    msg1 = tk.StringVar(value="Each field named for a config key, against the zoo.exe code that loads it.")
    ttk.Label(bar1, textvariable=msg1).pack(side="left")
    f1, tv1 = table(tab1, ("result", "struct", "offset", "field", "the code stores it at", "by"),
                    (70, 120, 70, 230, 220, 260))
    f1.pack(fill="both", expand=True, padx=6, pady=(0, 6))

    def run_loader():
        db["v"] = zt1db.load(ROOT)
        tv1.delete(*tv1.get_children())
        rows = loader_check(db["v"])
        order = {"WRONG": 0, "?": 1, "ok": 2}
        for sname, off, name, status, used in sorted(rows, key=lambda r: (order[r[3]], r[0], r[1])):
            by = db["v"]["structs"][sname]["fields"][zt1db.off_key(off)].get("by", "")
            tv1.insert("", "end", values=({"ok": "right", "WRONG": "WRONG", "?": "can't tell"}[status], sname,
                                          "0x%03x" % off, name, ", ".join("0x%x" % u for u in used) or "-", by),
                       tags=({"ok": "ok", "WRONG": "bad", "?": "unknown"}[status],))
        n = dict((s, sum(1 for r in rows if r[3] == s)) for s in order)
        msg1.set("%d config-key fields: %d right, %d WRONG, %d can't tell (read into a temporary first: "
                 "check those live)"
                 % (len(rows), n["ok"], n["WRONG"], n["?"]))
    ttk.Button(bar1, text="Check", command=run_loader).pack(side="right")

    # --- Species
    tab2 = ttk.Frame(nb)
    nb.add(tab2, text="Species data")
    bar2 = ttk.Frame(tab2)
    bar2.pack(fill="x", padx=6, pady=6)
    ttk.Label(bar2, text="Species:").pack(side="left")
    names = files.species()
    sp = tk.StringVar(value=start_species or ("zebra" if "zebra" in names else (names[0] if names else "")))
    ttk.Combobox(bar2, textvariable=sp, values=names, width=24).pack(side="left", padx=4)
    ttk.Label(bar2, text="Age/sex:").pack(side="left")
    sub = tk.StringVar(value=start_sub or "m")
    ttk.Combobox(bar2, textvariable=sub, values=["m", "f", "y"], width=4).pack(side="left", padx=4)
    msg2 = tk.StringVar(value="What the game's memory should hold for it, at each mapped offset.")
    ttk.Label(bar2, textvariable=msg2).pack(side="left", padx=10)
    f2, tv2 = table(tab2, ("offset", "field", "value in its .ai", "by"), (80, 280, 160, 300))
    f2.pack(fill="both", expand=True, padx=6, pady=(0, 6))

    def show_species(*_):
        db["v"] = zt1db.load(ROOT)
        tv2.delete(*tv2.get_children())
        rows = expected(db["v"], files, sp.get(), sub.get())
        fields = db["v"]["structs"].get(SPECIES, {}).get("fields", {})
        for off, name, val in rows:
            tv2.insert("", "end", values=("0x%03x" % off, name, val, fields[zt1db.off_key(off)].get("by", "")))
        msg2.set("%d of its .ai values have a mapped offset in %s" % (len(rows), SPECIES))
    ttk.Button(bar2, text="Show", command=show_species).pack(side="right")

    # --- Live
    tab3 = ttk.Frame(nb)
    nb.add(tab3, text="Live (game running)")
    bar3 = ttk.Frame(tab3)
    bar3.pack(fill="x", padx=6, pady=6)
    msg3 = tk.StringVar(value="Start the original game with a zoo that has this species, then Find.")
    ttk.Button(bar3, text="Find species in the game", command=lambda: find_live()).pack(side="left")
    ttk.Label(bar3, textvariable=msg3).pack(side="left", padx=10)
    bar3b = ttk.Frame(tab3)
    bar3b.pack(fill="x", padx=6)
    ttk.Label(bar3b, text="Watch address:").pack(side="left")
    addr_v = tk.StringVar()
    ttk.Entry(bar3b, textvariable=addr_v, width=14).pack(side="left", padx=4)
    ttk.Label(bar3b, text="as struct:").pack(side="left")
    struct_v = tk.StringVar(value=SPECIES)
    struct_box = ttk.Combobox(bar3b, textvariable=struct_v, values=sorted(db["v"]["structs"]), width=18)
    struct_box.pack(side="left", padx=4)
    ttk.Label(bar3b, text="size:").pack(side="left")
    size_v = tk.StringVar(value="0x460")
    ttk.Entry(bar3b, textvariable=size_v, width=7).pack(side="left", padx=4)
    watching = {"on": False, "last": {}, "seen": {}, "proc": None}
    only_mapped = tk.BooleanVar(value=False)
    ttk.Button(bar3b, text="Watch", command=lambda: start_watch()).pack(side="left", padx=4)
    ttk.Button(bar3b, text="Stop", command=lambda: watching.update(on=False)).pack(side="left")
    ttk.Checkbutton(bar3b, text="Mapped fields only (and any that change)", variable=only_mapped).pack(side="left",
                                                                                                    padx=8)
    ttk.Button(bar3b, text="Map the selected offset...", command=lambda: map_selected()).pack(side="right")
    pane = ttk.PanedWindow(tab3, orient="vertical")
    pane.pack(fill="both", expand=True, padx=6, pady=6)
    f3, tv3 = table(pane, ("object", "what", "matches"), (140, 500, 300))
    f4, tv4 = table(pane, ("offset", "field", "type", "value", "hex", "as float", "by"),
                    (70, 230, 120, 140, 100, 120, 220))
    pane.add(f3, weight=1)
    pane.add(f4, weight=3)

    def proc():
        if watching["proc"] is None:
            watching["proc"] = Process()
        return watching["proc"]

    def find_live():
        try:
            p = proc()
        except Exception as ex:
            messagebox.showerror("ZT1 Check", str(ex))
            return
        db["v"] = zt1db.load(ROOT)
        want = expected(db["v"], files, sp.get(), sub.get())
        msg3.set("Looking for %s (%d mapped values)..." % (sp.get(), len(want)))
        root.update()
        tv3.delete(*tv3.get_children())
        hits = find_by_values(p, want)
        for addr, rows, good in hits[:5]:
            tv3.insert("", "end", iid="s%x" % addr, values=("0x%08x" % addr, "%s species (%s)" % (sp.get(), sub.get()),
                                                            "%d of %d fields match the .ai" % (good, len(rows))),
                       tags=("ok" if good == len(rows) else "unknown",))
            for a in find_pointers(p, addr, at_offset=0x128)[:50]:
                tv3.insert("", "end", iid="a%x" % a, values=("0x%08x" % a, "  object with this species at +0x128 "
                                                             "(an animal, if ZTAnimal.type is right)", ""))
        msg3.set("%s: %d found. Pick one and Watch it." % (sp.get(), len(hits)) if hits else
                 "No %s found with those values: is one in the zoo, and the mapping right?" % sp.get())

    def pick(_):
        sel = tv3.selection()
        if sel:
            addr_v.set(tv3.item(sel[0])["values"][0])
            struct_v.set(SPECIES if sel[0].startswith("s") else "ZTAnimal")
    tv3.bind("<<TreeviewSelect>>", pick)

    def start_watch():
        tv4.delete(*tv4.get_children())
        watching.update(on=True, last={}, seen={})
        tick()

    def tick():
        if not watching["on"]:
            return
        try:
            base, size = int(addr_v.get(), 16), int(size_v.get(), 0)
            raw = proc().read(base, size)
        except Exception as ex:
            msg3.set("Can't read: %s" % ex)
            watching["on"] = False
            return
        fields = db["v"]["structs"].get(struct_v.get(), {}).get("fields", {})
        # (rows updated in place, so the list keeps its scroll and selection)
        wanted = []
        for off in range(0, len(raw) - 3, 4):
            chunk = raw[off:off + 4]
            f = fields.get(zt1db.off_key(off))
            u = struct.unpack("<I", chunk)[0]
            changed = watching["last"].get(off, u) != u
            watching["last"][off] = u
            if changed:
                watching["seen"][off] = time.time()
            recent = time.time() - watching["seen"].get(off, 0) < 2
            if not f and (only_mapped.get() or u == 0) and not recent:
                continue
            tags = (["changed"] if recent else []) + ([] if f else ["unmapped"])
            iid = "o%x" % off
            wanted.append(iid)
            vals = ("0x%03x" % off, f["name"] if f else "", f["type"] if f else "",
                    decode(chunk, f["type"]) if f else struct.unpack("<i", chunk)[0], "%08x" % u,
                    "%.4g" % struct.unpack("<f", chunk)[0], f.get("by", "") if f else "")
            if tv4.exists(iid):
                tv4.item(iid, values=vals, tags=tags)
            else:
                # (in offset order among the rows there)
                at = len([c for c in tv4.get_children() if int(c[1:], 16) < off])
                tv4.insert("", at, iid=iid, values=vals, tags=tags)
        keep = set(wanted)
        for c in tv4.get_children():
            if c not in keep:
                tv4.delete(c)
        root.after(500, tick)

    def form(title, message, fields):
        """One dialog: fields are (label, default) or (label, default,
        [choices]). The answers by label, or None if cancelled."""
        top = tk.Toplevel(root)
        top.title(title)
        top.transient(root)
        top.resizable(False, False)
        ttk.Label(top, text=message, wraplength=460, justify="left").grid(row=0, column=0, columnspan=2, padx=10,
                                                                        pady=(10, 6), sticky="w")
        vars_ = {}
        for i, fld in enumerate(fields):
            ttk.Label(top, text=fld[0] + ":").grid(row=i + 1, column=0, padx=(10, 4), pady=3, sticky="e")
            v = tk.StringVar(value=fld[1])
            w = ttk.Combobox(top, textvariable=v, values=fld[2], width=40, state="readonly") if len(fld) == 3                 else ttk.Entry(top, textvariable=v, width=43)
            w.grid(row=i + 1, column=1, padx=(0, 10), pady=3, sticky="w")
            if i == 0:
                w.focus_set()
            vars_[fld[0]] = v
        out = {}

        def ok(*_):
            out.update((k, v.get().strip()) for k, v in vars_.items())
            top.destroy()
        btns = ttk.Frame(top)
        btns.grid(row=len(fields) + 1, column=0, columnspan=2, pady=10)
        ttk.Button(btns, text="OK", command=ok).pack(side="left", padx=4)
        ttk.Button(btns, text="Cancel", command=top.destroy).pack(side="left", padx=4)
        top.bind("<Return>", ok)
        top.bind("<Escape>", lambda *_: top.destroy())
        top.grab_set()
        root.wait_window(top)
        return out or None

    def map_selected():
        sel = tv4.selection()
        if not sel:
            messagebox.showinfo("ZT1 Check", "Pick an offset in the watched object first.")
            return
        if me() == "unknown":
            who = form("ZT1 Check", "Your name or handle, for the 'by' column of what you map (asked once).",
                       [("Name", "")])
            if not who or not who["Name"]:
                return
            out = open(os.path.join(HERE, "private", "me.txt"), "w")
            out.write(who["Name"] + "\n")
            out.close()
        off = int(sel[0][1:], 16)
        sname = struct_v.get()
        d = zt1db.load(ROOT)
        old = d["structs"].get(sname, {}).get("fields", {}).get(zt1db.off_key(off), {})
        raw = tv4.item(sel[0])["values"]
        a = form("Map %s+0x%03x" % (sname, off),
                 "What's at %s+0x%03x? Now: %s (hex %s, as a float %s). It goes into the knowledge base under %s."
                 % (sname, off, raw[3], raw[4], raw[5], me()), [
                     ("Field name", old.get("name", "")),
                     ("Type", old.get("type", "int")),
                     ("What it is, in a line", old.get("summary", "")),
                     ("How sure are you?", old.get("confidence", "likely"), zt1db.CONFIDENCE)])
        if not a or not a["Field name"]:
            return
        zt1db.set_field(d, sname, off, me(), a["Field name"], a["Type"] or "int", a["What it is, in a line"],
                        a["How sure are you?"])
        zt1db.log(d, me(), "Field %s+%s = %s (%s), seen live" % (sname, zt1db.off_key(off), a["Field name"],
                                                                 a["Type"] or "int"))
        zt1db.save(ROOT, d)
        db["v"] = d
        msg3.set("Saved %s+0x%03x = %s under %s. Run ZT1_Apply in Ghidra to see it there."
                 % (sname, off, a["Field name"], me()))

    show_species()
    root.mainloop()


if __name__ == "__main__":
    if len(sys.argv) > 1 and sys.argv[1] == "window":
        window(*sys.argv[2:4])
    elif len(sys.argv) > 1:
        cli(sys.argv[1:])
    else:
        window()
