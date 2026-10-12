// ZT1 Lab: growing the knowledge base from what zoo.exe says about itself
// (Original Game > Grow the database). Three tabs, each a list of what's
// found beside what re/knowledge.json has, saved through re/kb.py:
// - Config loaders: every key a loader reads (cHungerCheck) and where it
//   goes - a field of the struct it fills, or a global - main.js kb:loaders
// - Class tables: every table of functions a constructor stores (a class),
//   slot by slot, with the other classes sharing each slot - kb:classes
// - The game's text: names its debug text gives, its source file paths -
//   kb:text
// What's saved is by the tool that found it ("Lab loader mapper"...), so it's
// told apart from what you name yourself.

const KBT = { tab: 'loaders', loaders: null, classes: null, text: null, open: null, busy: '' };

function kbtClose() {
  const m = $('modal');
  m.classList.add('hidden');
  m.innerHTML = '';
  sendPlayRect();
}

function kbtBody() {
  const row = (cells, attrs = '') => `<tr ${attrs}>${cells.map((c) => `<td>${c}</td>`).join('')}</tr>`;
  if (KBT.busy) return `<div class="muted">${esc(KBT.busy)}</div>`;
  if (KBT.tab === 'loaders') {
    const L = KBT.loaders;
    if (!L) return '<div class="muted">Reading zoo.exe…</div>';
    if (L.error) return `<div class="muted">${esc(L.error)}</div>`;
    const list = L.loaders
      .map((g) => {
        const open = KBT.open === g.loader;
        const head = row(
          [
            `<span class="blue clickable" data-kopen="${esc(g.loader)}">${esc(g.loader)}</span> <span class="muted">${esc(g.name || '')}</span>`,
            `${g.count} keys`,
            `${g.fields} fields · ${g.globals} globals`,
            `<span class="${g.mapped ? 'green' : 'muted'}">${g.mapped} in the base</span>`,
            `<input class="field" style="width:150px" data-kstruct="${esc(g.loader)}" value="${esc(g.struct)}" placeholder="Struct (ZTKeeperType)">`,
            `<div class="btn small" data-ksave="${esc(g.loader)}">Save</div>`,
          ],
          '',
        );
        const keys = open
          ? g.reads
              .map((x) =>
                row(
                  [
                    `<span class="mono">${esc(x.key)}</span>`,
                    x.dest ? (x.dest.field ? `+${esc(x.dest.field)}` : `global ${esc(x.dest.global)}`) : '<span class="muted">a local</span>',
                    `<span class="muted">${esc(x.kind)}</span>`,
                    x.have ? `<span class="green">${esc(x.have)}</span>` : '',
                    `<span class="clickable muted mono" data-go="${esc(x.at)}">${esc(x.at)}</span>`,
                    '',
                  ],
                  'class="sub"',
                ),
              )
              .join('')
          : '';
        return head + keys;
      })
      .join('');
    return `<div class="muted" style="margin-bottom:6px">${L.reads} key reads in ${L.loaders.length} loaders. Name the struct a loader fills and Save: each key becomes a field at its offset (where nothing's named yet), each global gets its key's name.</div><table class="rec-table">${list}</table>`;
  }
  if (KBT.tab === 'classes') {
    const C = KBT.classes;
    if (!C) return '<div class="muted">Reading zoo.exe…</div>';
    if (C.error) return `<div class="muted">${esc(C.error)}</div>`;
    const tables = C.tables.slice().sort((a, b) => !!b.name - !!a.name || b.slots - a.slots);
    const list = tables
      .map((t) => {
        const open = KBT.open === t.addr;
        let rows = row([
          `<span class="blue clickable" data-copen="${esc(t.addr)}">${esc(t.addr)}</span>`,
          `${t.slots} slots`,
          `<input class="field" style="width:160px" data-cname="${esc(t.addr)}" value="${esc(t.name)}" placeholder="Class name">`,
          t.saved ? '<span class="green">in the base</span>' : `<span class="muted">${esc(t.how)}</span>`,
          `<div class="btn small" data-csave="${esc(t.addr)}">Save</div>`,
        ]);
        if (open && KBT.slots && KBT.slots.addr === t.addr)
          rows += KBT.slots.slots
            .map((s) =>
              row(
                [
                  `<span class="mono muted">+${esc(s.slot)}</span>`,
                  `<span class="blue clickable mono" data-go="${esc(s.function)}">${esc(s.function)}</span>`,
                  s.known ? `<span class="green">${esc(s.known)}</span>` : '',
                  s.shared.length ? `<span class="muted">also in ${esc(s.shared.join(', '))}</span>` : '<span class="amber">its own</span>',
                  '',
                ],
                'class="sub"',
              ),
            )
            .join('');
        return rows;
      })
      .join('');
    return `<div class="muted" style="margin-bottom:6px">${C.tables.length} classes' tables of functions in zoo.exe (what their constructors store at an object's start), ${C.named} named. Open one to see its slots and which other named classes share each (the class they come from); name it and Save: every slot goes in the base.</div><table class="rec-table">${list}</table>`;
  }
  const T = KBT.text;
  if (!T) return '<div class="muted">Asking Ghidra…</div>';
  if (T.error) return `<div class="muted">${esc(T.error)}</div>`;
  return `<div class="muted" style="margin-bottom:6px">${T.names} functions named by their own debug text (one name each - more are other functions' text built in), ${T.sources} giving the original's source file. Save all: provisional names where nothing's named (by its file - gxcanvas_4014f5 - where only that's known), source files on them.</div><div class="btn small primary" data-tsave="1" style="margin-bottom:8px">Save all</div><table class="rec-table">${T.rows
    .map((r) =>
      row([
        `<span class="blue clickable mono" data-go="${esc(r.address)}">${esc(r.address)}</span>`,
        r.name ? `<b class="amber">${esc((r.cls ? r.cls + '::' : '') + r.name)}</b>` : '',
        r.source ? `<span class="mono">${esc(r.source)}</span>` : '',
        r.known ? '<span class="green">named already</span>' : '',
      ]),
    )
    .join('')}</table>`;
}

function kbtShow() {
  const m = $('modal');
  const tab = (k, label) => `<div class="btn small${KBT.tab === k ? ' primary' : ''}" data-ktab="${k}">${label}</div>`;
  m.innerHTML = `<div class="dialog" style="width:min(1100px,94vw);max-height:88vh;display:flex;flex-direction:column"><div class="h">Grow the database</div><div style="display:flex;gap:6px;margin-bottom:8px">${tab('loaders', 'Config loaders')}${tab('classes', 'Class tables')}${tab('text', "The game's text")}<div style="flex:1"></div><div class="btn small ghost" data-act="ghidraRefresh" title="After ZT1_Apply (or naming things) in your own Ghidra: the Lab's copy of the project made again from yours, so it shows the same">Refresh the Lab's Ghidra</div></div><div style="overflow:auto;flex:1">${kbtBody()}</div><div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
  m.classList.remove('hidden');
  sendPlayRect();
  m.onclick = async (e) => {
    const t = e.target;
    const d = t.dataset;
    const go = t.closest('[data-go]');
    if (d.x === 'no' || t === m) return kbtClose();
    if (go) {
      kbtClose();
      return ACTIONS.openCode(go.dataset.go);
    }
    if (d.ktab) return ACTIONS.kbTab(d.ktab);
    if (d.kopen) {
      KBT.open = KBT.open === d.kopen ? null : d.kopen;
      return kbtShow();
    }
    if (d.copen) {
      KBT.open = KBT.open === d.copen ? null : d.copen;
      if (KBT.open) KBT.slots = await lab.kbClass(d.copen);
      return kbtShow();
    }
    if (d.ksave) {
      const struct = m.querySelector(`[data-kstruct="${d.ksave}"]`).value.trim();
      return ACTIONS.kbLoaderSave(`${d.ksave}|${struct}`);
    }
    if (d.csave) {
      const name = m.querySelector(`[data-cname="${d.csave}"]`).value.trim();
      return ACTIONS.kbClassSave(`${d.csave}|${name}`);
    }
    if (d.tsave) return ACTIONS.kbTextSave();
  };
}

Object.assign(ACTIONS, {
  // (the window, on a tab: loaders, classes, text)
  kbTools: async (tab) => {
    KBT.tab = tab || KBT.tab;
    kbtShow();
    await ACTIONS.kbTab(KBT.tab);
  },
  kbTab: async (tab) => {
    KBT.tab = tab;
    KBT.open = null;
    kbtShow();
    if (tab === 'loaders' && !KBT.loaders) KBT.loaders = await lab.kbLoaders();
    if (tab === 'classes' && !KBT.classes) KBT.classes = await lab.kbClasses();
    if (tab === 'text' && !KBT.text) KBT.text = await lab.kbText();
    if (!$('modal').classList.contains('hidden')) kbtShow();
    const r = { loaders: KBT.loaders, classes: KBT.classes, text: KBT.text }[tab];
    // (for the Lab's tools: a summary, not every row)
    if (!r || r.error) return r || {};
    if (tab === 'loaders')
      return { reads: r.reads, loaders: r.loaders.map((g) => ({ loader: g.loader, name: g.name, struct: g.struct, keys: g.count, fields: g.fields, globals: g.globals, mapped: g.mapped })) };
    if (tab === 'classes')
      return {
        tables: r.tables.length,
        named: r.tables.filter((t) => t.name).map((t) => ({ addr: t.addr, name: t.name, slots: t.slots, saved: t.saved })),
        // (the rest, "table:slots", biggest first)
        unnamed: r.tables.filter((t) => !t.name).sort((a, b) => b.slots - a.slots).map((t) => `${t.addr}:${t.slots}`),
      };
    return { names: r.names, sources: r.sources, rows: r.rows.length };
  },
  // ("<loader>|<struct>", or "<loader>|<struct>|likely" for a guess): its
  // fields and globals saved
  kbLoaderSave: async (arg) => {
    const [loader, struct, confidence] = String(arg).split('|');
    const r = await lab.kbLoaderSave(loader.trim(), (struct || '').trim(), 'Lab loader mapper', (confidence || '').trim() || 'sure');
    toast(r.error ? esc(r.error) : esc(r.said || 'Saved.'), 5000);
    KBT.loaders = await lab.kbLoaders();
    if (!$('modal').classList.contains('hidden')) kbtShow();
    // (what the rest of the Lab knows, the census's class names too)
    await loadKb();
    return r;
  },
  // A loader's keys (for the Lab's tools): "<loader>"
  kbLoaderKeys: async (loader) => {
    if (!KBT.loaders) KBT.loaders = await lab.kbLoaders();
    const g = (KBT.loaders.loaders || []).find((x) => x.loader === String(loader).trim());
    return g ? { loader: g.loader, struct: g.struct, keys: g.reads.map((x) => ({ key: x.key, dest: x.dest, have: x.have, at: x.at })) } : { error: 'no such loader' };
  },
  // Several loaders' keys, a line each (for the Lab's tools): "<loader> <loader>..."
  // - or "all" - each key as "cKey +0x12c" / "cKey global 0x6393f8" / "cKey"
  kbLoaderBrief: async (arg) => {
    if (!KBT.loaders) KBT.loaders = await lab.kbLoaders();
    const want = String(arg || 'all').trim().split(/[\s,]+/);
    const out = {};
    for (const g of KBT.loaders.loaders || [])
      if (want[0] === 'all' || want.includes(g.loader))
        out[g.loader + (g.struct ? ' ' + g.struct : '')] = g.reads
          .map((x) => x.key + (x.dest ? (x.dest.field ? ' +' + x.dest.field : ' global ' + x.dest.global) : '') + (x.kind !== 'int' ? ' (' + x.kind + ')' : ''))
          .join(', ');
    return out;
  },
  // ("<table>|<name>"): a class and every slot of its table saved
  kbClassSave: async (arg) => {
    const [addr, name] = String(arg).split('|');
    if (!name || !name.trim()) return toast('Name the class first.', 3000);
    const r = await lab.kbClassSave(addr.trim(), name.trim(), 'Lab class tables');
    toast(r.error ? esc(r.error) : esc(r.said || 'Saved.'), 5000);
    KBT.classes = await lab.kbClasses();
    if (!$('modal').classList.contains('hidden')) kbtShow();
    // (what the rest of the Lab knows, the census's class names too)
    await loadKb();
    return r;
  },
  // A table's slots (for the Lab's tools): "<table>"
  kbClassSlots: async (addr) => lab.kbClass(String(addr).trim()),
  // Tables against the named classes, in short ("<table> <table>..."): its
  // slots, how many each named class shares, and how many are its own. A
  // base class's table shares most with all its kind; one no object uses
  // (abstract) is often that base
  kbClassCompare: async (arg) => {
    const out = {};
    for (const a of String(arg || '').trim().split(/[\s,]+/).filter(Boolean)) {
      const r = await lab.kbClass(a);
      if (r.error) {
        out[a] = r.error;
        continue;
      }
      const shared = {};
      for (const s of r.slots) for (const n of s.shared) shared[n] = (shared[n] || 0) + 1;
      out[a] = { slots: r.slots.length, own: r.slots.filter((s) => !s.shared.length).length, shared, known: r.slots.filter((s) => s.known).map((s) => `${s.slot} ${s.known}`) };
    }
    return out;
  },
  // Engine references in code Ghidra made no function of (lead q2): each
  // with where its function would start (for the Lab's tools)
  kbOrphans: async () => lab.kbOrphans(),
  // A function made at each of those starts ("all", or "<start> <start>..."),
  // where what's before it ends code (a return, a jump away, padding): the
  // bridge keeps them in re/made_functions.json, ZT1_Apply makes them in your
  // project
  kbMakeOrphans: async (arg) => {
    const r = await lab.kbOrphans();
    if (r.error) return r;
    const want = String(arg || 'all').trim().split(/[\s,]+/);
    const ends = (t) => /^(ret|int3|nop|jmp 0x)/.test(t);
    const out = [];
    for (const s of [...new Set(r.orphans.map((o) => o.start))]) {
      if (want[0] !== 'all' && !want.includes(s)) continue;
      const o = r.orphans.find((x) => x.start === s);
      if (want[0] === 'all' && !ends(o.before)) {
        out.push({ start: s, skipped: 'before it: ' + o.before });
        continue;
      }
      const g = await lab.ghidra(`/makefn?addr=${plainHex(s)}&by=${encodeURIComponent(S.me || '')}`);
      const j = g && g.json;
      out.push(j && j.ok ? { start: s, made: j.function.name, size: j.function.size } : { start: s, error: (j && j.error) || 'no answer' });
    }
    delete S.cache['/functions'];
    return { made: out.filter((x) => x.made).length, results: out };
  },
  // The Lab's Ghidra copy made again from your own project (after ZT1_Apply
  // or naming things in your Ghidra)
  ghidraRefresh: async () => {
    const r = await lab.ghidraRefresh();
    toast(esc(r.error || r.said), 6000);
    S.cache = {};
    return r;
  },
  kbTextSave: async () => {
    const r = await lab.kbTextSave('Lab (the game\'s text)');
    toast(r.error ? esc(r.error) : esc(r.said || 'Saved.'), 5000);
    KBT.text = await lab.kbText();
    if (!$('modal').classList.contains('hidden')) kbtShow();
    // (what the rest of the Lab knows, the census's class names too)
    await loadKb();
    return r;
  },
});
