// ZT1 Lab: the page. Five views (Scene, Code, Functions, Mods, Play) over
// the Lab engine (frames and state, from main.js), Ghidra (through the
// bridge), the knowledge base and the mods' files.
// (window.lab, from preload.js, is the global "lab")
const $ =(id) => document.getElementById(id);
// The window never scrolls as a whole (its panels scroll inside): a shifted
// page hides the top bar, and a reload would bring the shift back
history.scrollRestoration = 'manual';
const pinPage = () => {
  if (window.scrollX || window.scrollY) window.scrollTo(0, 0);
  for (const e of [document.documentElement, document.body])
    if (e && (e.scrollTop || e.scrollLeft)) e.scrollTop = e.scrollLeft = 0;
};
window.addEventListener('scroll', pinPage, true);
window.addEventListener('load', pinPage);
const store = {
  get: (k, d) => {
    try {
      const v = localStorage.getItem(k);
      return v === null ? d : JSON.parse(v);
    } catch {
      return d;
    }
  },
  set: (k, v) => {
    try {
      localStorage.setItem(k, JSON.stringify(v));
    } catch {}
  },
};

const S = {
  tab: store.get('tab', 'scene'),
  expert: false, // (one look: the Simple/Expert switch is gone)
  engine: { status: 'starting', detail: '', state: null, log: [] },
  ghidra: { ok: false, program: '', functions: null, loading: false, building: false, said: '' },
  kb: null,
  refs: {},
  me: '',
  mods: [],
  mod: store.get('mod', null),
  sel: { addr: store.get('fn', '0x4378df'), animal: null },
  drawer: { open: false, addr: null, field: null },
  fview: { filter: 'All', q: '', limit: 300 },
  editor: { stem: null, file: 'cpp', text: '', saved: '' },
  builds: {}, // stem -> {state: building|ok|failed, problems, at}
  play: { running: false, starting: false },
  consoleOpen: false,
  bottomTab: 'out',
  cache: {},
  api: [],
  tip: null,
  files: { q: store.get('filesQ', 'ui/credit'), list: null, prefix: null, open: null, want: null },
  // the engine's source: the file open, a search of all of it, the build
  eng: {
    files: null,
    file: store.get('engFile', null),
    opened: [],
    col: 1,
    q: '',
    hits: null,
    find: '',
    goto: store.get('engLine', 0),
    build: { state: 'idle', log: [], problems: [], secs: 0 },
    tab: 'out',
  },
};

// The tabs, named for what you do in each, in one bar: the original game
// (zoo.exe: find a function and read it), then ours (write it, test it, play
// it), then experiments. Ids stay as they were (the MCP, layouts)
const TABS = [
  ['explore', 'Explore', '', "Ask where anything is in the original game (selling animals, show tank, cHungerCheck), and see what our engine is missing"],
  ['code', 'Original Game', 'Original Engine', "The original zoo.exe: find a function on the left (names, the game's text, field:0x3d6), read its code in the middle, name it on the right"],
  ['engine', 'Write Code', 'ZT1 Engine', "Our engine's own source, and the game's files: write the feature here, then Build engine"],
  ['scene', 'Test Zoo', 'ZT1 Engine', 'Our engine running a test zoo: build in it, watch its code step by step, follow animals'],
  ['play', 'Play Game', 'ZT1 Engine', 'The whole game: our engine, or the original to compare'],
  ['mods', 'Experiments', '', "Small test scripts run in the test zoo, without changing the engine - e.g. checking one of the original's functions against ours"],
];
// An animal's stat -> the zoo.exe function behind it
const STAT_FN = { happiness: '0x4378df', hunger: '0x438a79', health: '0x4389aa', energy: '0x4384ba' };
const STAT_FIELD = { happiness: 'happiness', hunger: 'hunger', health: 'health', energy: 'energy' };
const CHART_COLORS = ['#5cc489', '#e8b45a', '#6aa8ef', '#c39bf0', '#f08a7a', '#9ad49c'];

// ------------------------------------------------------------------ util
function toast(text, ms = 2600) {
  const t = $('toast');
  t.innerHTML = text;
  t.classList.remove('hidden');
  clearTimeout(toast.timer);
  toast.timer = setTimeout(() => t.classList.add('hidden'), ms);
}

function ask(title, fields, okLabel = 'Save') {
  // A small form: fields [{key, label, value, area, options}] -> {key: value}
  // or null (options: [{value, label}] - a choice)
  return new Promise((resolve) => {
    const m = $('modal');
    m.innerHTML = `<div class="dialog"><div class="h">${esc(title)}</div>${fields
      .map(
        (f) =>
          `<div class="section"><div class="label">${esc(f.label)}</div>${
            f.options
              ? `<select class="field" data-k="${f.key}">${f.options.map((o) => `<option value="${esc(o.value)}"${o.value === f.value ? ' selected' : ''}>${esc(o.label)}</option>`).join('')}</select>`
              : f.area
              ? `<textarea class="field" data-k="${f.key}">${esc(f.value || '')}</textarea>`
              : `<input class="field" data-k="${f.key}" value="${esc(f.value || '')}" placeholder="${esc(f.hint || '')}">`
          }</div>`,
      )
      .join('')}<div class="btns"><div class="btn" data-x="no">Cancel</div><div class="btn primary" data-x="ok">${esc(okLabel)}</div></div></div>`;
    m.classList.remove('hidden');
    sendPlayRect();
    const first = m.querySelector('.field');
    if (first) first.focus();
    const done = (ok) => {
      const out = {};
      m.querySelectorAll('.field').forEach((el) => (out[el.dataset.k] = el.value.trim()));
      m.classList.add('hidden');
      m.innerHTML = '';
      sendPlayRect();
      resolve(ok ? out : null);
    };
    m.onclick = (e) => {
      if (e.target.dataset.x === 'ok') done(true);
      else if (e.target.dataset.x === 'no' || e.target === m) done(false);
    };
    m.onkeydown = (e) => {
      if (e.key === 'Enter' && e.target.tagName === 'INPUT') done(true);
      if (e.key === 'Escape') done(false);
    };
  });
}

async function ensureMe() {
  if (S.me) return true;
  const a = await ask('Who are you?', [{ key: 'name', label: "Your name or handle (it goes in the 'by' column of what you map)", hint: 'Batty251' }]);
  if (!a || !a.name) return false;
  await lab.setMe(a.name);
  S.me = a.name;
  return true;
}

// Ghidra, cached in the page too: path -> {json, cached} | {error}
async function G(path, force) {
  if (!force && S.cache[path] && !S.cache[path].pending) return S.cache[path];
  if (S.cache[path] && S.cache[path].pending) return S.cache[path].pending;
  const pending = lab.ghidra(path).then((r) => {
    S.cache[path] = r || { error: 'no answer' };
    return S.cache[path];
  });
  S.cache[path] = { pending };
  return pending;
}
const gNow = (path) => (S.cache[path] && !S.cache[path].pending ? S.cache[path] : null);
const hexKey = (a) => '0x' + parseInt(a, 16).toString(16).padStart(6, '0');
const plainHex = (a) => parseInt(a, 16).toString(16);

// ------------------------------------------------------------ knowledge
const kbFns = () => (S.kb ? S.kb.functions : {});

function fieldsAt(offHex) {
  // Mapped struct fields at an offset: [{struct, name, type, summary, by}]
  const out = [];
  if (!S.kb) return out;
  const off = parseInt(offHex, 16);
  for (const [sname, s] of Object.entries(S.kb.structs))
    for (const [k, f] of Object.entries(s.fields)) if (parseInt(k, 16) === off) out.push({ struct: sname, ...f });
  return out;
}

function animalFieldOffset(name) {
  const s = S.kb && S.kb.structs.ZTAnimal;
  if (!s) return null;
  for (const [k, f] of Object.entries(s.fields)) if (f.name === name) return k;
  return null;
}

function modsFor(addr) {
  const k = hexKey(addr);
  return S.mods.filter((m) => m.addresses.includes(k));
}

// Every function: the knowledge base's over Ghidra's list
function allFunctions() {
  const out = new Map();
  const kb = kbFns();
  const gl = S.ghidra.functions || [];
  for (const g of gl) {
    // (a switch's case blocks Ghidra made into functions: not real ones)
    if (/^(caseD_|switchD)/.test(g.name) || /^switchD/.test(g.class || '')) continue;
    const k = hexKey(g.address);
    const e = kb[k];
    out.set(k, {
      addr: k,
      name: e ? e.name : g.name,
      cls: e ? e.class || '' : g.class || '',
      size: g.size,
      kb: e || null,
      ghidraName: g.name,
    });
  }
  for (const [k, e] of Object.entries(kb))
    if (!out.has(k)) out.set(k, { addr: k, name: e.name, cls: e.class || '', size: null, kb: e, ghidraName: '' });
  const list = [...out.values()];
  for (const f of list) {
    const text = `${f.name} ${f.kb ? f.kb.summary || '' : ''}`;
    f.unnamed = !f.kb && isDefaultName(f.ghidraName);
    f.system = f.unnamed ? 'Other' : systemOf(text);
    f.hasMod = modsFor(f.addr).length > 0;
    f.inEngine = !!(S.refs[f.addr] || (f.kb && f.kb.engine && f.kb.engine.length));
    f.status = f.hasMod ? 'mod' : f.inEngine ? 'engine' : f.unnamed ? 'unnamed' : 'named';
  }
  list.sort((a, b) => (a.unnamed - b.unnamed) || (a.addr < b.addr ? -1 : 1));
  return list;
}

function fnInfo(addr) {
  const k = hexKey(addr);
  const e = kbFns()[k];
  const g = (S.ghidra.functions || []).find((x) => hexKey(x.address) === k);
  const name = e ? e.name : g ? g.name : 'FUN_' + plainHex(k).padStart(8, '0');
  return { addr: k, name, cls: e ? e.class || '' : g ? g.class || '' : '', kb: e, size: g ? g.size : null };
}

const displayName = (f) => (S.expert || isDefaultName(f.name) ? (f.cls ? f.cls + '::' : '') + f.name : friendlyName(f.name));

// ---------------------------------------------------------------- views
function setTab(t) {
  // (Functions and Code are one tab: the finder beside the code)
  const find = t === 'functions';
  if (find) t = 'code';
  if (S.tab === 'play' && t !== 'play' && S.play.running) lab.playArea(null);
  S.tab = t;
  store.set('tab', t);
  for (const [id] of TABS) $('v-' + id).classList.toggle('on', id === t);
  renderTop();
  if (t === 'scene') {
    renderScene();
    sizeCanvas();
    lab.engineSend({ cmd: 'frame' });
  } else if (t === 'code') {
    renderCode();
    renderFinder();
    if (find && $('fsearch')) $('fsearch').focus();
  }
  else if (t === 'mods') renderMods();
  else if (t === 'files') renderFiles();
  else if (t === 'engine') {
    renderEngine();
    refreshEngineFiles();
  }
  else if (t === 'play') renderPlay();
  else if (t === 'explore') exLoad().then(renderExplore);
}

function renderTop() {
  $('tabs').innerHTML = TABS.map(
    ([id, label, , what]) => `<div class="tab${S.tab === id ? ' on' : ''}" data-act="tab" data-arg="${id}" title="${esc(what)}">${label}</div>`,
  ).join('');
  // (the one big action of the tab: build ours, build and test an experiment,
  // run the picked experiment in the Test Zoo; none where there's nothing to run)
  // (Write Code's is Build engine - its only one: the Build panel has none)
  const runLabel = { engine: S.eng.build.state === 'building' ? 'Building…' : 'Build engine', mods: 'Build & test', scene: 'Run experiment' }[S.tab];
  $('run-btn').textContent = runLabel || '';
  $('run-btn').title = { engine: 'Save every file and build the engine (the Test Zoo restarts on it)', mods: 'Build this experiment and run it in the Test Zoo', scene: 'Run the picked experiment in the Test Zoo' }[S.tab] || '';
  $('run-btn').classList.toggle('hidden', !runLabel);
  const ep = $('engine-pill');
  const st = S.engine.status;
  ep.className = 'pill ' + (st === 'connected' ? 'ok' : st === 'starting' ? '' : 'bad');
  ep.querySelector('span').textContent =
    st === 'connected' ? 'Engine running' : st === 'starting' ? 'Engine starting' : 'Engine stopped';
  ep.title = S.engine.detail || '';
  ep.dataset.act = st === 'connected' ? '' : 'restartEngine';
  const gp = $('ghidra-pill');
  gp.className = 'pill ' + (S.ghidra.ok ? 'ok' : '');
  gp.querySelector('span').textContent = S.ghidra.ok ? 'Ghidra connected' : S.ghidra.building ? 'Ghidra: building' : S.ghidra.starting ? 'Ghidra starting' : 'Ghidra not connected';
  gp.title = S.ghidra.ok
    ? S.ghidra.program
    : S.ghidra.building
      ? (S.ghidra.said || 'The Lab is building a Ghidra project from your zoo.exe (once: a few minutes).')
      : S.ghidra.starting
        ? 'The Lab is starting Ghidra for itself (headless, zoo.exe read only): a minute or two. Code you have seen before still shows.'
        : (S.ghidra.said || 'No Ghidra found to start (re/private/ghidra.json says where it is). Or in Ghidra, with zoo.exe open: Tools > zt1-engine > Start Lab bridge. Until then, code you have seen before still shows.');
}

// (the Scene view: scene.js)

// ----------------------------------------------------------------- Code
function renderCode() {
  const f = fnInfo(S.sel.addr);
  const fp = '/function?addr=' + plainHex(f.addr);
  const cp = '/decompile?addr=' + plainHex(f.addr);
  const ap = '/disasm?addr=' + plainHex(f.addr);
  const meta = gNow(fp),
    dec = gNow(cp),
    asm = S.expert ? gNow(ap) : null;
  const again = () => S.tab === 'code' && renderCode();
  if (!meta) G(fp).then(again);
  if (!dec) G(cp).then(again);
  if (S.expert && !asm) G(ap).then(again);
  const mods = modsFor(f.addr);
  const refs = S.refs[f.addr] || [];
  CTX = contextStruct(f);
  const badges = [];
  if (mods.length) badges.push(`<div class="badge">checked by ${esc((mods[0].meta && mods[0].meta.name) || prettyStem(mods[0].stem))}</div>`);
  if (refs.length) badges.push(`<div class="badge blue">in our engine</div>`);
  const isRaw = !!(dec && dec.json && !(dec.json.c || '').trim());
  if (isRaw) badges.push(`<div class="badge amber" title="Ghidra hasn't made a function here: these are zoo.exe's raw instructions">no function yet</div>`);
  else if (!f.kb && isDefaultName(f.name)) badges.push(`<div class="badge amber">unnamed</div>`);
  else if (f.kb && f.kb.provisional) badges.push(`<div class="badge amber" title="We know what it does, not what the original programmers called it">provisional name</div>`);
  const plain = f.kb
    ? [f.kb.summary, f.kb.comment && f.kb.comment !== f.kb.summary ? f.kb.comment : ''].filter(Boolean).join(' ')
    : '';
  // the decompiled code
  let c = '';
  if (!dec) c = `<div class="muted" style="padding:0 14px">Asking Ghidra…</div>`;
  else if (dec.error)
    c = `<div class="muted" style="padding:0 14px;white-space:normal;line-height:1.5">Ghidra isn’t connected, and this function hasn’t been looked at before. In Ghidra: Tools › zt1-engine › Start Lab bridge.</div>`;
  else if (!(dec.json.c || '').trim()) c = rawCode(f.addr);
  else {
    const lines = (dec.json.c || '').replace(/\r/g, '').split('\n');
    while (lines.length && !lines[0].trim()) lines.shift();
    C_ELEM = cElemSizes(dec.json.c || '');
    c = lines.map((l, i) => `<div class="ln"><span class="no">${i + 1}</span><span>${highlightC(l, fieldLookup)}</span></div>`).join('');
  }
  let a = '';
  if (S.expert) {
    if (!asm) a = `<div class="muted" style="padding:0 14px">Asking Ghidra…</div>`;
    else if (asm.json && asm.json.lines)
      a = asm.json.lines
        .map(
          (x) =>
            `<div class="ln"><span class="k-addr">${esc(x.at.slice(2))}</span>&nbsp; ${highlightAsm(x.text)}${
              x.comment ? ` <span class="k-com">; ${esc(x.comment)}</span>` : ''
            }</div>`,
        )
        .join('');
  }
  // what it uses: mapped fields seen in the code
  const used = new Map();
  const elem = dec && dec.json && dec.json.c ? cElemSizes(dec.json.c) : {};
  if (dec && dec.json && dec.json.c)
    for (const l of dec.json.c.split('\n'))
      for (const m of [
        ...[...l.matchAll(/(param_1|this)\s*\+\s*(0x[0-9a-f]+)\)/gi)].map((x) => ({ off: x[2], writes: new RegExp(`\\+\\s*${x[2]}\\)\\s*=[^=]`, 'i').test(l) })),
        // (param_1[0xec]: an index - its byte offset by the pointer's size)
        ...[...l.matchAll(/\b(param_1|this)\s*\[\s*(0x[0-9a-f]+)\s*\]/gi)]
          .filter((x) => elem[x[1]])
          .map((x) => ({ off: '0x' + (parseInt(x[2], 16) * elem[x[1]]).toString(16), writes: new RegExp(`\\[\\s*${x[2]}\\s*\\]\\s*=[^=]`, 'i').test(l) })),
      ])
        for (const fd of fieldsAt(m.off).filter((x) => x.struct === CTX)) {
          const key = fd.struct + '.' + fd.name;
          const writes = m.writes;
          const u = used.get(key) || { fd, reads: false, writes: false };
          if (writes) u.writes = true;
          else u.reads = true;
          used.set(key, u);
        }
  const callers = meta && meta.json && meta.json.callers ? meta.json.callers : null;
  const callees = meta && meta.json && meta.json.callees ? meta.json.callees : null;
  const fnBox = (x) => {
    const info = fnInfo(x.address);
    const named = !(!info.kb && isDefaultName(x.name));
    return `<div class="box" data-act="selFn" data-arg="${hexKey(x.address)}"><div class="${named ? '' : 'mono'}">${esc(
      named ? displayName(info) : x.name,
    )}</div><div class="sub ${named ? '' : 'amber'}">${named ? esc(info.kb ? (info.kb.summary || '').slice(0, 60) : x.address) : 'unnamed · name it'}</div></div>`;
  };
  // (drawn again - the callers came, say - where it was scrolled to)
  const oldBox = document.querySelector('#v-code .codebox .c');
  const keep = oldBox && CODE_SHOWN === f.addr ? oldBox.scrollTop : null;
  CODE_SHOWN = f.addr;
  ensureOriginal();
  const sel = f;
  const dock = originalDock();
  dock.els.code.innerHTML = `<div class="page">
    <div class="pagehead"><span class="h">${esc(isRaw && !f.kb && isDefaultName(f.name) ? 'Code at ' + f.addr : displayName(f))}</span>${
      S.expert ? `<span class="mono muted" style="font-size:12px">${esc(f.cls ? f.cls + ' · ' : '')}${f.addr}</span>` : ''
    }${badges.join('')}<div class="spacer"></div><div class="btn" data-act="tryFn" data-arg="${f.addr}" title="Run zoo.exe's own code on what's picked in the Test Zoo, and see everything it read and wrote">Run the original</div><div class="btn" data-act="seeLive" data-arg="${f.addr}" title="Run the experiment that checks this function, in the Test Zoo">Watch in Test Zoo</div><div class="btn primary" data-act="modFor" data-arg="${f.addr}" title="${
      mods.length ? 'Open the experiment that checks this function' : 'A new experiment: a small script that checks this function against our engine in the Test Zoo'
    }">${mods.length ? 'Open its experiment' : 'New experiment'}</div></div>
    <div class="english"><div class="t">IN PLAIN ENGLISH</div><div class="d">${
      plain ? esc(plain) : '<span class="muted">Not described yet. Read the code, then give it a name and a note on the right.</span>'
    }</div></div>
    <div class="codebox"><div class="c code"><div class="head"><span>${dec && dec.json && !(dec.json.c || '').trim() ? 'RAW INSTRUCTIONS · X86' : 'DECOMPILED C'}${dec && dec.cached ? ' · saved copy' : ''}</span><span class="spacer"></span>${
      isRaw && RAW[f.addr] && RAW[f.addr].start
        ? `<div class="btn small primary" data-act="makeFn" data-arg="${RAW[f.addr].start}" title="Have Ghidra make a function from ${RAW[f.addr].start}, so its C can be decompiled (kept in re/made_functions.json)">Make it a function</div>`
        : ''
    }<span class="cfound" id="c-found"></span><input class="cfind" id="c-find" placeholder="Find in the code (Enter: next)" spellcheck="false" value="${esc(S.cfind || '')}"></div>${c}</div>${
      S.expert && !(dec && dec.json && !(dec.json.c || '').trim()) ? `<div class="a code asm"><div class="head">ASSEMBLY · X86</div>${a}</div>` : ''
    }</div></div>
    <div class="rightcol"><div class="section"><div class="label">Name it</div><input class="field" id="fname" value="${esc(isDefaultName(sel.name) ? '' : sel.name)}" placeholder="${esc(
      isDefaultName(sel.name) ? sel.name + ': what does it do?' : '',
    )}">${S.expert ? `<input class="field" id="fclass" value="${esc(sel.cls)}" placeholder="Class (e.g. ZTAnimal)">` : ''}<textarea class="field" id="fnote" placeholder="What it does, in plain words">${esc(
      sel.kb ? sel.kb.comment || sel.kb.summary || '' : '',
    )}</textarea><div style="display:flex;gap:8px;align-items:center"><div class="btn primary small" data-act="saveFn">Save</div><span class="muted" style="font-size:11.5px">${
      sel.kb ? `by ${esc(sel.kb.by || '?')}${sel.kb.provisional ? ' · provisional' : ''}` : `to knowledge.json${S.ghidra.ok ? ' + Ghidra' : ''}`
    }</span></div></div><div class="section"><div class="label">Called by</div>${
      !callers ? '<div class="muted">…</div>' : callers.length ? callers.slice(0, 12).map(fnBox).join('') : '<div class="muted">Nothing calls it directly: the game reaches it through a table (a virtual function).</div>'
    }</div>${
      S.expert && callees ? `<div class="section"><div class="label">Calls</div>${callees.length ? callees.slice(0, 12).map(fnBox).join('') : '<div class="muted">Nothing.</div>'}</div>` : ''
    }<div class="section"><div class="label">Fields it uses</div>${
      used.size
        ? [...used.values()]
            .map((u) => `<div class="kv" title="${esc(u.fd.summary || '')}"><span>${esc(S.expert ? u.fd.struct + '.' + u.fd.name : u.fd.name)}</span><span class="muted">${u.reads && u.writes ? 'reads + writes' : u.writes ? 'writes' : 'reads'}</span></div>`)
            .join('')
        : '<div class="muted">None of its offsets are mapped yet.</div>'
    }</div><div class="section"><div class="label">In our engine</div>${
      refs.length
        ? refs.map((r) => `<div class="box" data-act="openSrc" data-arg="${esc(r.where)}"><div class="mono" style="font-size:12px">${esc(r.where)}</div><div class="sub">${esc(r.comment.replace(/^[-\s]+/, '').slice(0, 120))}</div></div>`).join('')
        : '<div class="muted">Not implemented yet. When it is, cite it in a comment: // (zoo.exe ' + f.addr + ')</div>'
    }</div><div class="spacer"></div>${
      ''
    }</div>`;
  // (its details - name it, callers, fields, our engine - in their own panel)
  const rc = dock.els.code.querySelector('.rightcol');
  if (rc) dock.els.details.replaceChildren(rc);
  const box = document.querySelector('#v-code .codebox .c');
  // (raw instructions: the address asked about brought into view, once)
  const here = document.querySelector('#v-code .rawhere');
  if (here && RAW_SHOWN !== f.addr) {
    RAW_SHOWN = f.addr;
    if (box) box.scrollTop = here.offsetTop - box.clientHeight / 2;
  } else if (box && keep !== null) box.scrollTop = keep;
  codeFindWire();
  // (a function newly shown - its C just in - goes to the first match, once)
  if (S.cfind && CODE_FOUND !== f.addr && codeFind(false)) {
    CODE_FOUND = f.addr;
    S.cfindAt = 0;
    codeFind(true);
  }
}
let CODE_FOUND = null;

// Find in the code (the Code panel's box): every line holding the words
// lit, Enter goes to the next, Shift+Enter back. Kept as you move between
// functions, so the same word is found in each
function codeFind(go) {
  const box = document.querySelector('#v-code .codebox .c');
  if (!box) return 0;
  const q = (S.cfind || '').trim().toLowerCase();
  const lines = [...box.querySelectorAll('.ln')];
  const hits = [];
  for (const l of lines) {
    const on = !!q && l.textContent.toLowerCase().includes(q);
    l.classList.toggle('hit', on);
    l.classList.remove('hitnow');
    if (on) hits.push(l);
  }
  const n = hits.length;
  if (n) S.cfindAt = ((S.cfindAt || 0) % n + n) % n;
  const cur = n ? hits[S.cfindAt] : null;
  if (cur) cur.classList.add('hitnow');
  if (cur && go) box.scrollTop = cur.offsetTop - box.clientHeight / 3;
  const f = $('c-found');
  if (f) f.textContent = q ? (n ? `${S.cfindAt + 1} of ${n}` : 'none') : '';
  return n;
}
function codeFindWire() {
  const i = $('c-find');
  if (!i || i._wired) return;
  i._wired = true;
  i.oninput = () => {
    S.cfind = i.value;
    S.cfindAt = 0;
    codeFind(true);
  };
  i.onkeydown = (e) => {
    if (e.key === 'Enter') {
      S.cfindAt = (S.cfindAt || 0) + (e.shiftKey ? -1 : 1);
      codeFind(true);
    } else if (e.key === 'Escape') {
      i.value = S.cfind = '';
      codeFind(false);
    }
  };
  codeFind(false);
}
let RAW_SHOWN = null,
  CODE_SHOWN = null;

// Code Ghidra never made a function of (nothing calls it directly: the game
// reaches it through a table): zoo.exe's instructions there, from where that
// code starts - a guess, just after the code before it ends - with the text
// each points at (re/lab/zoobin.py)
const RAW = {}; // address -> its answer, null while asking
function rawCode(addr) {
  if (!(addr in RAW)) {
    RAW[addr] = null;
    lab.zooBin({ op: 'code', addr }).then((r) => {
      RAW[addr] = r;
      if (S.tab === 'code' && S.sel.addr === addr) renderCode();
    });
  }
  const r = RAW[addr];
  if (!r) return `<div class="muted" style="padding:0 14px">Ghidra hasn’t made a function here. Reading zoo.exe’s instructions… (the first time takes a minute)</div>`;
  if (r.error) return `<div class="muted" style="padding:0 14px;white-space:normal">Ghidra hasn’t made a function here, and the instructions couldn’t be read: ${esc(r.error)}</div>`;
  return (
    `<div class="muted rawnote">Ghidra hasn’t made a function here, so these are zoo.exe’s raw instructions (<b>Make it a function</b>, above, gets its C). The code starts at <span class="clickable mono" data-act="openCode" data-arg="${r.start}">${r.start}</span> (a guess: just after the code before it ends)${r.first && r.first !== r.start ? `, shown here from ${r.first}` : ''}; ${r.at} is marked.</div>` +
    r.lines
      .map(
        (x) =>
          `<div class="ln${x.at === r.at ? ' rawhere' : ''}"><span class="k-addr">${esc(x.at.slice(2))}</span>&nbsp; ${highlightAsm(x.text)}${x.str ? ` <span class="k-com">; "${esc(x.str)}"</span>` : ''}</div>`,
      )
      .join('')
  );
}

// field:0x3d6 in Functions: every instruction in zoo.exe touching that
// offset, by the function it's in (or the bare code), read or written - who
// uses a field the loader filled (cPreattack -> +0x3d6)
const FIELD = {}; // offset -> its answer, null while asking
let fnRanges = null;
function fnContaining(at) {
  const fns = S.ghidra.functions || [];
  if (!fnRanges || fnRanges.n !== fns.length)
    fnRanges = { n: fns.length, list: fns.filter((x) => x.size).map((x) => [parseInt(x.address, 16), x.size, x.address]).sort((a, b) => a[0] - b[0]) };
  const v = parseInt(at, 16),
    L = fnRanges.list;
  let lo = 0,
    hi = L.length - 1,
    best = -1;
  while (lo <= hi) {
    const mid = (lo + hi) >> 1;
    if (L[mid][0] <= v) (best = mid), (lo = mid + 1);
    else hi = mid - 1;
  }
  return best >= 0 && v < L[best][0] + L[best][1] ? L[best][2] : null;
}
// value:10282 in Functions: every instruction in zoo.exe using that number as
// itself (push 0x282a) - who shows a message, uses a constant
const VALUE = {}; // value -> its answer, null while asking
function valueMatches(q) {
  const value = q.slice(6).trim();
  const head = `<div class="trow head" style="position:static;grid-template-columns:1fr"><span>Instructions in zoo.exe using the number ${esc(value)}</span></div>`;
  if (!/^(0x[0-9a-f]+|-?\d+)$/i.test(value)) return head + `<div class="more muted" style="cursor:default">A number: value:10282 or value:0x282a</div>`;
  if (!(value in VALUE)) {
    VALUE[value] = null;
    lab.zooBin({ op: 'value', value }).then(async (r) => {
      if (r && r.hits && r.hits.length && S.ghidra.ok) {
        const o = await lab.ghidra('/owners?addrs=' + r.hits.map((h) => plainHex(h.at)).join(','));
        if (o && o.json && o.json.owners) r.owners = o.json.owners;
      }
      VALUE[value] = r;
      if (S.tab === 'code') renderFinder();
    });
  }
  const r = VALUE[value];
  if (!r) return head + `<div class="more muted" style="cursor:default">Reading zoo.exe’s code… (the first time takes a minute)</div>`;
  if (r.error) return head + `<div class="more muted" style="cursor:default">Couldn’t read zoo.exe: ${esc(r.error)}</div>`;
  if (!r.hits.length) return head + `<div class="more muted" style="cursor:default">Nothing uses it.</div>`;
  return (
    head +
    r.hits
      .map((h) => {
        const o = r.owners && r.owners[plainHex(h.at)];
        const fa = r.owners ? (o ? hexKey(o.address) : null) : fnContaining(h.at);
        const info = fa ? fnInfo(fa) : null;
        const who = info ? esc(isDefaultName(info.name) ? info.name : displayName(info)) : `code at ${h.at}`;
        return `<div class="trow" style="grid-template-columns:minmax(0,0.9fr) minmax(0,1.6fr)" data-act="openCode" data-arg="${fa || h.at}" title="open ${fa ? 'its function' : 'the code there'}"><span class="${info ? '' : 'mono muted'}">${who}</span><span class="mono" style="font-size:12px">${esc(h.at.slice(2))}&nbsp; ${highlightAsm(h.text)}</span></div>`;
      })
      .join('')
  );
}

function fieldMatches(q) {
  // field:0x3b0 [byte|word|dword|address]
  const [off0, size = ''] = q.slice(6).trim().toLowerCase().split(/\s+/);
  const off = off0 || '';
  const key = off + ' ' + size;
  const head = `<div class="trow head" style="position:static;grid-template-columns:1fr"><span>Instructions in zoo.exe touching “+ ${esc(off)}”${size ? ` (${esc(size)})` : ''}</span></div>`;
  if (!/^0x[0-9a-f]+$/.test(off)) return head + `<div class="more muted" style="cursor:default">An offset, in hex: field:0x3d6 (add byte, word or dword to keep to that size)</div>`;
  if (!(key in FIELD)) {
    FIELD[key] = null;
    lab.zooBin({ op: 'field', off, size }).then(async (r) => {
      // (which function each is in, by Ghidra: a function's code can be in pieces)
      if (r && r.hits && r.hits.length && S.ghidra.ok) {
        const o = await lab.ghidra('/owners?addrs=' + r.hits.map((h) => plainHex(h.at)).join(','));
        if (o && o.json && o.json.owners) r.owners = o.json.owners;
      }
      FIELD[key] = r;
      if (S.tab === 'code') renderFinder();
    });
  }
  const r = FIELD[key];
  if (!r) return head + `<div class="more muted" style="cursor:default">Reading zoo.exe’s code… (the first time takes a minute)</div>`;
  if (r.error) return head + `<div class="more muted" style="cursor:default">Couldn’t read zoo.exe: ${esc(r.error)}</div>`;
  if (!r.hits.length) return head + `<div class="more muted" style="cursor:default">Nothing touches it.</div>`;
  // (what's known at this offset, and a form to name it)
  const known = fieldsAt(off);
  const structs = Object.keys((S.kb && S.kb.structs) || {});
  const form = `<div class="fieldname">${
    known.length ? `<div class="muted">Known: ${known.map((k) => `<b>${esc(k.struct)}.${esc(k.name)}</b>`).join(', ')}</div>` : ''
  }<div class="fnrow"><span class="muted">Name +${esc(off)} in</span><select class="field" id="ff-struct">${structs
    .map((n) => `<option${n === 'ZTAnimal' ? ' selected' : ''}>${esc(n)}</option>`)
    .join('')}</select><input class="field" id="ff-name" placeholder="name (e.g. rampaging)"><select class="field" id="ff-type">${['bool', 'byte', 'int', 'short', 'float', 'pointer']
    .map((t) => `<option${t === (size === 'byte' ? 'bool' : 'int') ? ' selected' : ''}>${t}</option>`)
    .join('')}</select></div><div class="fnrow"><input class="field" id="ff-note" placeholder="What it is, in plain words" style="flex:1"><div class="btn small primary" data-act="saveField" data-arg="${esc(off)}">Save</div></div></div>`;
  const ownerOf = (h) => {
    const o = r.owners && r.owners[plainHex(h.at)];
    if (r.owners) return o ? hexKey(o.address) : null;
    return fnContaining(h.at);
  };
  return (
    head +
    form +
    r.hits
      .map((h) => {
        const fa = ownerOf(h);
        const info = fa ? fnInfo(fa) : null;
        const who = info ? esc(isDefaultName(info.name) ? info.name : displayName(info)) : `code at ${h.at}`;
        return `<div class="trow" style="grid-template-columns:minmax(0,0.9fr) minmax(0,1.4fr) 70px" data-act="openCode" data-arg="${fa || h.at}" title="open ${fa ? 'its function' : 'the code there'}"><span class="${info ? '' : 'mono muted'}">${who}</span><span class="mono" style="font-size:12px">${esc(h.at.slice(2))}&nbsp; ${highlightAsm(h.text)}</span><span class="${h.kind === 'write' ? 'amber' : 'muted'}">${{ write: 'writes', read: 'reads', call: 'calls through', address: 'takes its address' }[h.kind] || h.kind}${h.size && h.kind !== 'address' ? ' · ' + h.size : ''}</span></div>`;
      })
      .join('')
  );
}

// ------------------------------------------------------- the finder
// The left of Original Game (it was the Functions tab): search zoo.exe's
// functions by name, address and notes, its text (and the functions using
// each) or field:0x3d6; filter by system; click one to read it beside
function ensureOriginal() {
  const d = originalDock();
  if (d.els.finder.querySelector('#fsearch')) return;
  d.els.finder.innerHTML = `<div class="btn small ghost" data-act="kbTools" style="margin-bottom:6px" title="Fill the knowledge base from what zoo.exe says about itself: its config loaders' keys, its classes' tables of functions, its own text">Grow the database</div><input class="search" id="fsearch" placeholder="Search names, notes, the game's text… field:0x3d6 or value:10282" spellcheck="false"><div id="fprog" class="fprog"></div><div class="fchips" id="fchips"></div><div class="flist" id="flist"></div>`;
  const s = $('fsearch');
  s.value = S.fview.q;
  s.oninput = () => {
    S.fview.q = s.value;
    S.fview.limit = 300;
    S.fview.typed = s.value;
    renderFinder();
    // (the game's text too, from Ghidra, once typing stops)
    clearTimeout(textSearchTimer);
    const q = s.value.trim();
    if (q.length >= 3 && S.ghidra.ok && !/^(field|value):/i.test(q))
      textSearchTimer = setTimeout(() => G(stringsQuery(q)).then(() => S.tab === 'code' && S.fview.q.trim() === q && renderFinder()), 350);
  };
}
function renderFinder() {
  ensureOriginal();
  const all = allFunctions();
  const named = all.filter((f) => !f.unnamed).length;
  const counts = { All: all.length, Unnamed: all.filter((f) => f.unnamed).length, 'Has an experiment': all.filter((f) => f.hasMod).length, 'In our engine': all.filter((f) => f.inEngine).length };
  for (const [n] of SYSTEMS) counts[n] = all.filter((f) => f.system === n).length;
  const chips = ['All', ...SYSTEMS.map(([n]) => n), 'Unnamed', 'Has an experiment', 'In our engine'].filter((n) => counts[n] || n === 'All');
  const q = S.fview.q.toLowerCase();
  const rows = all.filter((f) => {
    const flt = S.fview.filter;
    if (flt === 'Unnamed' && !f.unnamed) return false;
    if (flt === 'Has an experiment' && !f.hasMod) return false;
    if (flt === 'In our engine' && !f.inEngine) return false;
    if (SYSTEMS.some(([n]) => n === flt) && f.system !== flt) return false;
    if (q && !`${f.name} ${f.cls} ${f.addr} ${f.kb ? f.kb.summary || '' : ''}`.toLowerCase().includes(q)) return false;
    return true;
  });
  const statusText = { mod: '● has an experiment', engine: 'in our engine', named: 'named', unnamed: 'unnamed' };
  const statusCls = { mod: 'green', engine: 'blue', named: '', unnamed: 'amber' };
  const shown = rows.slice(0, S.fview.limit);
  const box = $('fsearch');
  // (set from elsewhere - the MCP, a system chip: shown in the box; what's
  // being typed is left alone)
  if (box && box.value !== S.fview.q && S.fview.q !== S.fview.typed) box.value = S.fview.q;
  setHtml(
    $('fprog'),
    `<div class="progress"><div style="width:${all.length ? (named / all.length) * 100 : 0}%"></div></div><span>${named.toLocaleString()} of ${all.length.toLocaleString()} named</span>${
      S.ghidra.functions ? '' : '<span class="badge amber" title="Start the Lab bridge in Ghidra to list all of zoo.exe">Ghidra not connected</span>'
    }`,
  );
  setHtml(
    $('fchips'),
    chips
      .map(
        (n) =>
          `<div class="chip${S.fview.filter === n ? ' on' : ''}${n === 'Unnamed' ? ' amber' : n === 'Has an experiment' ? ' green' : ''}" data-act="ffilter" data-arg="${esc(n)}">${esc(n)} ${counts[n].toLocaleString()}</div>`,
      )
      .join(''),
  );
  const list = $('flist');
  const keep = list.scrollTop;
  setHtml(
    list,
    shown
      .map(
        (f) =>
          `<div class="frow${f.addr === S.sel.addr ? ' sel' : ''}" data-act="selFnRow" data-arg="${f.addr}"><div class="${f.unnamed ? 'mono dim' : ''}">${esc(
            f.unnamed ? f.name : displayName(f),
          )}</div><div class="sub">${S.expert || f.unnamed ? `<span class="mono">${f.addr}</span>${f.unnamed ? '' : ' · '}` : ''}${f.unnamed ? '' : esc(f.system)} · <span class="${statusCls[f.status]}">${statusText[f.status]}</span></div></div>`,
      )
      .join('') +
      (rows.length > shown.length ? `<div class="more" data-act="fmore">Show more (${(rows.length - shown.length).toLocaleString()} left)</div>` : '') +
      (rows.length || /^(field|value):/.test(q) ? '' : '<div class="more muted" style="cursor:default">No function names match.</div>') +
      (/^field:/.test(q) ? fieldMatches(q) : /^value:/.test(q) ? valueMatches(q) : textMatches(q)),
  );
  list.scrollTop = keep;
}
// (the old name: what the MCP and older code call)
const renderFunctions = () => S.tab === 'code' && renderFinder();

// The game's text matching the search, and the functions that use it:
// how code nobody has named yet is found ("credits.cfg", "delay")
let textSearchTimer = null;
// (one spelling for asking and for finding the answer: any case is the same
// search to Ghidra)
function stringsQuery(q) {
  return '/strings?q=' + encodeURIComponent((q || '').trim().toLowerCase());
}
function textMatches(q) {
  q = (q || '').trim();
  if (q.length < 3) return '';
  const r = gNow(stringsQuery(q));
  const head = `<div class="trow head" style="position:static;grid-template-columns:1fr"><span>Text in zoo.exe containing “${esc(q)}”</span></div>`;
  if (!S.ghidra.ok && !r) return head + `<div class="more muted" style="cursor:default">Connect Ghidra to search the game's text.</div>`;
  if (!r) return head + `<div class="more muted" style="cursor:default">Searching…</div>`;
  if (r.error || !r.json.strings) return head + `<div class="more muted" style="cursor:default">Ghidra couldn't search (start the Lab bridge again: it may be an older one).</div>`;
  if (!r.json.strings.length) return head + `<div class="more muted" style="cursor:default">No text matches.</div>`;
  return (
    head +
    r.json.strings
      .map(
        (s) =>
          `<div class="trow" style="grid-template-columns:minmax(0,1fr) minmax(0,1.4fr);cursor:default"><span class="mono amber" title="${esc(s.text)}">"${esc(s.text)}"</span><span>${
            s.users.length
              ? [...new Map(s.users.map((u) => [u.address, u])).values()]
                  .map((u) => {
                    const info = fnInfo(u.address);
                    const named = !(!info.kb && isDefaultName(u.name));
                    return `<span class="chip" style="display:inline-block;margin:2px 6px 2px 0" data-act="selFnRow" data-arg="${u.address}" title="used at ${u.at}">${esc(
                      named ? displayName(info) : u.name,
                    )}</span>`;
                  })
                  .join('')
              : `<span class="muted">not used by any function (yet found)</span> <span class="chip" style="display:inline-block;margin:2px 0 2px 8px" data-act="xrefs" data-arg="${esc(s.address)}" title="Every place in zoo.exe that points at this text - code Ghidra hasn't made a function of opens as raw instructions">Where used</span>`
          }</span></div>`,
      )
      .join('')
  );
}

// (by: who it's saved as; else you, the Lab's user)
async function saveFn(by) {
  const sel = fnInfo(S.sel.addr);
  const name = ($('fname').value || '').trim();
  const cls = $('fclass') ? $('fclass').value.trim() : sel.cls;
  const note = ($('fnote').value || '').trim();
  if (!name) return toast('Give it a name first.');
  if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return toast('A name is letters, digits and _ (like checkHunger).');
  if (!by && !(await ensureMe())) return;
  const who = by || S.me;
  const args = ['func', sel.addr, name];
  if (note) args.push(note);
  if (cls) args.push('--class', cls);
  args.push('--confidence', 'likely');
  if (by) args.push('--by', by);
  const r = await lab.kbRun(args);
  if (!r.ok) return toast('Couldn’t save: ' + esc(r.out.slice(0, 200)), 5000);
  let g = '';
  if (S.ghidra.ok) {
    const rr = await lab.ghidra(`/rename?addr=${plainHex(sel.addr)}&name=${encodeURIComponent(name)}&class=${encodeURIComponent(cls)}&by=${encodeURIComponent(who)}`);
    g = rr && rr.json && rr.json.ok ? ' and in Ghidra' : ' (Ghidra didn’t take it)';
    delete S.cache['/functions'];
    loadGhidraFunctions(true);
  }
  await loadKb();
  toast(`Saved <b>${esc(name)}</b> to the knowledge base${g}.`);
  if (S.tab === 'code') {
    renderCode();
    renderFinder();
  }
}

// ----------------------------------------------------------------- Mods
const TEMPLATE = (f) => `#include "lab_api.h"

// zoo.exe ${f.addr} ${f.name}${f.summary ? ': ' + f.summary : ''}
// What does zoo.exe do here? Write your reading of it below, and check it
// against our engine, step by step.

LAB_EXPORT const char *lab_about() { return "${(f.title || f.name).replace(/"/g, "'")}"; }

static int animal = -1;

LAB_EXPORT void lab_start(Lab *lab) {
  lab->exhibit(5, 5, "chainlnk");
  animal = lab->animal("zebra", false);
  lab->focusAnimal(animal);
  lab->log("zebra %d in the lab exhibit", animal);
}

LAB_EXPORT bool lab_step(Lab *lab, int step) {
  lab->runEngine(1.0f); // one second: one tick of the animal's checks
  double h = lab->get(animal, "happiness");
  lab->watch("happiness", h, "ZTAnimal+0x2a8");
  lab->plot("happiness", h);
  lab->expect("happiness stays -100..100", h >= -100 && h <= 100);
  return step < 120;
}
`;

async function newMod(addr, fromFile) {
  const f = addr ? fnInfo(addr) : null;
  const a = await ask(
    'New mod',
    [
      { key: 'title', label: 'What is it called?', value: f ? friendlyName(f.name) : fromFile ? prettyStem(fromFile.split('/').pop().replace(/\.[^.]+$/, '')) + ' check' : '', hint: 'Hunger rebalance' },
      { key: 'addr', label: 'The zoo.exe function it’s about (optional)', value: f ? f.addr : '', hint: '0x438a79' },
    ],
    'Create',
  );
  if (!a || !a.title) return;
  const stem = a.title.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '') || 'mod';
  if (await lab.modsExists(stem + '.cpp')) return toast(`There’s already an experiment called ${esc(stem)}.`);
  const fi = a.addr && /^0x[0-9a-f]+$/i.test(a.addr) ? fnInfo(a.addr) : { addr: '0x000000', name: 'unknown', kb: null };
  let code = TEMPLATE({ addr: fi.addr, name: fi.name, summary: fi.kb ? fi.kb.summary : '', title: a.title });
  // (made from one of the game's files: it says which, and for a species'
  // .ai, the species it adopts)
  if (fromFile) {
    code = `// Made from the game's ${fromFile}\n` + code;
    const sp = /^animals\/([a-z0-9_]+)\.ai$/.exec(fromFile);
    if (sp) code = code.replace(/"lion"/g, `"${sp[1]}"`);
  }
  await lab.modsWrite(stem + '.cpp', code);
  await lab.modsWrite(stem + '.json', JSON.stringify({ name: a.title, about: fi.kb ? fi.kb.summary || '' : '' }, null, 2) + '\n');
  await loadMods();
  pickMod(stem);
  openEditor(stem);
  setTab('mods');
}

async function openEditor(stem, file = 'cpp') {
  if (S.editor.stem && S.editor.text !== S.editor.saved) await saveEditor();
  const text = (await lab.modsRead(stem + (file === 'cpp' ? '.cpp' : '.json'))) ?? (file === 'json' ? '{\n  "name": "' + prettyStem(stem) + '"\n}\n' : '');
  S.editor = { stem, file, text, saved: text };
  renderMods();
}

async function saveEditor() {
  const e = S.editor;
  if (!e.stem || e.text === e.saved) return;
  await lab.modsWrite(e.stem + (e.file === 'cpp' ? '.cpp' : '.json'), e.text);
  e.saved = e.text;
  await loadMods();
}

function renderEditorLayers() {
  const e = S.editor;
  const lines = e.text.split('\n');
  const b = S.builds[e.stem];
  const problems = e.file === 'cpp' && b && b.problems ? b.problems : [];
  const errLines = new Set(problems.map((p) => p.line));
  const lh = 12.5 * 1.85;
  $('ed-pre').innerHTML = (e.file === 'cpp' ? lines.map((l) => highlightC(l)) : lines.map((l) => esc(l))).join('\n') + '\n';
  $('ed-gutter').innerHTML = lines.map((_, i) => `<div class="${errLines.has(i + 1) ? 'err' : ''}">${i + 1}</div>`).join('');
  $('ed-marks').innerHTML = problems
    .map((p) => {
      const top = 12 + (p.line - 1) * lh;
      const fix = suggestFix(p, lines[p.line - 1] || '');
      return `<div class="errline" style="top:${top}px"></div><div class="bubble" style="top:${top + lh}px">${esc(p.message)}${
        fix ? ` Did you mean <b style="color:#fff">${esc(fix.to)}</b>? <span class="green clickable" style="font-weight:600" data-act="applyFix" data-arg="${p.line}|${esc(fix.from)}|${esc(fix.to)}">Fix</span>` : ''
      }</div>`;
    })
    .join('');
  const ta = $('ed-text');
  ta.style.height = Math.max(lines.length * lh + 40, $('editor').clientHeight) + 'px';
}

function suggestFix(p, line) {
  // "'happines': is not a member of 'Lab'" / "'happines': identifier not found"
  const m = p.message.match(/'([A-Za-z_]\w*)'\s*:\s*(is not a member of 'Lab'|identifier not found|undeclared identifier)/);
  if (!m) return null;
  const to = closest(m[1], S.api.map((a) => a.name));
  return to && to !== m[1] && line.includes(m[1]) ? { from: m[1], to } : null;
}

function renderBuildPanel() {
  const out = $('build-body');
  if (!out) return;
  const e = S.editor;
  const b = S.builds[e.stem];
  const st = S.engine.state;
  const probs = b && b.problems ? b.problems : [];
  $('bt-probs').textContent = `Problems ${probs.length}`;
  $('bt-probs').className = S.bottomTab === 'probs' ? 'on' : '';
  $('bt-out').className = S.bottomTab === 'out' ? 'on' : '';
  if (S.bottomTab === 'probs') {
    out.innerHTML = probs.length
      ? probs
          .map((p) => `<div class="step clickable" data-act="gotoLine" data-arg="${p.line}"><span class="red">✕</span><span>Line ${p.line}: ${esc(p.message)} <span class="dim">${esc(p.code)}</span></span></div>`)
          .join('')
      : '<div class="muted">No problems.</div>';
    return;
  }
  const rows = [];
  rows.push(`<div class="step"><span class="${e.text === e.saved ? 'green' : 'muted'}">${e.text === e.saved ? '✓' : '○'}</span>${e.text === e.saved ? 'Saved' : 'Not saved yet (Ctrl+S, or it saves when you build)'}</div>`);
  if (!b) rows.push(`<div class="step"><span class="muted">○</span>Not built yet. Press <b>Build &amp; test</b>.</div>`);
  else if (b.state === 'building') rows.push(`<div class="step"><span class="muted">…</span>Building ${esc(e.stem)}.cpp (the first build of a session sets up the compiler: about 30 s)</div>`);
  else if (b.state === 'failed')
    rows.push(`<div class="step"><span class="red">✕</span>Build failed: ${probs.length} problem${probs.length === 1 ? '' : 's'}${probs.length ? ' at line ' + probs.map((p) => p.line).join(', ') : ''}</div><div class="step muted"><span>○</span>Run in the lab zoo</div>`);
  else {
    rows.push(`<div class="step"><span class="green">✓</span>Built</div>`);
    if (st && st.script === e.stem && st.loaded)
      rows.push(
        `<div class="step"><span class="green">✓</span>${st.live ? `Running in the lab zoo · step ${st.step}` : `Finished after ${st.step} steps`} · ${
          st.checks.filter((c) => c.ok).length
        }/${st.checks.length} checks pass <span class="blue clickable" data-act="tab" data-arg="scene">Watch it</span></div>`,
      );
  }
  out.innerHTML = rows.join('');
}

function renderMods() {
  const e = S.editor;
  if (!e.stem && S.mods.length) {
    openEditor(S.mod && S.mods.some((m) => m.stem === S.mod) ? S.mod : S.mods[0].stem);
    return;
  }
  const will = e.stem ? modWill(e.file === 'cpp' ? e.text : '', kbFns()) : [];
  if (!S.modsLeft) S.modsLeft = store.get('modsLeft', 'mods');
  // (the HD art panel became an experiment of its own: re/lab/scripts/hd_art.cpp;
  // Your mods - making mods and sharing them - is modkit.js)
  if (S.modsLeft === 'gamemods') return renderModKit();
  if (S.modsLeft !== 'game') S.modsLeft = 'mods';
  const g = S.modsLeft === 'game' && S.modsGame;
  $('v-mods').innerHTML = `<div class="modlist" style="width:${S.modsLeft === 'game' ? 360 : 240}px">${modkitTabs()}${
    S.modsLeft === 'game'
      ? '<div class="gfslot" id="mods-gf"></div>'
      : `${S.mods
          .map((m) => {
            const [st, cls] = modStatus(m.stem);
            return `<div class="item${m.stem === e.stem ? ' sel' : ''}" data-act="editMod" data-arg="${esc(m.stem)}"><div class="name">${esc(
              (m.meta && m.meta.name) || prettyStem(m.stem),
            )}<span class="dim clickable modcopy" data-act="copyMod" data-arg="${esc(m.stem)}" title="A new mod, starting as a copy of this one">⧉</span></div><div class="sub ${cls}">● ${esc(st)}</div></div>`;
          })
          .join('')}<div class="item muted" data-act="newMod">+ New experiment</div>`
  }</div>
    <div style="flex:1;display:flex;flex-direction:column;min-width:0">${
      g
        ? `<div class="filetabs"><div class="on mono">${esc(g.name)} <span class="dim">(the game's, read only)</span></div><div class="spacer"></div><span class="muted" id="mods-refs"></span><div class="btn small primary" data-act="newModFromFile" data-arg="${esc(
            g.name,
          )}" title="A new experiment about this file">New experiment from this file</div><div class="btn ghost small" data-act="modsGameClose">Back to ${esc(e.stem || 'the experiment')}</div></div><div class="code gftext">${gfTextHtml(g.text)}</div>`
        : e.stem
        ? `<div class="filetabs"><div class="${e.file === 'cpp' ? 'on' : ''}" data-act="edFile" data-arg="cpp">${esc(e.stem)}.cpp${
            e.file === 'cpp' && e.text !== e.saved ? ' •' : ''
          }</div><div class="${e.file === 'json' ? 'on' : ''}" data-act="edFile" data-arg="json">mod.json${
            e.file === 'json' && e.text !== e.saved ? ' •' : ''
          }</div><div class="spacer"></div><div class="btn ghost small" data-act="openModFile">Open in your editor</div></div>
      <div class="editor" id="editor"><div class="inner"><div class="gutter" id="ed-gutter"></div><div id="ed-marks"></div><pre id="ed-pre"></pre><textarea id="ed-text" spellcheck="false"></textarea></div></div>
      <div class="buildout"><div class="tabsrow"><div id="bt-out" data-act="bottomTab" data-arg="out">Build output</div><div id="bt-probs" data-act="bottomTab" data-arg="probs">Problems 0</div></div><div class="body" id="build-body"></div></div>`
        : `<div class="stagemsg"><b>No experiments yet</b><span>An experiment is a small C++ script that sets up the Test Zoo and checks one rule of the original game against our engine, step by step - without changing the engine.</span><div class="btn primary" data-act="newMod">New experiment</div></div>`
    }</div>
    <div class="rightcol"><div class="section"><div class="label">This experiment will</div>${
      will.length ? will.map((w) => `<div class="willbox">${w}</div>`).join('') : '<div class="muted">…</div>'
    }</div><div class="section"><div class="label">Lab API</div><div class="muted" style="font-size:12px">Click one to put it in at the cursor.</div>${S.api
      .map((a) => `<div class="apibox" data-act="insertApi" data-arg="${esc(a.sig)}">${esc(a.sig)}<div class="d">${esc(a.about)}</div></div>`)
      .join('')}</div></div>`;
  // (the game's files on the left; a game file open in the middle)
  if (S.modsLeft === 'game') gfMount($('mods-gf'), 'mods');
  if (g) gfRefsInto($('mods-refs'), g.name);
  if (!e.stem || !$('ed-text')) return;
  const ta = $('ed-text');
  ta.value = e.text;
  renderEditorLayers();
  renderBuildPanel();
  ta.oninput = () => {
    e.text = ta.value;
    renderEditorLayers();
    const tabs = document.querySelectorAll('.filetabs div');
    if (tabs[e.file === 'cpp' ? 0 : 1]) tabs[e.file === 'cpp' ? 0 : 1].textContent = (e.file === 'cpp' ? e.stem + '.cpp' : 'mod.json') + (e.text !== e.saved ? ' •' : '');
  };
  ta.onkeydown = (ev) => {
    if (ev.key === 'Tab') {
      ev.preventDefault();
      const s = ta.selectionStart;
      ta.setRangeText('  ', s, ta.selectionEnd, 'end');
      ta.oninput();
    }
    if ((ev.ctrlKey || ev.metaKey) && ev.key.toLowerCase() === 's') {
      ev.preventDefault();
      saveEditor().then(() => {
        toast('Saved.');
        renderMods();
      });
    }
  };
}

// --------------------------------------------------------- Engine code
// Our engine's source, changed and built here, in Monaco (VS Code's editor)
// with clangd behind it: search the whole engine, open a file at a line,
// edit (autocomplete, hover, F12 to the definition, Shift+F12 for uses,
// Ctrl+F / Ctrl+H to find and replace, errors as you type), save, Build
// engine (the lab zoo restarts on the new one; Play's game when it's
// started again)
async function openEngineFile(rel, line, col) {
  const E = S.eng;
  if (!CodeEditor.isOpen(rel)) {
    const r = /^src\//.test(rel) || !/^[A-Za-z]:/.test(rel) ? await lab.srcReadAll(rel) : await lab.readAny(rel);
    if (!r) return toast('Couldn’t read ' + esc(rel));
    await CodeEditor.open(rel, r.text, { crlf: r.crlf, readOnly: !/^(src|vendor)\//.test(rel), abs: /^[A-Za-z]:/.test(rel) });
  }
  E.file = rel;
  E.goto = line || 1;
  E.col = col || 1;
  if (/^src\//.test(rel)) {
    store.set('engFile', rel);
    store.set('engLine', E.goto);
  }
  if (S.tab !== 'engine') setTab('engine');
  else renderEngine();
}

async function saveEngineFile(rel) {
  rel = rel || S.eng.file;
  if (!rel || !CodeEditor.isDirty(rel) || CodeEditor.readOnlyOf(rel)) return true;
  // (changed on disk since it was opened here: asked before it's replaced)
  const disk = await lab.srcReadAll(rel);
  if (disk && CodeEditor.diskOf(rel) !== null && disk.text !== CodeEditor.diskOf(rel)) {
    const go = await ask(`${rel} has changed on disk since you opened it`, [], 'Save mine over it');
    if (!go) return false;
  }
  const text = CodeEditor.textOf(rel);
  const ok = await lab.srcWrite(rel, text, CodeEditor.crlfOf(rel));
  if (ok) {
    const now = await lab.srcReadAll(rel);
    CodeEditor.markSaved(rel, now ? now.text : undefined);
  }
  else toast('Couldn’t save ' + esc(rel));
  return ok;
}

// The engine's files open here, against the disk: changed there and not
// here, taken as they are now; changed in both, said
async function refreshEngineFiles() {
  for (const rel of S.eng.opened) {
    if (!/^(src|vendor)\//.test(rel) || !CodeEditor.isOpen(rel)) continue;
    const r = await lab.srcReadAll(rel);
    if (!r || r.text === CodeEditor.diskOf(rel)) continue;
    if (CodeEditor.reload(rel, r.text)) toast(`${esc(rel)} changed on disk: reloaded.`, 2500);
    else toast(`${esc(rel)} changed on disk, and you have unsaved changes to it here. Saving asks first.`, 5000);
  }
  if (S.tab === 'engine') refreshEngineChrome();
}
window.addEventListener('focus', () => S.tab === 'engine' && refreshEngineFiles());

async function saveAllEngineFiles() {
  for (const f of S.eng.opened) await saveEngineFile(f);
}

function engineTabs() {
  const E = S.eng;
  return E.opened
    .map((f) => {
      const name = f.replace(/^game:/, '').split(/[\\/]/).pop();
      const dirty = CodeEditor.isDirty(f);
      return `<div class="${f === E.file ? 'on' : ''} mono" data-act="engTab" data-arg="${esc(f)}" title="${esc(f)}">${esc(name)}${dirty ? ' •' : ''}${
        CodeEditor.readOnlyOf(f) ? ' <span class="dim">(read only)</span>' : ''
      }<span class="dim clickable" data-act="engClose" data-arg="${esc(f)}" style="margin-left:8px">✕</span></div>`;
    })
    .join('');
}

function refreshEngineChrome() {
  const t = $('ee-tabs');
  if (t) t.innerHTML = engineTabs();
  const st = $('ee-lsp');
  if (st) {
    const s = CodeEditor.lsp.status;
    st.textContent = s === 'ready' ? 'C++ intelligence ready' : s === 'off' ? '' : s === 'missing' ? 'clangd missing' : `clangd ${s}`;
    st.className = s === 'ready' ? 'green' : 'muted';
  }
}

function renderEngineBuild() {
  // (the top bar's Build engine says Building… while it builds)
  if (S.tab === 'engine' && renderEngineBuild.was !== S.eng.build.state) (renderEngineBuild.was = S.eng.build.state), renderTop();
  const E = S.eng;
  const body = $('eb-body');
  if (!body) return;
  const b = E.build;
  $('eb-out').className = E.tab === 'out' ? 'on' : '';
  $('eb-probs').className = E.tab === 'probs' ? 'on' : '';
  $('eb-calls').className = E.tab === 'calls' ? 'on' : '';
  $('eb-sync').className = E.tab === 'sync' ? 'on' : '';
  $('eb-run').className = E.tab === 'run' ? 'on' : '';
  if (E.tab === 'run') {
    body.innerHTML = runHtml();
    return;
  }
  if (E.tab === 'sync') {
    body.innerHTML = syncHtml();
    if (!SY.st && !SY.busy) syncRefresh();
    return;
  }
  $('eb-probs').textContent = `Problems ${b.problems.length}`;
  if (E.tab === 'calls') {
    body.innerHTML = callsHtml();
    return;
  }
  if (E.tab === 'probs') {
    body.innerHTML = b.problems.length
      ? b.problems
          .map((p) => `<div class="step clickable" data-act="engOpen" data-arg="${esc(p.file)}:${p.line}"><span class="red">✕</span><span>${esc(p.file)}:${p.line}: ${esc(p.message)} <span class="dim">${esc(p.code)}</span></span></div>`)
          .join('')
      : '<div class="muted">No problems.</div>';
    return;
  }
  const rel = b.kind === 'release' ? b.release || {} : null;
  const head = rel
    ? b.state === 'building'
      ? `<div class="step"><span class="muted">…</span>Making a release: x64 Release build in build-x64/, then the zip…</div>`
      : b.state === 'ok'
        ? `<div class="step"><span class="green">✓</span><span>Release made in ${b.secs} s: <span class="mono">${esc(rel.zip)}</span> (${esc(rel.what || '')})<br><span class="dim mono" style="font-size:11px">sha256 ${esc(rel.sha)}</span>${
            rel.leftOut && rel.leftOut.length ? `<br><span class="muted">${rel.leftOut.length} of pc-sync’s files left out: not ours to ship (listed below).</span>` : ''
          } <span class="blue clickable" data-act="engShowDist">Show it</span></span></div>`
        : `<div class="step"><span class="red">✕</span>No release: ${esc(rel.error || 'it stopped')}</div>`
    : b.state === 'building'
      ? `<div class="step"><span class="muted">…</span>Building the engine…</div>`
      : b.state === 'ok'
        ? `<div class="step"><span class="green">✓</span>Built in ${b.secs} s. The lab zoo restarted on it.${
            S.play.running && S.play.kind !== 'original' ? ' <span class="blue clickable" data-act="playRestart">Restart the game</span> to play the new build.' : ' <span class="blue clickable" data-act="tab" data-arg="play">Play it</span>'
          }</div>`
        : b.state === 'failed'
          ? `<div class="step"><span class="red">✕</span>Build failed: ${b.problems.length} problem${b.problems.length === 1 ? '' : 's'} <span class="blue clickable" data-act="engBottom" data-arg="probs">See them</span></div>`
          : `<div class="step"><span class="muted">○</span>Change the code, then <b>Build engine</b>.</div>`;
  body.innerHTML = head + `<div class="mono dim" style="font-size:11.5px;white-space:pre-wrap">${esc(b.log.slice(-60).join('\n'))}</div>`;
  if (b.state === 'building') body.scrollTop = body.scrollHeight;
}

// ------------------------------------------------------------ Sync
// The engine to GitHub (the home: a branch and a pull request) and CodeRook
// (a mirror of main). What the person may do on GitHub decides what's
// offered: the owner can mirror; with write access, push a branch; anyone
// else pushes to their fork and proposes from there (lab-app/sync.js)
const SY = { st: null, busy: '', log: [], msg: '' };
lab.onSyncLine((l) => {
  SY.log.push(l);
  if (S.tab === 'engine' && S.eng.tab === 'sync') renderEngineBuild();
});
async function syncRefresh(fetch) {
  SY.busy = 'Looking…';
  renderEngineBuild();
  SY.st = await lab.sync('status', { fetch: !!fetch });
  SY.busy = '';
  renderEngineBuild();
}
async function syncDo(what, args, label) {
  SY.busy = label;
  renderEngineBuild();
  const r = await lab.sync(what, args);
  SY.busy = '';
  const line = r.ok ? `✓ ${label.replace(/…$/, '')}${r.url ? ': ' + r.url : r.nothing ? ': nothing to commit' : ''}` : `✕ ${label.replace(/…$/, '')}: ${r.error || 'it didn’t work'}`;
  SY.log.push(line);
  if (r.out) SY.log.push(...r.out.split('\n').slice(-8));
  toast(esc(line), r.ok ? 2500 : 6000);
  await syncRefresh();
  return r;
}
const roleText = { admin: 'owner (admin)', maintain: 'maintainer', write: 'helper (write)', read: 'contributor (read: your fork)', none: 'not signed in to GitHub' };
function syncHtml() {
  const st = SY.st;
  if (!st) return `<div class="muted">${SY.busy || 'Looking at the repo…'}</div>`;
  const w = st.who || {};
  const owner = w.role === 'admin';
  const canPush = w.role !== 'none';
  const changed = st.files.length;
  const where = w.role === 'read' ? `your fork (${esc(w.gh)}/zt1-engine)` : `GitHub (${esc(st.home)})`;
  let h = `<div class="synchead"><div><b class="mono">${esc(st.branch)}</b> <span class="muted">${st.behind ? `${st.behind} behind ${esc(st.base)}` : `up to date with ${esc(st.base)}`}${st.ahead ? ` · ${st.ahead} ahead` : ''}${
    st.upstream && st.upAhead ? ` · ${st.upAhead} not pushed` : ''
  }</span></div><div class="muted">GitHub: <b>${esc(w.gh || '—')}</b>, ${esc(roleText[w.role] || w.role)}${w.ghError ? ` <span class="red">${esc(w.ghError)}</span>` : ''} · CodeRook: ${
    st.coderook.signedIn ? `<span class="green">signed in</span>, mirror <b>${esc(st.coderook.project)}</b>${st.coderook.mirrorLinked ? '' : ' (made on the first mirror)'}` : '<span class="amber">not signed in (cbx sign-in)</span>'
  }</div><div class="spacer"></div><span class="clickable blue" data-act="syncGo" data-arg="refresh">↻ Look again</span></div>`;
  if (SY.busy) h += `<div class="step"><span class="muted">…</span>${esc(SY.busy)}</div>`;
  // conflicts first: nothing else until they're settled
  if (st.conflicts.length)
    h +=
      `<div class="label red">Conflicts with ${esc(st.base)} (${st.conflicts.length}): settle each, then Commit</div>` +
      st.conflicts
        .map(
          (f) =>
            `<div class="need bad"><div class="mono">${esc(f)}</div><div class="nacts"><span class="clickable blue" data-act="engOpen" data-arg="${esc(f)}:1">Open it</span><span class="clickable" data-act="syncResolve" data-arg="${esc(f)}|ours">Take mine</span><span class="clickable" data-act="syncResolve" data-arg="${esc(
              f,
            )}|theirs">Take theirs</span><span class="clickable" data-act="syncResolve" data-arg="${esc(f)}|done" title="After fixing it by hand in the editor">Fixed it</span></div></div>`,
        )
        .join('') +
      `<div class="nacts"><span class="clickable red" data-act="syncGo" data-arg="abort">Give up the merge</span></div>`;
  // the steps
  const btn = (act, label, title, off) => `<div class="btn small${off ? ' disabled' : ''}" data-act="syncGo" data-arg="${act}" title="${esc(title)}">${label}</div>`;
  h += `<div class="syncsteps">${btn('pull', '① Get latest ' + esc(st.base), 'Fetch GitHub, merge its ' + st.base + ' into this branch (conflicts listed here)', !!SY.busy)}${btn(
    'commit',
    `② Commit${changed ? ` (${changed})` : ''}`,
    'Everything changed (.gitignore keeps build/, the game files and the rest out)',
    !!SY.busy || !changed || st.conflicts.length,
  )}${btn('push', '③ Push', 'To ' + where, !!SY.busy || !canPush)}${btn(
    'propose',
    st.pr ? `④ Pull request #${st.pr.number} open` : '④ Propose',
    st.pr ? st.pr.url : 'A pull request into ' + st.base + ' on ' + st.home,
    !!SY.busy || !canPush || st.branch === st.base || !!st.pr,
  )}${owner ? btn('mirror', '⑤ Mirror main to CodeRook', 'Main as it is on GitHub, to CodeRook (' + st.coderook.project + ', public)', !!SY.busy || !st.coderook.signedIn) : ''}<div class="spacer"></div>${btn(
    'all',
    '▶ Sync',
    'Get latest, commit, push, and propose if there’s no pull request yet: you see what goes where first',
    !!SY.busy || !canPush,
  )}${st.branch === st.base ? btn('branch', 'New branch', 'Work on a branch of your own, proposed into ' + st.base, !!SY.busy) : ''}</div>`;
  if (st.pr) h += `<div class="muted">Pull request: <span class="clickable blue" data-act="syncOpenPr">#${st.pr.number} ${esc(st.pr.title)}</span></div>`;
  h += `<div class="label">Changed (${changed})</div><div class="syncfiles mono">${
    st.files
      .slice(0, 400)
      .map((f) => `<div><span class="${f.x === '?' ? 'green' : f.x === 'D' || f.y === 'D' ? 'red' : 'amber'}">${esc((f.x + f.y).replace(/ /g, '·'))}</span> <span class="clickable" data-act="engOpen" data-arg="${esc(f.path)}:1">${esc(f.path)}</span></div>`)
      .join('') || '<span class="muted">Nothing changed.</span>'
  }</div>`;
  if (SY.log.length) h += `<div class="label">What happened</div><div class="mono dim" style="font-size:11.5px;white-space:pre-wrap">${esc(SY.log.slice(-40).join('\n'))}</div>`;
  return h;
}

// ------------------------------------------------------------ Calls
// Who calls a function (or what it calls), as a tree: each caller opens to
// its own callers, every row goes to the place (the call, in the caller)
const CALLS = { dir: 'in', root: null, kids: new Map(), open: new Set(), busy: false, error: '' };
let callsSeq = 0;
const callKey = (c) => `${c.rel || c.abs}:${c.line}:${c.name}`;

async function showCalls(rel, line, col, dir) {
  const E = S.eng;
  CALLS.dir = dir || CALLS.dir;
  CALLS.busy = true;
  CALLS.error = '';
  E.tab = 'calls';
  renderEngineBuild();
  const r = await CodeEditor.callsAt(rel, line, col);
  if (r.error) {
    CALLS.busy = false;
    CALLS.error = r.error === 'not in a function' ? 'Put the cursor in a function (or on a call to one).' : `clangd: ${r.error}`;
    renderEngineBuild();
    return;
  }
  CALLS.root = { name: r.item.name, detail: r.item.detail || '', item: r.item, ...CodeEditor.placeOf(r.item), inside: r.inside };
  CALLS.kids = new Map();
  CALLS.open = new Set([callKey(CALLS.root)]);
  await loadCalls(CALLS.root);
  CALLS.busy = false;
  renderEngineBuild();
}

async function loadCalls(c) {
  const k = callKey(c);
  if (CALLS.kids.has(k)) return;
  CALLS.kids.set(k, null);
  CALLS.kids.set(k, await CodeEditor.callsOf(c.item, CALLS.dir));
}

function callsHtml() {
  if (CALLS.busy) return '<div class="muted">Asking clangd…</div>';
  if (CALLS.error) return `<div class="muted">${esc(CALLS.error)}</div>`;
  if (!CALLS.root) return '<div class="muted">Right-click in a function › <b>Who calls this</b> (or Shift+Alt+H), and its callers show here.</div>';
  const seg = (d, l) => `<span class="${CALLS.dir === d ? 'on' : ''}" data-act="callsDir" data-arg="${d}">${l}</span>`;
  const row = (c, depth, seen) => {
    const k = callKey(c);
    const kids = CALLS.kids.get(k);
    const open = CALLS.open.has(k);
    const where = c.rel ? `${c.rel.replace(/^src\//, '')}:${c.line}` : (c.abs || '').split('/').pop() + `:${c.line}`;
    const loop = seen.has(c.name);
    let h = `<div class="callrow" style="padding-left:${depth * 16 + 4}px"><span class="tw" data-act="callsOpen" data-arg="${esc(k)}">${loop ? '↻' : open ? '▾' : '▸'}</span><span class="clickable" data-act="callsGo" data-arg="${esc(k)}"><span class="mono">${esc(c.name)}</span> <span class="dim mono">${esc(where)}</span>${
      c.times > 1 ? ` <span class="dim">×${c.times}</span>` : ''
    }</span></div>`;
    if (open && !loop) {
      if (kids === undefined || kids === null) h += `<div class="muted" style="padding-left:${depth * 16 + 24}px">…</div>`;
      else if (!kids.length)
        h += `<div class="muted" style="padding-left:${depth * 16 + 24}px">${CALLS.dir === 'in' ? 'Nothing in the engine calls it (or only through a pointer: a std::function, a callback).' : 'It calls nothing of ours.'}</div>`;
      else {
        const s2 = new Set(seen).add(c.name);
        for (const kc of kids) h += row(kc, depth + 1, s2);
      }
    }
    return h;
  };
  return `<div class="callshead"><span class="seg">${seg('in', 'Who calls it')}${seg('out', 'What it calls')}</span><span class="muted">${
    CALLS.root.inside ? 'the function the cursor is in' : 'at the cursor'
  }</span></div>${row(CALLS.root, 0, new Set())}`;
}

function callFind(k) {
  let hit = null;
  const look = (c) => {
    if (hit) return;
    if (callKey(c) === k) hit = c;
    for (const kc of CALLS.kids.get(callKey(c)) || []) look(kc);
  };
  if (CALLS.root) look(CALLS.root);
  return hit;
}

// ------------------------------------------------ the binary in the editor
// Engine code's suggestions beside clangd's, from what the Lab knows:
//   ZT_STEP("|  ZT_FN("|   zoo.exe's functions - type a name, part of what
//                         it does, or an address; the address goes in
//   +0x|                  the mapped struct fields at that offset
//   num("|  set(m, "|     the game's own .ai keys and behaviour sets (as the
//                         engine reads them, lowercase)
//   0x| elsewhere          functions by address (a comment's "(zoo.exe 0x…)")
// and on hover, what an address, a field or a game key is
const ZOOKEYS = { keys: null, sets: null, asked: 0, waits: [] };
function zooKeys() {
  if (ZOOKEYS.keys) return Promise.resolve(ZOOKEYS);
  if (!SCENE_MSG.aiKeys)
    SCENE_MSG.aiKeys = (m) => {
      ZOOKEYS.keys = m.keys;
      ZOOKEYS.sets = m.sets;
      for (const w of ZOOKEYS.waits.splice(0)) w();
    };
  if (Date.now() - ZOOKEYS.asked > 5000 && S.engine.status === 'connected') {
    ZOOKEYS.asked = Date.now();
    lab.engineSend({ cmd: 'aiKeys' });
  }
  return new Promise((r) => {
    ZOOKEYS.waits.push(() => r(ZOOKEYS));
    setTimeout(() => r(ZOOKEYS), 4000);
  });
}
function zooFunctions() {
  // (the knowledge base's first - named, described - then Ghidra's named)
  const kb = kbFns();
  const out = Object.entries(kb).map(([a, e]) => ({ addr: a, name: e.name, summary: e.summary || '', engine: (e.engine || []).length > 0, kb: true }));
  const seen = new Set(out.map((f) => f.addr));
  for (const g of S.ghidra.functions || []) {
    const a = hexKey(g.address);
    if (seen.has(a)) continue;
    out.push({ addr: a, name: g.name, summary: '', engine: false, kb: false, unnamed: isDefaultName(g.name) });
  }
  return out;
}
const fnItem = (f) => ({
  label: f.name,
  desc: f.addr,
  detail: f.summary || (f.unnamed ? 'not named yet' : ''),
  doc: `**${esc(f.name)}** · \`${f.addr}\`${f.summary ? `\n\n${f.summary}` : ''}${f.engine ? '\n\n*In our engine already.*' : ''}`,
  insert: f.addr,
  filter: `${f.name} ${f.addr} ${f.summary}`,
  kind: f.engine ? 'Reference' : 'Function',
});
CodeEditor.hooks.zoo = {
  async complete(before) {
    let m;
    if ((m = /ZT_(?:STEP|FN)\(\s*"([^"]*)$/.exec(before))) {
      const typed = m[1];
      const words = typed.toLowerCase().split(/\s+/).filter(Boolean);
      const fns = zooFunctions().filter((f) => !f.unnamed || /^0x/i.test(typed));
      const hits = fns.filter((f) => words.every((w) => `${f.name} ${f.addr} ${f.summary}`.toLowerCase().includes(w)));
      return { typed, items: hits.slice(0, 300).map(fnItem) };
    }
    if ((m = /\+\s*(0x[0-9a-fA-F]*)$/.exec(before))) {
      const typed = m[1];
      const items = [];
      for (const [sname, s] of Object.entries((S.kb && S.kb.structs) || {}))
        for (const [off, f] of Object.entries(s.fields))
          if (off.startsWith(typed.toLowerCase()))
            items.push({ label: `${off} ${f.name}`, desc: sname, detail: `${f.type || ''} · ${f.summary || ''}`, insert: off, filter: `${off} ${f.name} ${sname}`, kind: 'Field' });
      // (ZTAnimalType+0x…: that struct's fields first)
      const named = (/(\w+)\s*\+\s*0x[0-9a-fA-F]*$/.exec(before) || [])[1];
      if (named) items.sort((a, b) => (b.desc === named) - (a.desc === named));
      return { typed, items };
    }
    if ((m = /\b(num|getList|getInt|getString|set|pushSet|moveOf|has)\s*\(\s*(?:[^,"()]*,\s*)?"([A-Za-z0-9_]*)$/.exec(before))) {
      const [, fn, typed] = m;
      const z = await zooKeys();
      if (!z.keys) return null;
      const want = /^(set|pushSet|moveOf)$/.test(fn) ? ['sets'] : /^(num|getList|getInt|getString)$/.test(fn) ? ['keys'] : ['keys', 'sets'];
      const t = typed.toLowerCase();
      const items = [];
      for (const w of want)
        for (const [k, n, eg] of z[w])
          if (k.toLowerCase().startsWith(t))
            items.push({ label: k, desc: `${n} file${n === 1 ? '' : 's'}`, detail: `${w === 'sets' ? 'Behaviour set' : 'Key'} · e.g. ${eg}`, insert: k.toLowerCase(), filter: k.toLowerCase(), kind: w === 'sets' ? 'Event' : 'Property' });
      items.sort((a, b) => parseInt(b.desc) - parseInt(a.desc));
      return { typed, items: items.slice(0, 400) };
    }
    if ((m = /(?:^|[^\w+])(0x[0-9a-fA-F]{2,8})$/.exec(before))) {
      const typed = m[1].toLowerCase();
      const hits = zooFunctions().filter((f) => f.addr.startsWith(typed));
      return { typed: m[1], items: hits.slice(0, 200).map(fnItem) };
    }
    return null;
  },
  hover(word, line) {
    if (/^0x[0-9a-f]{3,8}$/i.test(word)) {
      const a = hexKey(word);
      const out = [];
      // (after a "+": a field at that offset)
      if (new RegExp(`\\+\\s*${word}\\b`, 'i').test(line))
        for (const f of fieldsAt(word)) out.push(`**${esc(f.struct)}.${esc(f.name)}** · ${esc(f.type || '')}\n\n${esc(f.summary || '')}`);
      const e = kbFns()[a];
      const g = (S.ghidra.functions || []).find((x) => hexKey(x.address) === a);
      if (e) out.push(`**${esc(e.name)}** · zoo.exe \`${a}\`${e.provisional ? ' · provisional name' : ''}\n\n${esc(e.summary || '')}${(e.engine || []).length ? `\n\nIn our engine: ${e.engine.map((x) => '`' + x + '`').join(', ')}` : ''}`);
      else if (g) out.push(`**${esc(g.name)}** · zoo.exe \`${a}\`${isDefaultName(g.name) ? ' · not named yet: name it in Functions' : ''}`);
      if (out.length) out.push('*Right-click › zoo.exe code here, to open it in Code.*');
      return out.length ? out.join('\n\n---\n\n') : null;
    }
    if (ZOOKEYS.keys && /^[cb][A-Za-z0-9]+$/.test(word)) {
      const w = word.toLowerCase();
      const k = ZOOKEYS.keys.find((x) => x[0].toLowerCase() === w) || ZOOKEYS.sets.find((x) => x[0].toLowerCase() === w);
      if (k) return `**${esc(k[0])}** · set in ${k[1]} of the game's .ai files (e.g. \`${esc(k[2])}\`)\n\n*Game files: has:${esc(k[0].toLowerCase())} lists them.*`;
    }
    return null;
  },
};
// (the engine's answer comes as a SCENE_MSG - scene.js's, so it's hooked up
// on the first ask; asked for once the engine's up, so hover knows them too)
setTimeout(() => zooKeys(), 8000);

// The right-click menu's Lab items
CodeEditor.hooks.menu = (id, at) => {
  if (!at.rel) return;
  if (id === 'callers' || id === 'callees') showCalls(at.rel, at.line, at.col, id === 'callers' ? 'in' : 'out');
  else if (id === 'search') {
    S.eng.q = at.word;
    const q = $('eng-q');
    if (q) {
      q.value = at.word;
      q.oninput();
    }
  } else if (id === 'breakpoint') {
    if (!/^src\//.test(at.rel)) return toast('Breakpoints go in the engine’s own code (src/).');
    ACTIONS.dkBp(`${at.rel}:${at.line}`);
    toast(DK.bps.has(`${at.rel}:${at.line}`) ? `The Scene stops at ${esc(at.rel)}:${at.line} when it runs (trace on).` : `No longer stops at ${esc(at.rel)}:${at.line}.`, 2500);
  } else if (id === 'zoo') {
    // (a ZT_STEP / ZT_FN names its zoo.exe address; else the function's)
    const m = /0x[0-9a-fA-F]{5,8}/.exec(at.text) || /0x[0-9a-fA-F]{5,8}/.exec(at.word);
    if (m) ACTIONS.openCode(m[0]);
    else toast('No zoo.exe address on this line: its ZT_STEP / ZT_FN names one when it’s been found.', 3500);
  } else if (id === 'copy') {
    navigator.clipboard.writeText(`${at.rel}:${at.line}`);
    toast(`Copied ${esc(at.rel)}:${at.line}`, 1500);
  } else if (id === 'build') ACTIONS.engBuild();
};

function renderEngineLeft() {
  const E = S.eng;
  const q = E.q.trim();
  let left = '';
  if (q.length >= 2) {
    left = E.hits
      ? E.hits.length
        ? E.hits
            .map(
              (h) =>
                `<div class="item" data-act="engOpen" data-arg="${esc(h.file)}:${h.line}"><div class="name mono" style="font-size:12px">${esc(h.file.replace(/^src\//, ''))}:${h.line}</div><div class="sub mono" style="font-size:11.5px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis">${esc(h.text)}</div></div>`,
            )
            .join('') + (E.hits.length >= 400 ? '<div class="muted" style="padding:8px">(the first 400)</div>' : '')
        : '<div class="muted" style="padding:8px">Nothing in the engine’s code has that.</div>'
      : '<div class="muted" style="padding:8px">Searching…</div>';
  } else {
    left = (E.files || [])
      .map((f) => `<div class="item${f === E.file ? ' sel' : ''}" data-act="engOpen" data-arg="${esc(f)}:1"><div class="name mono" style="font-size:12px">${esc(f.replace(/^src\//, ''))}</div></div>`)
      .join('');
  }
  if ($('ee-left')) $('ee-left').innerHTML = left;
}

function renderEngine() {
  const E = S.eng;
  if (!E.files) {
    lab.srcList().then((f) => {
      E.files = f;
      if (S.tab === 'engine') renderEngineLeft();
    });
  }
  if (E.file && !CodeEditor.isOpen(E.file)) {
    openEngineFile(E.file, E.goto);
    return;
  }
  if (E.file && !E.opened.includes(E.file)) E.opened.push(E.file);
  if (!E.left) E.left = store.get('engLeft', 'code');
  const leftTabs = `<div class="seg mini lefttabs"><div class="${E.left !== 'game' ? 'on' : ''}" data-act="engLeft" data-arg="code">Our code</div><div class="${E.left === 'game' ? 'on' : ''}" data-act="engLeft" data-arg="game">Game files</div></div>`;
  const dock = engineDock();
  // (the game files' list is kept as it is: only put back in its slot)
  const gfKeep = GF.els && GF.els.engine;
  if (gfKeep) gfKeep.remove();
  dock.els.files.innerHTML = `${leftTabs}${
    E.left === 'game'
      ? '<div class="gfslot" id="eng-gf"></div>'
      : `<div style="display:flex;gap:6px;margin-bottom:8px"><input class="search" id="eng-q" style="flex:1;min-width:0" placeholder="Search all of the engine's code… (vendor/… for its libraries)" value="${esc(E.q)}"><div class="btn small ghost" data-act="engNewScript" title="A script: engine code with the zoo's parts set up for you (the animals, guests, staff, fences, map, the clock, what's picked in the Test Zoo), run on the Test Zoo from here">＋ New script</div><div class="btn small ghost" data-act="engNewFile" title="A new source file in src/ (.cpp, .hpp or .h): built with the rest on the next Build">＋ New file</div></div><div id="ee-left" style="overflow:auto;flex:1"></div>`
  }`;
  dock.els.editor.innerHTML = `${
      E.file
        ? `<div class="filetabs"><div id="ee-tabs" style="display:flex;overflow-x:auto;padding:0;background:none;box-shadow:none">${engineTabs()}</div><div class="spacer"></div><span id="ee-lsp" style="padding:9px 8px;font-size:12px"></span><span class="muted" id="ee-cursor" style="padding:9px 8px;font-size:12px"></span><div class="btn ghost small" data-act="engFind" title="Ctrl+F (Ctrl+H: replace)">Find</div><div class="btn ghost small" data-act="engOpenOutside">Open in your editor</div></div>
      ${engBarHtml(E.file)}<div style="flex:1;position:relative;min-height:0" id="ee-slot"></div>`
        : `<div style="flex:1;position:relative"><div class="stagemsg"><b>Our engine's code</b><span>Search it in Files, or open it from a function’s <b>In our engine</b> in Original Game.<br>Change it here, then <b>Build engine</b> and Play.</span></div></div>`
    }`;
  dock.els.output.innerHTML = `<div class="buildout" id="eb-box"><div class="tabsrow"><div id="eb-out" data-act="engBottom" data-arg="out">Build output</div><div id="eb-probs" data-act="engBottom" data-arg="probs">Problems 0</div><div id="eb-calls" data-act="engBottom" data-arg="calls" title="Who calls the function at the cursor (right-click › Who calls this, or Shift+Alt+H)">Calls</div><div id="eb-sync" data-act="engBottom" data-arg="sync" title="Commit, push to GitHub, propose, mirror to CodeRook">Sync</div><div id="eb-run" data-act="engBottom" data-arg="run" title="What the last reader run read">Run</div><div class="spacer"></div><div class="btn small ghost${E.build.state === 'building' ? ' disabled' : ''}" data-act="engRelease" title="Build it x64 Release in build-x64/ and make dist/zt1-engine-<date>-win64.zip (+ .sha256), as the Alpha was made, without any of the original game's files. Nothing is uploaded.">Make release</div></div><div class="body" id="eb-body"></div></div>`;
  if (E.left === 'game') gfMount($('eng-gf'), 'engine');
  if (/^game:/.test(E.file || '')) gfRefsInto($('eng-refs'), E.file.slice(5));
  // (Our code's search list, whatever's open: a game file too)
  if (E.left !== 'game') renderEngineLeft();
  renderEngineBuild();
  const s = $('eng-q');
  if (s) s.oninput = () => {
    E.q = s.value;
    clearTimeout(renderEngine.t);
    renderEngine.t = setTimeout(async () => {
      const qq = E.q.trim();
      E.hits = null;
      if (/^vendor\//i.test(qq)) {
        // (the libraries' files, by name: "vendor/pe-resource" finds them)
        if (!E.vendorFiles) E.vendorFiles = await lab.srcList('vendor');
        const n = qq.toLowerCase();
        E.hits = E.vendorFiles.filter((f) => f.toLowerCase().includes(n)).slice(0, 400).map((f) => ({ file: f, line: 1, text: '(a library built with the engine)' }));
      } else if (qq.length >= 2) E.hits = await lab.srcGrep(qq);
      if (S.tab === 'engine' && E.q.trim() === qq) renderEngineLeft();
    }, 300);
  };
  if (!E.file) return;
  CodeEditor.mount($('ee-slot')).then(() => {
    CodeEditor.show(E.file, E.goto, E.col);
    refreshEngineChrome();
  });
}

// (the editor's events: saved with Ctrl+S, the tabs' dots, where the cursor is)
CodeEditor.hooks.save = () => saveEngineFile().then((ok) => ok && (toast('Saved ' + esc(S.eng.file)), refreshEngineChrome()));
CodeEditor.hooks.change = () => {
  clearTimeout(refreshEngineChrome.t);
  refreshEngineChrome.t = setTimeout(refreshEngineChrome, 150);
};
CodeEditor.hooks.cursor = (line, col) => {
  S.eng.goto = line;
  S.eng.col = col;
  const c = $('ee-cursor');
  if (c) c.textContent = `Ln ${line}, Col ${col}`;
  if (/^src\//.test(S.eng.file || '')) store.set('engLine', line);
};
CodeEditor.hooks.open = (rel, line) => openEngineFile(rel, line);
CodeEditor.lsp.onStatus = () => refreshEngineChrome();

lab.onBuildLine((l) => {
  const b = S.eng.build;
  b.log.push(l);
  if (b.log.length > 400) b.log.splice(0, 100);
  if (S.tab === 'engine') renderEngineBuild();
});
lab.onReleaseDone((r) => {
  const b = S.eng.build;
  b.state = r.ok ? 'ok' : 'failed';
  b.secs = r.secs;
  b.release = r;
  toast(r.ok ? `Release made: ${esc(r.zip)}` : `No release: ${esc(r.error || 'it stopped')}`, 5000);
  if (S.tab === 'engine') renderEngine();
});
lab.onBuildDone((r) => {
  const b = S.eng.build;
  b.state = r.ok ? 'ok' : 'failed';
  b.problems = r.problems;
  b.secs = r.secs;
  CodeEditor.setBuildProblems(r.problems);
  toast(r.ok ? `Engine built in ${r.secs} s.` : `The engine didn’t build: ${r.problems.length} problem${r.problems.length === 1 ? '' : 's'}.`, 4000);
  if (!r.ok && r.problems.length) S.eng.tab = 'probs';
  if (S.tab === 'engine') {
    renderEngine();
    if (!r.ok && r.problems[0]) openEngineFile(r.problems[0].file, r.problems[0].line, r.problems[0].col);
  }
});

// ---------------------------------------------------------- Game files
// The game's files as our engine reads them (the expansions' and updates'
// copies over the base game's): what a layout or config really says
function renderFiles() {
  const F = S.files;
  const q = F.q.trim().toLowerCase();
  if (S.engine.status === 'connected' && F.prefix !== q && F.want !== q) {
    F.want = q;
    lab.engineSend({ cmd: 'files', prefix: q });
  }
  const list = F.list && F.prefix === q ? F.list : null;
  const open = F.open;
  let body = '';
  if (open) {
    const lines = open.kind === 'text' ? open.text.replace(/\r/g, '').split('\n') : [];
    const hl = (l) => {
      // [Section], key = value, ; comment
      const c = l.indexOf(';');
      const main = c >= 0 ? l.slice(0, c) : l;
      const com = c >= 0 ? `<span class="k-com">${esc(l.slice(c))}</span>` : '';
      let m;
      if ((m = main.match(/^(\s*)(\[.*\])(.*)$/))) return `${esc(m[1])}<span class="k-kw">${esc(m[2])}</span>${esc(m[3])}${com}`;
      if ((m = main.match(/^(\s*)([^=]+?)(\s*=\s*)(.*)$/)))
        return `${esc(m[1])}<span class="k-fn">${esc(m[2])}</span>${esc(m[3])}${
          /^-?\d+$/.test(m[4].trim()) ? `<span class="k-num">${esc(m[4])}</span>` : `<span class="k-str">${esc(m[4])}</span>`
        }${com}`;
      return esc(main) + com;
    };
    body =
      open.kind === 'missing'
        ? `<div class="muted" style="padding:14px">The engine can't find ${esc(open.name)}.</div>`
        : open.kind === 'binary'
          ? `<div class="muted" style="padding:14px">${esc(open.name)} is art or sound (${open.size.toLocaleString()} bytes), not text.</div>`
          : `<div class="code" style="padding:10px 0;flex:1;user-select:text">${lines
              .map((l, i) => `<div class="ln"><span class="no">${i + 1}</span><span>${hl(l)}</span></div>`)
              .join('')}</div>`;
  }
  $('v-files').innerHTML = `<div class="modlist" style="width:340px"><input class="search" id="filesq" style="width:100%;margin-bottom:8px" placeholder="A path, or the start of one: ui/credit" value="${esc(F.q)}">${
    S.engine.status !== 'connected'
      ? '<div class="muted" style="padding:8px">The engine reads the files: it isn’t running.</div>'
      : !list
        ? '<div class="muted" style="padding:8px">Looking…</div>'
        : list.length
          ? list
              .map(
                (f) =>
                  `<div class="item${open && open.name === f.name ? ' sel' : ''}" data-act="openGameFile" data-arg="${esc(f.name)}"><div class="name mono" style="font-size:12px">${esc(
                    f.name,
                  )}</div><div class="sub">${esc(f.from)}</div></div>`,
              )
              .join('') + (list.length >= 3000 ? '<div class="muted" style="padding:8px">(the first 3,000)</div>' : '')
          : '<div class="muted" style="padding:8px">No files start with that.</div>'
  }</div><div style="flex:1;display:flex;flex-direction:column;min-width:0">${
    open
      ? `<div class="filetabs"><div class="on mono">${esc(open.name)}</div><div class="spacer"></div><div style="color:var(--muted)" title="${esc(open.from)}">from ${esc(
          open.from.split(/[\\/]/).slice(-2).join('/'),
        )}</div></div><div style="flex:1;overflow:auto;display:flex;background:var(--bg1)">${body}</div>`
      : `<div class="stagemsg"><b>The game's files</b><span>Layouts (.lyt), configs (.cfg, .ai), as the engine reads them:<br>an expansion's or update's copy over the base game's.</span></div>`
  }</div>`;
  const s = $('filesq');
  if (!s) return;
  s.oninput = () => {
    F.q = s.value;
    store.set('filesQ', F.q);
    clearTimeout(renderFiles.t);
    renderFiles.t = setTimeout(() => {
      const pos = s.selectionStart;
      renderFiles();
      const n = $('filesq');
      n.focus();
      n.setSelectionRange(pos, pos);
    }, 250);
  };
}

// --------------------------------------------------------------- Play
function renderPlay() {
  const p = S.play;
  // (one Start: in the middle while it's not running; its controls up here once it is)
  $('playbar').innerHTML = `<b>Play Game</b><span class="muted">${p.running ? (p.kind === 'original' ? 'The original game, zoo.exe.' : 'Our engine, as it runs now.') : 'The whole game: our engine, or the original to compare.'}</span><div class="spacer"></div>${
    p.running
      ? `<span id="rec-bar" style="display:flex;align-items:center;gap:8px">${typeof recBar === 'function' ? recBar() : ''}</span><label class="muted" style="display:flex;align-items:center;gap:6px" title="The game's own volume (Windows' mixer has it on its own): nothing else's">${ic('volume')}<input type="range" id="play-vol" min="0" max="100" step="1" value="${playVolume()}" style="width:110px"><span id="play-vol-n" class="mono" style="min-width:3ch">${playVolume()}</span></label><div class="btn" data-act="playFocus" title="Send the keyboard to the game (it has the mouse when you click in it)">Keyboard to game</div><div class="btn" data-act="playStop">Stop the game</div>`
      : ''
  }`;
  const vol = $('play-vol');
  if (vol)
    vol.oninput = () => {
      store.set('playVolume', +vol.value);
      $('play-vol-n').textContent = vol.value;
      lab.playCmd('volume ' + vol.value);
    };
  $('playhint').innerHTML = p.running
    ? `<span>Loading the game…</span>`
    : `<b>Play the game</b><span>Either game runs here, in this view: menus, freeform zoos, scenarios.<br>Play original runs the real zoo.exe, windowed by our hook (its disc image is mounted for it).</span><div style="display:flex;gap:10px;justify-content:center"><div class="btn primary${p.starting ? ' disabled' : ''}" data-act="playStart" title="The game as our engine runs it now, in this view">${p.starting ? 'Starting…' : 'Play ZT1 Engine'}</div><div class="btn" data-act="playOriginal" title="The original zoo.exe, here in this view too (our hook runs it windowed) - to compare">Play original</div></div>`;
  if (typeof renderSteps === 'function') renderSteps();
  if (p.running) sendPlayRect();
}

// The Play volume (0-100, the game's own: src/SessionVolume.hpp), kept
// for the next game; sent as each starts - a few times, as it comes up
function playVolume() {
  const v = store.get('playVolume', 100);
  return Number.isFinite(+v) ? Math.max(0, Math.min(100, +v)) : 100;
}
function sendPlayVolume() {
  for (const t of [800, 3000, 8000]) setTimeout(() => S.play.running && lab.playCmd('volume ' + playVolume()), t);
}

// Where the game's window goes: the Play area, in page pixels (main.js
// turns that into the screen's); nowhere while another view is up or a
// dialog is over it
function sendPlayRect() {
  if (!S.play.running) return;
  if (S.tab !== 'play' || !$('modal').classList.contains('hidden')) return lab.playArea(null);
  const r = $('playarea').getBoundingClientRect();
  lab.playArea({ x: r.left, y: r.top, w: r.width, h: r.height });
}

// --------------------------------------------------------------- actions
function pickMod(stem) {
  S.mod = stem;
  store.set('mod', stem);
  const m = S.mods.find((x) => x.stem === stem);
  if (m && m.addresses.length) {
    S.sel.addr = m.addresses[0];
    store.set('fn', S.sel.addr);
  }
}

async function runMod(stem) {
  if (!stem) return toast('Pick an experiment first (left side), or make one in Experiments.');
  if (S.editor.stem === stem) await saveEditor();
  S.builds[stem] = { state: 'building', problems: [] };
  lab.engineSend({ cmd: 'build', script: stem });
  renderLive();
}

const ACTIONS = {
  tab: (t) => setTab(t),
  run: async () => {
    if (S.tab === 'engine') return ACTIONS.engBuild();
    if (S.tab === 'mods' && S.editor.stem) {
      pickMod(S.editor.stem);
      await runMod(S.editor.stem);
      setTab('scene');
    } else {
      await runMod(S.mod);
      if (S.tab !== 'scene') setTab('scene');
    }
  },
  restartEngine: () => {
    S.engine.status = 'starting';
    renderTop();
    lab.engineRestart();
  },
  newMod: () => newMod(S.tab === 'code' || S.tab === 'functions' ? S.sel.addr : null),
  pickMod: (stem) => {
    if (S.mod === stem) {
      openEditor(stem);
      setTab('mods');
      return;
    }
    pickMod(stem);
    renderScene();
  },
  editMod: (stem) => {
    pickMod(stem);
    openEditor(stem);
  },
  system: (n) => {
    S.fview.filter = n;
    S.fview.limit = 300;
    setTab('functions');
  },
  rotate: () => lab.engineSend({ cmd: 'rotate' }),
  focusLab: () => lab.engineSend({ cmd: 'focusLab' }),
  turnArt: () => lab.engineSend({ cmd: 'turn' }),
  stepOnce: () => lab.engineSend({ cmd: 'step' }),
  stopMod: () => lab.engineSend({ cmd: 'stop' }),
  cycleSpeed: () => {
    const sps = (S.engine.state || {}).sps || 4;
    lab.engineSend({ cmd: 'sps', value: sps >= 16 ? 1 : sps >= 4 ? 16 : 4 });
  },
  openFn: (stat) => {
    const addr = STAT_FN[stat];
    if (S.drawer.open && S.drawer.field === stat) {
      S.drawer.open = false;
      renderScene();
      return;
    }
    S.drawer = { open: true, addr, field: stat };
    S.sel.addr = addr;
    store.set('fn', addr);
    renderScene();
  },
  closeDrawer: () => {
    S.drawer.open = false;
    renderScene();
  },
  openCode: (addr) => {
    S.sel.addr = hexKey(addr);
    store.set('fn', S.sel.addr);
    setTab('code');
  },
  selFn: (addr) => ACTIONS.openCode(addr),
  openGameFile: (name) => lab.engineSend({ cmd: 'file', name }),
  engOpen: (arg) => {
    const i = arg.lastIndexOf(':');
    openEngineFile(arg.slice(0, i), +arg.slice(i + 1));
  },
  engOpenOutside: () => S.eng.file && lab.openFile(S.eng.file, S.eng.goto || 1),
  engFind: () => CodeEditor.action('actions.find'),
  // (the editor's right-click menu, opened from code: for testing it)
  engMenu: () => CodeEditor.action('editor.action.showContextMenu'),
  engTab: (f) => openEngineFile(f, null),
  // A new source file (src/...): asked for its path (or given one), started
  // with a comment saying what it's for, opened to write in
  engNewFile: async (given) => {
    let rel = (given || '').trim();
    let about = '';
    if (!rel) {
      const a = await ask('A new engine file', [
        { key: 'rel', label: 'Its path', value: 'src/' },
        { key: 'about', label: 'What it does (its first comment)', value: '' },
      ], 'Make it');
      if (!a || !a.rel) return;
      rel = a.rel.trim();
      about = (a.about || '').trim();
    }
    const header = /\.(hpp|h)$/i.test(rel);
    const guard = rel.split('/').pop().replace(/[^A-Za-z0-9]/g, '_').toUpperCase();
    const text = header
      ? `#ifndef ${guard}\n#define ${guard}\n\n// ${about || rel.split('/').pop()}\n\n#endif // ${guard}\n`
      : `// ${about || rel.split('/').pop()}\n`;
    const r = await lab.srcCreate(rel, text);
    if (!r.ok) return toast(esc(r.error), 4000);
    S.eng.files = null;
    toast(`Made <b>${esc(rel)}</b>.`, 2500);
    openEngineFile(rel, 1);
  },
  engClose: async (f) => {
    if (CodeEditor.isDirty(f)) await saveEngineFile(f);
    const E = S.eng;
    E.opened = E.opened.filter((x) => x !== f);
    if (E.file === f) E.file = E.opened[E.opened.length - 1] || null;
    renderEngine();
  },
  engBottom: (t) => {
    S.eng.tab = t;
    renderEngineBuild();
  },
  // (by code: "src/Animals.cpp:1450" or with ":out" for what it calls)
  engCalls: async (arg) => {
    const [rel, line, dir] = arg.split(':');
    await openEngineFile(rel, +line);
    await new Promise((r) => setTimeout(r, 300));
    await showCalls(rel, +line, 1, dir === 'out' ? 'out' : 'in');
  },
  syncGo: async (what) => {
    const st = SY.st;
    if (what === 'refresh') return syncRefresh(true);
    if (what === 'pull') return syncDo('pull', {}, 'Getting the latest ' + (st ? st.base : 'main') + '…');
    if (what === 'abort') return syncDo('abort', {}, 'Giving up the merge…');
    if (what === 'push') return syncDo('push', {}, 'Pushing…');
    if (what === 'mirror') return syncDo('mirror', {}, 'Mirroring main to CodeRook…');
    if (what === 'branch') {
      const a = await ask('A branch of your own', [{ key: 'name', label: 'Its name (what you’re working on)', hint: 'marine-animals' }]);
      if (a && a.name) return syncDo('branch', { name: a.name.trim() }, 'Starting ' + a.name.trim() + '…');
      return;
    }
    if (what === 'commit') {
      const a = await ask('Commit', [{ key: 'message', label: 'What changed, in a sentence or two', area: true, value: SY.msg }]);
      if (!a || !a.message) return;
      SY.msg = '';
      return syncDo('commit', { message: a.message }, 'Committing…');
    }
    if (what === 'propose') {
      const a = await ask('Propose into ' + st.base, [
        { key: 'title', label: 'Its title', value: st.branch },
        { key: 'body', label: 'What it does, and how you tested it', area: true },
      ]);
      if (a) return syncDo('propose', { title: a.title, body: a.body }, 'Opening the pull request…');
      return;
    }
    if (what === 'all') {
      // what goes where, first
      const w = st.who;
      const a = await ask(`Sync ${st.branch}`, [
        { key: 'message', label: `Commit message for the ${st.files.length} changed file${st.files.length === 1 ? '' : 's'}${st.files.length ? '' : ' (none: nothing to commit)'}`, area: true, value: SY.msg },
        {
          key: 'note',
          label: `Then: get the latest ${st.base} from GitHub (stops if there are conflicts), commit, push to ${w.role === 'read' ? 'your fork' : st.home + ' · ' + st.branch}${
            st.branch !== st.base && !st.pr ? ', and open a pull request into ' + st.base : ''
          }. Nothing is force-pushed.`,
          value: '',
          hint: '(nothing to type here)',
        },
      ], 'Sync');
      if (!a) return;
      const p = await syncDo('pull', {}, 'Getting the latest ' + st.base + '…');
      if (!p.ok || (SY.st && SY.st.conflicts.length)) return;
      if (SY.st.files.length) {
        if (!a.message) return toast('A commit needs a message.');
        const c = await syncDo('commit', { message: a.message }, 'Committing…');
        if (!c.ok) return;
      }
      const u = await syncDo('push', {}, 'Pushing…');
      if (!u.ok) return;
      if (SY.st.branch !== SY.st.base && !SY.st.pr) return ACTIONS.syncGo('propose');
    }
  },
  syncResolve: (arg) => {
    const i = arg.lastIndexOf('|');
    const f = arg.slice(0, i),
      how = arg.slice(i + 1);
    return syncDo('resolve', { file: f, how }, (how === 'done' ? 'Marking ' : how === 'ours' ? 'Taking mine for ' : 'Taking theirs for ') + f + '…');
  },
  syncOpenPr: () => SY.st && SY.st.pr && lab.openUrl && lab.openUrl(SY.st.pr.url),
  callsDir: (d) => {
    if (!CALLS.root || CALLS.dir === d) return;
    CALLS.dir = d;
    const r = CALLS.root;
    CALLS.kids = new Map();
    CALLS.open = new Set([callKey(r)]);
    loadCalls(r).then(renderEngineBuild);
    renderEngineBuild();
  },
  callsOpen: (k) => {
    const c = callFind(k);
    if (!c) return;
    if (CALLS.open.has(k)) CALLS.open.delete(k);
    else {
      CALLS.open.add(k);
      loadCalls(c).then(renderEngineBuild);
    }
    renderEngineBuild();
  },
  callsGo: (k) => {
    const c = callFind(k);
    if (c && c.rel) openEngineFile(c.rel, c.line);
    else if (c) toast(`${esc(c.name)} is outside the engine’s code (${esc(c.abs || '')}).`, 3000);
  },
  // (the lab zoo's set-up asked of the engine and kept: for the next one -
  // after a build, or the Lab started again, its page too)
  keepRebuild: async () => keepRebuild(),
  engBuild: async () => {
    if (S.eng.build.state === 'building') return;
    await saveAllEngineFiles();
    S.eng.build = { state: 'building', log: [], problems: [], secs: 0 };
    S.eng.tab = 'out';
    // (the lab zoo's set-up kept: the new engine makes it again)
    await keepRebuild();
    const r = await lab.engineBuild();
    if (r.error) {
      S.eng.build.state = 'failed';
      toast(esc(r.error), 5000);
    }
    if (S.tab === 'engine') renderEngine();
  },
  engRelease: async () => {
    if (S.eng.build.state === 'building') return;
    await saveAllEngineFiles();
    S.eng.build = { state: 'building', kind: 'release', log: [], problems: [], secs: 0, release: {} };
    S.eng.tab = 'out';
    if (S.tab === 'engine') renderEngine();
    const r = await lab.engineRelease();
    if (r.error) {
      S.eng.build.state = 'failed';
      S.eng.build.release = { error: r.error };
      toast(esc(r.error), 5000);
    }
    if (S.tab === 'engine') renderEngine();
  },
  engShowDist: () => {
    const z = (S.eng.build.release || {}).zip;
    if (z) lab.reveal(z);
  },
  playOriginal: async () => {
    if (S.play.running) return;
    // (the Play view's size first: the original's drawn at it)
    if (S.tab !== 'play') setTab('play');
    S.play.running = true;
    S.play.kind = 'original';
    renderPlay();
    const r = await lab.playOriginal();
    if (r && r.error) {
      S.play.running = false;
      renderPlay();
      return toast(esc(r.error), 6000);
    }
    for (const t of [500, 1500, 3000, 6000, 10000]) setTimeout(sendPlayRect, t);
    sendPlayVolume();
    recPoll();
    setTimeout(livePoll, 3000);
  },

  playRestart: async () => {
    await lab.playStop();
    const wait = () => (S.play.running ? setTimeout(wait, 300) : ACTIONS.playStart());
    setTimeout(wait, 300);
    setTab('play');
  },
  // Where a global (or anything at an address) is used: Ghidra's references
  xrefs: async (addr) => {
    const m = $('modal');
    const show = (body) => {
      m.innerHTML = `<div class="dialog" style="width:520px"><div class="h">Where <span class="mono amber">${esc(addr)}</span> is used</div>${body}<div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
      m.classList.remove('hidden');
      sendPlayRect();
    };
    m.onclick = (e) => {
      const go = e.target.closest('[data-go]');
      if (go || e.target.dataset.x === 'no' || e.target === m) {
        m.classList.add('hidden');
        m.innerHTML = '';
        if (go) ACTIONS.openCode(go.dataset.go);
      }
    };
    show('<div class="muted">Asking Ghidra…</div>');
    const r = await G('/xrefs?addr=' + plainHex(addr));
    if (!r || r.error || !r.json.refs) return show('<div class="muted">Ghidra isn’t connected.</div>');
    const refs = r.json.refs;
    // (one line per function: how many times, read or written; code that
    // isn't in a function Ghidra knows - a start-up loader, say - one line
    // per place, by its address)
    const by = new Map();
    for (const x of refs) {
      const k = x.function || 'code at ' + x.from;
      const e = by.get(k) || { name: k, at: x.from, n: 0, kinds: new Set() };
      e.n++;
      e.kinds.add(/WRITE/.test(x.type) ? 'writes' : /READ/.test(x.type) ? 'reads' : x.type.toLowerCase());
      by.set(k, e);
    }
    const fnAddr = (name, at) => {
      const m2 = name.match(/FUN_([0-9a-f]+)/i);
      if (m2) return '0x' + m2[1].replace(/^0+/, '');
      const g = (S.ghidra.functions || []).find((f) => f.name === name.split('::').pop());
      return g ? g.address : at;
    };
    show(
      by.size
        ? `<div class="section" style="max-height:420px;overflow:auto">${[...by.values()]
            .map((e) => {
              const a = fnAddr(e.name, e.at);
              const info = fnInfo(a);
              return `<div class="xref" data-go="${esc(a)}"><span>${esc(info.kb ? displayName(info) : e.name)}</span><span class="muted">${esc([...e.kinds].join(' + '))}${e.n > 1 ? ' · ' + e.n + '×' : ''}</span></div>`;
            })
            .join('')}</div>`
        : '<div class="muted">Nothing uses it directly.</div>',
    );
    // (a global - a value in the game's data: named here, kept as a number)
    if (parseInt(addr, 16) >= 0x600000) {
      const known = ((S.kb && S.kb.constants) || {})[String(parseInt(addr, 16))];
      const box = m.querySelector('.dialog');
      if (box)
        box.querySelector('.btns').insertAdjacentHTML(
          'beforebegin',
          `<div class="section"><div class="label">Name it</div>${known ? `<div class="muted" style="margin-bottom:4px">Known: ${esc(known.meaning || '')}</div>` : ''}<div class="fnrow"><input class="field" id="fg-name" placeholder="its name (e.g. cDefenseRadius)"><input class="field" id="fg-note" placeholder="what it is" style="flex:1"><div class="btn small primary" data-act="saveGlobal" data-arg="${esc(hexKey(addr))}">Save</div></div></div>`,
        );
    }
  },
  // (a function picked in the finder: read beside it)
  selFnRow: (addr) => {
    S.sel.addr = hexKey(addr);
    store.set('fn', S.sel.addr);
    if (S.tab !== 'code') return setTab('code');
    renderCode();
    renderFinder();
  },
  ffilter: (n) => {
    S.fview.filter = n;
    S.fview.limit = 300;
    renderFunctions();
  },
  fmore: () => {
    S.fview.limit += 500;
    renderFinder();
  },
  saveFn: () => saveFn(),
  // A field of a struct, named from field: (re/kb.py field)
  saveField: async (off) => {
    const st = $('ff-struct').value, name = ($('ff-name').value || '').trim(), type = $('ff-type').value, note = ($('ff-note').value || '').trim();
    if (!/^[A-Za-z_][A-Za-z0-9_]*$/.test(name)) return toast('A name is letters, digits and _ (like rampaging).');
    if (!(await ensureMe())) return;
    const r = await lab.kbRun(['field', st, off, name, type, note || name, '--confidence', 'likely']);
    if (!r.ok) return toast('Couldn’t save: ' + esc(r.out.slice(0, 200)), 5000);
    await loadKb();
    toast(`Saved <b>${esc(st)}.${esc(name)}</b> (+${esc(off)}) to the knowledge base.`);
    if (S.tab === 'code') renderFinder(), renderCode();
  },
  // A global (a value in the game's data), named from Where used (re/kb.py const)
  saveGlobal: async (a) => {
    const name = ($('fg-name').value || '').trim(), note = ($('fg-note').value || '').trim();
    if (!name) return toast('Give it a name first.');
    if (!(await ensureMe())) return;
    const r = await lab.kbRun(['const', a, `${name}${note ? ': ' + note : ''}`, '--kind', 'global']);
    if (!r.ok) return toast('Couldn’t save: ' + esc(r.out.slice(0, 200)), 5000);
    await loadKb();
    $('modal').classList.add('hidden');
    $('modal').innerHTML = '';
    toast(`Saved <b>${esc(name)}</b> (${esc(a)}) to the knowledge base.`);
  },
  // Code Ghidra's analysis missed, made a function (the bridge keeps it in
  // re/made_functions.json), then opened: its decompiled C, callers and all
  makeFn: async (start) => {
    if (!S.ghidra.ok) return toast('Ghidra isn’t connected: start the Lab bridge first.');
    const r = await lab.ghidra(`/makefn?addr=${plainHex(start)}&by=${encodeURIComponent(S.me || '')}`);
    const j = r && r.json;
    if (!j || !j.ok) return toast('Couldn’t make it a function: ' + esc((j && j.error) || (r && r.error) || 'no answer'), 5000);
    const a = hexKey(j.function.address);
    for (const k of Object.keys(S.cache)) if (k.includes('addr=' + plainHex(a)) || k.includes('addr=' + plainHex(start))) delete S.cache[k];
    delete RAW[a];
    delete RAW[hexKey(start)];
    delete S.cache['/functions'];
    await loadGhidraFunctions(true);
    ACTIONS.openCode(a);
    toast(`Made <b>${esc(j.function.name)}</b> (${j.function.size} bytes): its C is decompiled now.`);
  },
  // The functions using a number ("10028", "0x75"): a message, a task, a UI
  // element, a constant - each with the instruction that has it
  numberUsers: async (arg) => {
    const v = String(arg).trim();
    const r = await G('/scalar?value=' + encodeURIComponent(v), true);
    if (!r || !r.json) return { error: 'Ghidra isn’t connected.' };
    return r.json;
  },
  // Several at once ("0x52d94f 0x43eb4f ..."): every entry of a class table
  // Ghidra's analysis missed, made functions and kept (made_functions.json)
  makeFns: async (arg) => {
    if (!S.ghidra.ok) return { error: 'Ghidra isn’t connected.' };
    const made = [];
    const already = [];
    const inside = [];
    const failed = [];
    // (a function there already isn't made again: Ghidra's list says which)
    const listed = await G('/functions', true);
    const have = new Set(((listed && listed.json && listed.json.functions) || []).map((f) => hexKey(f.address)));
    for (const start of [...new Set(String(arg).split(/[\s,]+/).filter(Boolean))]) {
      if (have.has(hexKey(start))) {
        already.push(start);
        continue;
      }
      const r = await lab.ghidra(`/makefn?addr=${plainHex(start)}&by=${encodeURIComponent(S.me || '')}`);
      const j = r && r.json;
      if (j && j.ok) made.push(`${start} ${j.function.name} (${j.function.size} bytes)`);
      else if (j && j.function) inside.push(`${start} inside ${j.function.name}`);
      else failed.push(`${start}: ${(j && j.error) || 'no answer'}`);
    }
    delete S.cache['/functions'];
    await loadGhidraFunctions(true);
    toast(`Made ${made.length} functions.`, 4000);
    return { made, already, inside, failed };
  },
  seeLive: async (addr) => {
    const m = modsFor(addr)[0];
    if (m) {
      pickMod(m.stem);
      await runMod(m.stem);
    }
    setTab('scene');
    if (!m) toast('No experiment checks this one yet: <b>New experiment</b> makes one.', 4000);
  },
  modFor: async (addr) => {
    const m = modsFor(addr)[0];
    if (m) {
      pickMod(m.stem);
      await openEditor(m.stem);
      setTab('mods');
    } else newMod(addr);
  },
  openSrc: (where) => ACTIONS.engOpen(where),
  toggleConsole: () => {
    S.consoleOpen = !S.consoleOpen;
    renderConsole();
    requestAnimationFrame(sizeCanvas);
  },
  openMods: () => {
    if (S.mod) openEditor(S.mod);
    setTab('mods');
  },
  edFile: (f) => S.editor.stem && openEditor(S.editor.stem, f),
  openModFile: () => S.editor.stem && lab.openFile(`re/lab/scripts/${S.editor.stem}.${S.editor.file === 'cpp' ? 'cpp' : 'json'}`, 1),
  bottomTab: (t) => {
    S.bottomTab = t;
    renderBuildPanel();
  },
  gotoLine: (n) => {
    const ta = $('ed-text');
    const lines = ta.value.split('\n');
    let pos = 0;
    for (let i = 0; i < +n - 1 && i < lines.length; i++) pos += lines[i].length + 1;
    ta.focus();
    ta.setSelectionRange(pos, pos + (lines[+n - 1] || '').length);
    $('editor').scrollTop = Math.max(0, (+n - 5) * 12.5 * 1.85);
  },
  applyFix: (arg) => {
    const [line, from, to] = arg.split('|');
    const lines = S.editor.text.split('\n');
    lines[+line - 1] = lines[+line - 1].replace(new RegExp(`\\b${from}\\b`), to);
    S.editor.text = lines.join('\n');
    const b = S.builds[S.editor.stem];
    if (b) b.problems = b.problems.filter((p) => p.line !== +line);
    renderMods();
  },
  insertApi: (sig) => {
    const ta = $('ed-text');
    if (!ta) return;
    ta.focus();
    ta.setRangeText(sig + ';', ta.selectionStart, ta.selectionEnd, 'end');
    ta.oninput();
  },
  fieldTip: (key) => {
    const [struct, off] = key.split('|');
    const f = fieldsAt(off).find((x) => x.struct === struct);
    if (!f) return;
    const live = struct === 'ZTAnimal' ? liveValueFor(f.name) : null;
    ask(`${struct}.${f.name}`, [], 'Close').then(() => {});
    const d = document.querySelector('#modal .dialog');
    d.querySelector('.h').insertAdjacentHTML(
      'afterend',
      `<div><span class="mono amber">${esc(f.name)}</span> <span class="muted">${esc(struct)} +${esc(off)} · ${esc(f.type)}</span></div><div style="color:var(--text2);line-height:1.5">${esc(
        f.summary || 'No note yet.',
      )}</div>${live !== null ? `<div>Right now it’s <b>${esc(fmt(live))}</b> (the selected animal).</div>` : ''}<div class="muted" style="font-size:12px">Mapped by ${esc(f.by || '?')}, ${esc(f.confidence || '')}</div>`,
    );
    d.querySelector('[data-x="no"]').classList.add('hidden');
  },
  playStart: async () => {
    S.play.starting = true;
    renderPlay();
    const r = await lab.playStart();
    S.play.starting = false;
    if (r.error) {
      toast(esc(r.error), 5000);
      renderPlay();
      return;
    }
    S.play.running = true;
    S.play.kind = 'engine';
    renderPlay();
    // (its window comes once the engine's up: placed a few times as it does)
    for (const t of [300, 1000, 2500, 5000]) setTimeout(sendPlayRect, t);
    sendPlayVolume();
  },
  playStop: async () => {
    await lab.playStop();
  },
  playFocus: () => lab.playCmd('focus'),
  // A line to the game in Play (src/Embed.hpp): "click <x> <y>" at its
  // window's pixels, for testing menus without the mouse
  playCmd: (line) => lab.playCmd(String(line || '')),
  // The header's update button: Check updates, or - one downloaded and
  // checked - Restart to update
  checkLabUpdate: async () => {
    if (LAB_UPDATE.state === 'ready') {
      const r = await lab.labUpdateInstall();
      if (!r.ok) toast(esc(r.message), 6000);
      return;
    }
    const st = await lab.checkLabUpdate();
    showLabUpdate(st);
    if (st.state !== 'downloading' && st.state !== 'checking') toast(esc(st.message), 5000);
  },
};

// The Lab's update state (main.js labUpdater), on the header's button
let LAB_UPDATE = { state: 'idle', message: '' };
function showLabUpdate(st) {
  LAB_UPDATE = st || LAB_UPDATE;
  const b = $('lab-update-btn');
  if (!b) return;
  const s = LAB_UPDATE.state;
  b.textContent = s === 'checking' ? 'Checking…' : s === 'downloading' ? 'Downloading update…' : s === 'ready' ? 'Restart to update' : 'Check updates';
  b.classList.toggle('disabled', s === 'checking' || s === 'downloading');
  b.classList.toggle('primary', s === 'ready');
  b.classList.toggle('ghost', s !== 'ready');
  b.title = LAB_UPDATE.message || 'Check for a signed ZT1 Lab update';
}
lab.onLabUpdate((st) => showLabUpdate(st));
lab.labUpdateState().then(showLabUpdate);

document.addEventListener('click', (e) => {
  const el = e.target.closest('[data-act]');
  if (!el || !el.dataset.act) return;
  if (el.classList.contains('disabled')) return;
  const fn = ACTIONS[el.dataset.act];
  if (fn) fn(el.dataset.arg);
});

// The lab zoo's set-up, asked of the engine and kept - in S.rebuild for the
// engine a build starts, and in the page's store for a Lab started again
// (its page and engine new) - so the next engine makes it again
async function keepRebuild() {
  if (S.engine.status !== 'connected') return null;
  const got = new Promise((r) => {
    SCENE_MSG.rebuild = (m) => r(m);
    setTimeout(() => r(null), 2500);
  });
  lab.engineSend({ cmd: 'rebuildGet' });
  const m = await got;
  if (m && m.cmds && m.cmds.length) {
    S.rebuild = m;
    store.set('rebuildNext', m);
  }
  return m;
}

// ------------------------------------------------------------- loading
async function loadKb() {
  const r = await lab.kbLoad();
  S.kb = r.kb;
  S.refs = {};
  for (const [k, v] of Object.entries(r.refs)) S.refs[hexKey(k)] = v;
  S.me = r.me;
}

// (a build may cite new zoo.exe functions: Functions' "in our engine" again)
lab.onBuildDone(async (r) => {
  if (!r.ok) return;
  await loadKb();
  if (S.tab === 'code') renderFinder();
});

async function loadMods() {
  S.mods = await lab.modsList();
  if (!S.mod && S.mods.length) S.mod = S.mods[0].stem;
}

async function loadGhidraFunctions(force) {
  if (S.ghidra.loading) return;
  S.ghidra.loading = true;
  const r = await G('/functions', force);
  S.ghidra.loading = false;
  if (r && r.json && r.json.functions) {
    S.ghidra.functions = r.json.functions;
    if (S.tab === 'code') renderFinder();
    if (S.tab === 'scene') renderSceneLeft();
  }
}

async function loadApi() {
  const r = await lab.srcRead('re/lab/lab_api.h', 1, 0, 400);
  S.api = r ? parseApi(r.lines) : [];
}

async function pollGhidra() {
  let st = await lab.ghidraStatus();
  const was = S.ghidra.ok, wasStarting = S.ghidra.starting, wasBuilding = S.ghidra.building;
  // (the Lab's own headless Ghidra still opening the project: a minute or two;
  //  and the longer job before that, when it has to make the project first)
  S.ghidra.building = !!(st && st.building);
  S.ghidra.said = (st && st.said) || '';
  S.ghidra.starting = !!(st && st.starting);
  if (S.ghidra.starting || S.ghidra.building) st = null;
  S.ghidra.ok = !!st;
  S.ghidra.program = st ? `${st.program} · ${st.functions.toLocaleString()} functions` : '';
  if (S.ghidra.ok && (!was || !S.ghidra.functions)) {
    // (fresh from Ghidra now it's here: the saved copies may be old)
    for (const k of Object.keys(S.cache)) if (S.cache[k] && S.cache[k].cached) delete S.cache[k];
    loadGhidraFunctions(true);
  }
  if (was !== S.ghidra.ok || wasStarting !== S.ghidra.starting || wasBuilding !== S.ghidra.building) {
    renderTop();
    if (S.tab === 'code') renderCode();
    if (S.tab === 'code') renderFinder();
  }
}

// ------------------------------------------------------------- engine
lab.onEngineStatus((s) => {
  S.engine.status = s.status;
  S.engine.detail = s.detail;
  renderTop();
  if (s.status === 'connected') {
    lastViewport = '';
    sizeCanvas();
    lab.engineSend({ cmd: 'frame' });
    // (after a build: the lab zoo made again as it was)
    // (or kept by the Lab before it was started again)
    if (!S.rebuild && !S.rebuildTried) {
      S.rebuildTried = true;
      const kept = store.get('rebuildNext', null);
      if (kept && kept.cmds && kept.cmds.length) S.rebuild = kept;
    }
    store.set('rebuildNext', null);
    if (S.rebuild) {
      const rb = S.rebuild;
      S.rebuild = null;
      setTimeout(() => {
        lab.engineSend({ cmd: 'rebuildRun', cmds: rb.cmds, clock: rb.clock });
        toast(`Making the lab zoo again after the build (${rb.cmds.length} steps, back to ${Math.round(rb.clock)} s)…`, 3500);
      }, 1500);
    }
  }
  // (the page showing, again: some ask the engine for what they show)
  if (S.tab === 'scene') renderScene();
  else if (S.tab === 'files') {
    S.files.want = null;
    renderFiles();
  }
  // (Game files asked before the engine was up: asked again now it is)
  if (s.status === 'connected' && typeof GF !== 'undefined' && !GF.index) {
    GF.asked = 0;
    gfAskIndex();
    for (const h of Object.keys(GF.els)) gfRender(h);
  }
});

lab.onEngine((m) => {
  if (m.t === 'state') {
    const first = !S.engine.state;
    S.engine.state = m;
    if (first && S.tab === 'scene') renderScene();
    else renderLive();
  } else if (m.t === 'log') {
    S.engine.log.push(m.text);
    if (S.engine.log.length > 3000) S.engine.log.splice(0, 1000);
    if (S.consoleOpen && S.tab === 'scene') renderConsole();
  } else if (m.t === 'scripts') {
    loadMods().then(() => S.tab === 'scene' && renderSceneLeft());
  } else if (m.t === 'build') {
    S.builds[m.script] = { state: m.ok ? 'ok' : 'failed', problems: m.problems || [] };
    if (m.ok) toast(`<b>${esc(prettyStem(m.script))}</b> built: running in the lab zoo.`);
    else {
      toast(`<b>${esc(prettyStem(m.script))}</b> didn’t build: ${m.problems.length} problem${m.problems.length === 1 ? '' : 's'}.`, 4000);
      if (S.editor.stem !== m.script) openEditor(m.script);
      setTab('mods');
      S.bottomTab = 'probs';
    }
    if (S.tab === 'mods') renderMods();
    renderLive();
  } else if (m.t === 'files') {
    S.files.list = m.list;
    S.files.prefix = m.prefix;
    if (S.tab === 'files') renderFiles();
  } else if (m.t === 'file') {
    S.files.open = m;
    if (S.tab === 'files') renderFiles();
  } else if (SCENE_MSG[m.t]) {
    SCENE_MSG[m.t](m);
  } else if (m.t === 'picked') {
    if (m.animal >= 0) {
      S.sel.animal = m.animal;
      renderLive();
    }
  }
});

// (a game already running in Play - the page reloaded while it ran: its
// controls back, its window put back over the view)
// (once every script's loaded: recorder.js and memory.js come after this one)
window.addEventListener('load', async () => {
  const st = await lab.playState();
  if (!st || !st.running) return;
  S.play.running = true;
  S.play.kind = st.kind;
  if (S.tab === 'play') renderPlay();
  if (st.kind === 'original') {
    recPoll();
    livePoll();
  }
});
lab.onPlayExit(() => {
  S.play.running = false;
  if (S.tab === 'play') renderPlay();
});

window.addEventListener('resize', () => {
  if (S.tab === 'scene') sizeCanvas();
  if (S.tab === 'play') sendPlayRect();
});
// (it can fire before scene.js is in)
new ResizeObserver(() => S.tab === 'scene' && typeof sizeCanvas === 'function' && sizeCanvas()).observe($('stage'));
new ResizeObserver(() => S.tab === 'play' && sendPlayRect()).observe($('playarea'));

// (the screenshot test: a view, then an action there by name, arg after =)
lab.onTest(({ tab, act }) => {
  if (tab && tab !== S.tab) setTab(tab);
  if (act) {
    const [name, arg] = act.split('=');
    if (ACTIONS[name]) ACTIONS[name](arg);
  }
});

// ---------------------------------------------------------------- start
// (once every script is in: scene.js and control.js come after this one)
window.addEventListener('DOMContentLoaded', async () => {
  renderTop();
  await Promise.all([loadKb(), loadMods(), loadApi()]);
  loadGhidraFunctions(false);
  setTab(S.tab);
  const status = await lab.engineStart();
  if (status === 'connected') {
    // (already up - the page was reloaded: frames again, at this size)
    S.engine.status = 'connected';
    lastViewport = '';
    if (S.tab === 'scene') sizeCanvas();
    lab.engineSend({ cmd: 'frame' });
    setTab(S.tab);
  }
  renderTop();
  pollGhidra();
  setInterval(pollGhidra, 3000);
});
