// ZT1 Lab: a zoo.exe function tried on what's picked in the Scene (the
// dock's "Try zoo.exe" tab). The original x86 code runs, sandboxed
// (re/lab/zoorun.py), on the picked animal (or exhibit...) laid out as the
// knowledge base says its struct is, filled with its live values. What it
// reached for and nobody's mapped shows red: find it (Functions, Game
// files, the code), name it (the knowledge base) or give it a value, and
// run it again.
// (loaded after scene.js)

const TRY = {
  addr: store.get('tryAddr', null),
  struct: null, // what the picked thing is taken as (null: worked out)
  calls: store.get('tryCalls', 'skip'), // skip | run
  res: null,
  running: false,
  cur: 0, // the instruction looked at
  fills: store.get('tryFills', {}), // struct -> offset -> a value given by hand
  species: {}, // "lion|m" -> its .ai values
  ranOn: '',
};

// What the picked thing is, as zoo.exe's structs
function tryStructFor(sel, f) {
  if (TRY.struct && S.kb && S.kb.structs[TRY.struct]) return TRY.struct;
  if (sel && sel.kind === 'animal') return 'ZTAnimal';
  if (sel && sel.kind === 'exhibit') return 'ZTHabitat';
  const c = f && contextStruct(f);
  return c && S.kb.structs[c] ? c : 'ZTAnimal';
}

// A species' .ai values from the engine (for ZTAnimalType)
function speciesValues(species, subtype) {
  const key = `${species}|${subtype}`;
  if (TRY.species[key]) return Promise.resolve(TRY.species[key]);
  const st = S.kb.structs.ZTAnimalType;
  const keys = st ? Object.values(st.fields).map((f) => f.name).filter((n) => /^c[A-Z]/.test(n)) : [];
  return new Promise((resolve) => {
    TRY.waitSpecies = (m) => {
      if (m.species !== species) return false;
      TRY.species[key] = m.values;
      resolve(m.values);
      return true;
    };
    lab.engineSend({ cmd: 'speciesValues', species, subtype, keys });
    setTimeout(() => resolve(TRY.species[key] || {}), 3000);
  });
}

// The objects the function's given: the picked thing as its struct, what
// its pointers point to (a struct the knowledge base knows), each field
// filled from the engine where the names match, or by hand
async function tryObjects(sel, structName) {
  let speciesVals = {};
  if (sel && sel.kind === 'animal') speciesVals = await speciesValues(sel.species, sel.female ? 'f' : 'm');
  const objs = [];
  const liveFor = (struct) => (struct === 'ZTAnimalType' ? speciesVals : sel || {});
  const add = (struct, name, depth) => {
    const i = objs.length;
    const st = S.kb.structs[struct] || { fields: {}, size: 0x400 };
    const o = { name, struct, size: parseInt(st.size, 0) || 0x400, fields: [] };
    objs.push(o);
    const live = liveFor(struct);
    for (const [offHex, f] of Object.entries(st.fields || {})) {
      const off = parseInt(offHex, 16);
      const t = (f.type || 'int').trim();
      const target = t.endsWith('*') ? t.replace(/\s*\*$/, '').trim() : null;
      if (target && S.kb.structs[target] && depth < 2) {
        const to = add(target, target === 'ZTAnimalType' && sel ? sel.species : target, depth + 1);
        o.fields.push({ off, type: t, name: f.name, to });
        continue;
      }
      const v = live[f.name];
      const num = typeof v === 'boolean' ? (v ? 1 : 0) : typeof v === 'number' ? v : undefined;
      o.fields.push({ off, type: t, name: f.name, value: num === undefined ? null : num, live: num !== undefined });
    }
    for (const [offHex, v] of Object.entries(TRY.fills[struct] || {})) {
      const off = parseInt(offHex, 16);
      const had = o.fields.find((x) => x.off === off);
      if (had) Object.assign(had, { value: v, given: true, to: undefined });
      else o.fields.push({ off, type: 'int', name: null, value: v, given: true });
    }
    return i;
  };
  add(structName, sel ? sel.name || sel.kind : structName, 0);
  return objs;
}

function tryNames() {
  const names = {};
  for (const g of S.ghidra.functions || []) names[hexKey(g.address)] = g.name;
  for (const [k, e] of Object.entries(kbFns())) names[hexKey(k)] = e.name;
  return names;
}

async function tryRun(addr) {
  if (addr) {
    TRY.addr = hexKey(addr);
    store.set('tryAddr', TRY.addr);
    TRY.res = null;
  }
  if (S.tab !== 'scene') setTab('scene');
  if (dockedOn()) showPanel('try');
  else if (!S.consoleOpen || DK.tab !== 'try') {
    S.consoleOpen = true;
    DK.tab = 'try';
    renderConsole();
    requestAnimationFrame(sizeCanvas);
  }
  const s = sc();
  const sel = s && s.sel;
  if (!TRY.addr) return renderTry();
  if (!sel) {
    TRY.res = null;
    toast('Pick something in the view (an animal, an exhibit…) to run it on.', 3000);
    return renderTry();
  }
  const f = fnInfo(TRY.addr);
  const structName = tryStructFor(sel, f);
  TRY.running = true;
  renderTry();
  const objects = await tryObjects(sel, structName);
  TRY.ranOn = `${sel.name || sel.kind} as ${structName}`;
  const res = await lab.zooRun({ addr: TRY.addr, calls: TRY.calls, objects, names: tryNames(), ecx: 'obj:0', limit: 20000, keep: 4000 });
  res.objects = objects;
  res.structName = structName;
  TRY.res = res;
  TRY.running = false;
  TRY.cur = 0;
  // (the first instruction it touched something it shouldn't, if any)
  const bad = (res.steps || []).findIndex((x) => x.acc.some((a) => (a.k === 'obj' && !a.known) || a.k === 'nothing'));
  if (bad >= 0) TRY.cur = bad;
  renderTry();
}

const hx = (n) => '0x' + (n >>> 0).toString(16);

function renderTry() {
  const box = $('try');
  if (!box) return;
  const s = sc();
  const sel = s && s.sel;
  const f = TRY.addr ? fnInfo(TRY.addr) : null;
  const structs = S.kb ? Object.keys(S.kb.structs) : [];
  const structName = f ? tryStructFor(sel, f) : '';
  const r = TRY.res;
  let head = `<div class="tryhead">`;
  head += f
    ? `<span class="label">zoo.exe</span><span><span class="mono amber clickable" data-act="openCode" data-arg="${f.addr}">${esc(f.addr)}</span> <b>${esc(displayName(f))}</b></span>`
    : `<span class="muted">Pick a zoo.exe function: <b>Try it here</b> in Code, Functions or Steps, or</span>`;
  head += `<input class="search small" id="try-fn" placeholder="an address or a name" style="width:170px">`;
  head += `<span class="muted">on</span><b>${esc(sel ? sel.name || sel.kind : 'nothing picked')}</b><span class="muted">as</span><select class="sel" id="try-struct">${structs
    .map((n) => `<option${n === structName ? ' selected' : ''}>${esc(n)}</option>`)
    .join('')}</select>`;
  head += `<span class="muted">calls</span><div class="seg mini">${[
    ['skip', 'Skip'],
    ['run', 'Run them'],
  ]
    .map(([k, l]) => `<div class="${TRY.calls === k ? 'on' : ''}" data-act="tryCalls" data-arg="${k}">${l}</div>`)
    .join('')}</div>`;
  head += `<div class="spacer"></div><span class="dbg go${!f || !sel ? ' off' : ''}" data-act="tryGo">${TRY.running ? 'Running…' : r ? '▶ Run again' : '▶ Run it'}</span>`;
  if (r && r.touched && r.touched.some((t) => t.obj === 0 && t.writes && t.live)) head += `<span class="dbg" data-act="tryApply" title="Put what it wrote into the picked one, in the scene">Apply what it wrote</span>`;
  if (f) head += `<span class="dbg" data-act="modFor" data-arg="${f.addr}">Make it a mod</span>`;
  head += `</div>`;
  let body;
  if (!r) {
    body = `<div class="muted tryempty">${
      TRY.running
        ? 'Running zoo.exe’s code…'
        : !f
          ? 'Find a function in <b>Functions</b> or <b>Code</b> (or one in Steps) and press <b>Try it here</b>. It runs zoo.exe’s own code on what’s picked, and shows everything it reaches for: <span class="green">green</span> what we know, <span class="red">red</span> what nobody’s mapped yet.'
          : !sel
            ? 'Pick something in the view to run it on.'
            : 'Press <b>Run it</b>.'
    }</div>`;
  } else body = `<div class="try3"><div class="tryneed">${tryNeedHtml(r)}</div><div class="tryasm" id="try-asm">${tryAsmHtml(r)}</div><div class="tryc code" id="try-c">${tryCHtml(r, f)}</div></div>`;
  setHtml(box, head + body);
  const inp = $('try-fn');
  if (inp && !inp.onkeydown)
    inp.onkeydown = (e) => {
      if (e.key !== 'Enter') return;
      const q = inp.value.trim().toLowerCase();
      if (!q) return;
      const hit = /^(0x)?[0-9a-f]{5,8}$/.test(q) ? { addr: hexKey(q.startsWith('0x') ? q : '0x' + q) } : allFunctions().find((x) => x.name.toLowerCase() === q) || allFunctions().find((x) => x.name.toLowerCase().includes(q));
      if (!hit) return toast('No function like that.');
      tryRun(hit.addr);
    };
  const ss = $('try-struct');
  if (ss)
    ss.onchange = () => {
      TRY.struct = ss.value;
      TRY.res = null;
      renderTry();
    };
  const cur = document.querySelector('#try-asm .arow.on');
  if (cur && TRY.scrolledTo !== `${TRY.res && TRY.res.id}:${TRY.cur}`) {
    TRY.scrolledTo = `${TRY.res && TRY.res.id}:${TRY.cur}`;
    const a = $('try-asm');
    const y = cur.getBoundingClientRect().top - a.getBoundingClientRect().top + a.scrollTop;
    a.scrollTop = Math.max(0, y - a.clientHeight / 3);
  }
}

// What it wanted: what's missing first (red), then what it read and wrote
function tryNeedHtml(r) {
  const o0 = (r.objects || [])[0] || {};
  let h = `<div class="trystat ${r.ok ? 'green' : 'red'}">${
    r.ok ? `Ran ${r.count.toLocaleString()} instructions and returned <b>${r.eax}</b>` : r.returned ? `Returned ${r.eax}, with problems` : 'It didn’t get to the end'
  } <span class="dim">· on ${esc(TRY.ranOn)}</span></div>`;
  const missing = (r.errors || []).filter((e) => e.kind === 'unmapped');
  const other = (r.errors || []).filter((e) => e.kind !== 'unmapped');
  if (missing.length || other.length) {
    h += `<div class="label red">It wanted, and couldn’t find (${missing.length + other.length})</div>`;
    for (const e of missing) {
      const t = (r.touched || []).find((x) => x.obj === e.obj && x.off === e.off);
      h += `<div class="need bad"><div><span class="mono">${esc(e.struct)}+${hx(e.off)}</span> <span class="muted">${t ? (t.writes ? (t.reads ? 'read and written' : 'written') : 'read') : ''}${
        t && t.size ? ` · ${t.size} byte${t.size > 1 ? 's' : ''}` : ''
      }</span></div><div class="nacts"><span class="clickable" data-act="tryShow" data-arg="${e.at}">where</span><span class="clickable blue" data-act="tryName" data-arg="${esc(e.struct)}|${e.off}|${t ? t.type : 'int'}">Name it</span><span class="clickable" data-act="tryFill" data-arg="${esc(e.struct)}|${e.off}">Give a value</span>${
        e.struct === 'ZTAnimalType' ? `<span class="clickable" data-act="tryFind" data-arg="species">Game files</span>` : ''
      }</div></div>`;
    }
    for (const e of other)
      h += `<div class="need bad"><div>${esc(e.text)}</div><div class="nacts"><span class="clickable" data-act="tryShow" data-arg="${e.at}">where</span>${
        e.kind === 'pointer' ? `<span class="clickable blue" data-act="tryFill" data-arg="${esc(o0.struct)}|0">Fill in +0</span>` : ''
      }</div></div>`;
  }
  const known = (r.touched || []).filter((t) => t.known);
  if (known.length) {
    h += `<div class="label">What it used</div>`;
    for (const t of known) {
      const o = r.objects[t.obj] || {};
      const fld = (o.fields || []).find((x) => x.off === t.off) || {};
      const src = fld.given ? '<span class="badge amber">given</span>' : fld.live ? '<span class="badge">live</span>' : fld.to !== undefined ? '<span class="badge blue">→ ' + esc((r.objects[fld.to] || {}).struct || '') + '</span>' : '<span class="badge amber">no value</span>';
      const val = fld.to !== undefined ? '' : t.writes ? `${fmt(t.first)} → <b>${fmt(t.last)}</b>` : fmt(t.first);
      h += `<div class="need ok"><div><span class="green">${esc(t.name || hx(t.off))}</span> <span class="dim mono">${esc(o.struct)}+${hx(t.off)}</span> ${src}</div><div>${val} <span class="dim">${
        t.writes ? 'written' : 'read'
      }</span></div></div>`;
    }
  }
  if ((r.strings || []).length) h += `<div class="label">Text it used</div>` + r.strings.map((x) => `<div class="need"><span class="str">“${esc(x.text)}”</span> <span class="dim mono">${hx(x.addr)}</span></div>`).join('');
  if ((r.globals || []).length)
    h +=
      `<div class="label">Globals (as in the file)</div>` +
      r.globals
        .slice(0, 30)
        .map((g) => `<div class="need"><span class="mono clickable" data-act="xrefs" data-arg="${hx(g.addr)}" title="Where it's used">${hx(g.addr)}</span> <span>${g.value}</span> <span class="dim">${g.writes ? 'written' : 'read'}</span></div>`)
        .join('');
  if ((r.calls || []).length)
    h +=
      `<div class="label">What it called</div>` +
      r.calls
        .map(
          (c) =>
            `<div class="need"><span class="${c.to ? 'clickable blue' : ''}" ${c.to ? `data-act="openCode" data-arg="${hx(c.to)}"` : ''}>${esc(c.name)}</span> <span class="dim">×${c.count} · ${esc(c.why)}</span>${
              c.to && !/!/.test(c.name) ? ` <span class="clickable green" data-act="tryFn" data-arg="${hx(c.to)}">try</span>` : ''
            }</div>`,
        )
        .join('');
  return h;
}

// Its instructions as they ran (the first 4000), what each touched
function tryAsmHtml(r) {
  const steps = r.steps || [];
  const cur = steps[TRY.cur];
  let h = '';
  if (cur)
    h += `<div class="regs mono">${(r.regs || [])
      .map((n, i) => `<span><span class="dim">${n}</span> ${(cur.r[i] >>> 0).toString(16)}</span>`)
      .join('')}</div>`;
  h += steps
    .map((x, i) => {
      const bad = x.acc.some((a) => (a.k === 'obj' && !a.known) || a.k === 'nothing');
      const acc = x.acc
        .map((a) => {
          const what = a.k === 'obj' ? a.name || `${(r.objects[a.o] || {}).struct}+${hx(a.off)}` : a.k === 'global' ? hx(a.a) : `${hx(a.a)}?`;
          const cls = a.k === 'obj' ? (a.known ? 'green' : 'red') : a.k === 'nothing' ? 'red' : 'dim';
          return `<span class="${cls}">${a.w ? '←' : ''}${esc(what)}${a.w ? '' : ' ' + a.v}${a.w ? ' = ' + a.v : ''}</span>`;
        })
        .join(' ');
      return `<div class="arow${i === TRY.cur ? ' on' : ''}${bad ? ' bad' : ''}" data-act="tryStep" data-arg="${i}"><span class="k-addr">${x.a.toString(16)}</span><span style="padding-left:${x.d * 10}px">${highlightAsm(
        x.t,
      )}${x.call ? ` <span class="${x.call.skipped ? 'dim' : 'blue'}">${esc(x.call.name)}${x.call.skipped ? ' (skipped)' : ''}</span>` : ''}</span><span class="acc">${acc}</span></div>`;
    })
    .join('');
  if (r.truncated) h += `<div class="dim" style="padding:6px 10px">… ${(r.count - steps.length).toLocaleString()} more not kept</div>`;
  return h;
}

// Its decompiled C, the lines with the offsets it touched lit
function tryCHtml(r, f) {
  if (!f) return '';
  const path = '/decompile?addr=' + plainHex(f.addr);
  const d = gNow(path);
  if (!d) {
    G(path).then(() => S.tab === 'scene' && (dockedOn() ? $('try') : DK.tab === 'try') && renderTry());
    return '<div class="muted" style="padding:10px 14px">Asking Ghidra…</div>';
  }
  if (d.error) return `<div class="muted" style="padding:10px 14px;white-space:normal;font-family:var(--sans)">Ghidra isn’t connected, and this function hasn’t been looked at before: its C shows once it has been.</div>`;
  const offs = new Map();
  for (const t of r.touched || []) if (t.obj === 0) offs.set(t.off, t.known);
  CTX = r.structName;
  const lines = (d.json.c || '').replace(/\r/g, '').split('\n');
  while (lines.length && !lines[0].trim()) lines.shift();
  return lines
    .map((l, i) => {
      let mark = '';
      for (const m of l.matchAll(/\+\s*(0x[0-9a-f]+)/gi)) {
        const k = parseInt(m[1], 16);
        if (offs.has(k)) mark = offs.get(k) ? (mark || 'okl') : 'badl';
      }
      return `<div class="ln ${mark}"><span class="no">${i + 1}</span><span>${highlightC(l, fieldLookup)}</span></div>`;
    })
    .join('');
}

// (after a field's named or given a value: run again where it was asked
// from - the step's binary in Steps, else Try)
function tryAgain() {
  if ((dockedOn() ? $('dk-bin') || $('dk-err') : S.consoleOpen && DK.tab === 'steps') && typeof renderStepView === 'function') {
    BIN.key = null;
    renderStepView();
  } else tryRun();
}

Object.assign(ACTIONS, {
  tryRun: (addr) => tryRun(addr),
  tryFn: (addr) => tryRun(addr),
  tryGo: () => tryRun(),
  tryCalls: (k) => {
    TRY.calls = k;
    store.set('tryCalls', k);
    tryRun();
  },
  tryStep: (i) => {
    TRY.cur = +i;
    renderTry();
  },
  tryShow: (at) => {
    const i = (TRY.res && TRY.res.steps ? TRY.res.steps : []).findIndex((x) => x.a === +at);
    if (i >= 0) {
      TRY.cur = i;
      renderTry();
    } else toast('That was past the instructions kept.');
  },
  // A field it wanted: named into the knowledge base (as you), then run again
  tryName: async (arg) => {
    const [struct, off, type] = arg.split('|');
    if (!(await ensureMe())) return;
    const s = sc();
    const engineNames = s && s.sel ? Object.keys(s.sel).filter((k) => typeof s.sel[k] === 'number' || typeof s.sel[k] === 'boolean') : [];
    const a = await ask(`Name ${struct}+${hx(+off)}`, [
      { key: 'name', label: `Its name. Use one of the engine’s to fill it live${engineNames.length ? ': ' + engineNames.join(', ') : ''}`, hint: 'hunger' },
      { key: 'type', label: 'Its type (int, float, byte, short, or Struct *)', value: type || 'int' },
      { key: 'note', label: 'What it is (a note for whoever comes next)', area: true },
    ]);
    if (!a || !a.name) return;
    const r = await lab.kbRun(['field', struct, hx(+off), a.name, a.type || 'int', a.note || '']);
    if (!r.ok) return toast('Couldn’t save it: ' + esc(r.out.slice(0, 200)), 5000);
    await loadKb();
    toast(`<b>${esc(struct)}.${esc(a.name)}</b> saved to the knowledge base. Running it again…`, 2500);
    tryAgain();
  },
  tryFill: async (arg) => {
    const [struct, off] = arg.split('|');
    const was = (TRY.fills[struct] || {})[hx(+off)];
    const a = await ask(`A value for ${struct}+${hx(+off)}`, [
      { key: 'v', label: 'A number (0x… for an address: a vtable, a pointer). Empty takes it away.', value: was !== undefined ? String(was) : '' },
    ]);
    if (!a) return;
    TRY.fills[struct] = TRY.fills[struct] || {};
    if (a.v === '') delete TRY.fills[struct][hx(+off)];
    else TRY.fills[struct][hx(+off)] = /^0x/i.test(a.v) ? parseInt(a.v, 16) : parseFloat(a.v);
    store.set('tryFills', TRY.fills);
    tryAgain();
  },
  tryFind: (what) => {
    const s = sc();
    if (what === 'species' && s && s.sel && s.sel.file) {
      gameFilesGo(s.sel.file, s.sel.file);
    }
  },
  // Its writes into the picked one, in the scene (the engine's fields of
  // the same names)
  tryApply: () => {
    const r = TRY.res;
    if (!r) return;
    let n = 0;
    for (const t of r.touched || [])
      if (t.obj === 0 && t.writes && t.known) {
        const fld = (r.objects[0].fields || []).find((x) => x.off === t.off);
        if (fld && fld.live) {
          lab.engineSend({ cmd: 'set', field: t.name, value: t.last });
          n++;
        }
      }
    toast(n ? `Put ${n} value${n > 1 ? 's' : ''} it wrote into the scene.` : 'Nothing it wrote is a field the engine has.', 3000);
  },
});

SCENE_MSG.speciesValues = (m) => TRY.waitSpecies && TRY.waitSpecies(m);
