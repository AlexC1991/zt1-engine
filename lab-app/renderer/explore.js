// ZT1 Lab: Explore - ask where anything is, and see what's missing.
//
// The original game has no source and no documentation: what's known of it
// lives in the knowledge base, the recordings, Ghidra and our engine's
// comments. re/lab/atlas.py joins them into one map (re/private/atlas.json):
// every function with its name, what it does, the text it uses, the actions
// it ran in and the engine code copying it; every config key, struct, class;
// every recorded action with how much of it our engine has. Here it's
// searched with plain words ("selling animals", "show tank", "cHungerCheck")
// - ranked locally (BM25: rarer words count more, the name most), nothing
// sent anywhere - and each answer says why it matched and where to go next.

const EX = { mode: store.get('exMode', 'actions'), fid: null, fidOpen: null, atlas: null, index: null, q: store.get('exQ', ''), results: [], open: null, filter: store.get('exFilter', 'all'), building: false };

const EX_STATUS = {
  copied: ['Copied from the original', 'green', 'Our engine cites at least half of the code that runs only for this'],
  traced: ['Partly traced', 'blue', 'Our engine cites some of the code that runs only for this'],
  untraced: ['Probably in our engine, not traced', 'amber', "Our engine's code uses this action's words, but cites none of the original's code for it: a guess until someone checks - Details"],
  unknown: ['Not found - check', 'redline', "Our engine's code doesn't use this action's words: a guess, maybe missing - open Details and check"],
  missing: ['Not in our engine', 'red', 'Checked by someone: our engine does not do this'],
  shared: ['No code of its own', 'muted', 'Everything that ran here runs for other things too'],
};
const EX_EXAMPLES = ['selling animals', 'show tank', 'where is the zoo saved', 'guest hunger', 'cHungerCheck', 'tank depth', 'month end wages', 'dinosaur escape'];

// ------------------------------------------------------------- the words
// (the same as atlas.py's: camelCase split, lower case, stemmed, the little
// words left out)
const EX_STOP = new Set('a an the of to in on and or for by with is it its at as from this that be when what where how which do does did are was were into then than there their them they you your i me my'.split(' '));
function exStem(w) {
  for (const suf of ['ings', 'ing', 'ies', 'ed', 'es', 's'])
    if (w.length > suf.length + 2 && w.endsWith(suf)) return w.slice(0, -suf.length) + (suf === 'ies' ? 'y' : '');
  return w;
}
function exTokens(text) {
  const out = [];
  for (const w of String(text || '').match(/[A-Za-z][A-Za-z0-9]*|\d+/g) || []) {
    const parts = /^\d+$/.test(w) ? [w] : w.split(/(?<=[a-z0-9])(?=[A-Z])|(?<=[A-Z])(?=[A-Z][a-z])/);
    for (const p0 of parts.length > 1 ? parts.concat([w]) : parts) {
      const p = p0.toLowerCase();
      if (p.length < 2 || EX_STOP.has(p)) continue;
      out.push(exStem(p));
    }
  }
  return out;
}

// --------------------------------------------------------------- the index
function exIndex(atlas) {
  const post = new Map();
  const lens = [];
  atlas.docs.forEach((d, i) => {
    const ts = (d.w || '').split(' ').filter(Boolean);
    lens.push(ts.length);
    const tf = new Map();
    for (const t of ts) tf.set(t, (tf.get(t) || 0) + 1);
    for (const [t, n] of tf) {
      if (!post.has(t)) post.set(t, []);
      post.get(t).push([i, n]);
    }
  });
  const avg = lens.reduce((a, b) => a + b, 0) / Math.max(1, lens.length);
  return { post, lens, avg, n: atlas.docs.length };
}
// The answers: BM25 over every entry, the query's words and their synonyms
// (worth half), an address asked for found at once
function exSearch(q) {
  const A = EX.atlas;
  const I = EX.index;
  if (!A || !I) return [];
  const addr = String(q).match(/0x[0-9a-f]{5,8}/i);
  const qt = exTokens(q);
  const weights = new Map();
  for (const t of qt) {
    weights.set(t, 1);
    for (const s of (A.synonyms || {})[t] || []) if (!weights.has(s)) weights.set(s, 0.5);
  }
  const score = new Map();
  const why = new Map();
  for (const [t, wq] of weights) {
    const p = I.post.get(t);
    if (!p) continue;
    const idf = Math.log(1 + (I.n - p.length + 0.5) / (p.length + 0.5));
    for (const [i, tf] of p) {
      const s = (wq * idf * (tf * 2.2)) / (tf + 1.2 * (0.25 + (0.75 * I.lens[i]) / I.avg));
      score.set(i, (score.get(i) || 0) + s);
      if (!why.has(i)) why.set(i, new Set());
      why.get(i).add(t);
    }
  }
  const kindBoost = { action: 1.35, key: 1.15, struct: 1.1, class: 1.1, engine: 0.95, function: 1 };
  const list = [...score.entries()].map(([i, s]) => {
    const d = A.docs[i];
    let b = kindBoost[d.kind] || 1;
    if (d.kind === 'function') b *= d.status === 'unknown' ? 0.6 : d.status === 'engine' ? 1.15 : 1;
    // (more of the question's words matched counts for more than one matched often)
    b *= 0.6 + (0.4 * why.get(i).size) / Math.max(1, qt.length);
    return { i, s: s * b, why: [...why.get(i)] };
  });
  list.sort((a, b) => b.s - a.s);
  if (addr) {
    const key = '0x' + parseInt(addr[0], 16).toString(16).padStart(6, '0');
    const at = A.docs.findIndex((d) => d.kind === 'function' && d.id === key);
    if (at >= 0) list.unshift({ i: at, s: 999, why: ['the address'] });
  }
  return list.slice(0, 60).map((r) => ({ ...r, d: A.docs[r.i] }));
}

// --------------------------------------------------------------- the view
async function exLoad(force) {
  if (EX.atlas && !force) return;
  EX.atlas = await lab.atlasLoad();
  EX.index = EX.atlas ? exIndex(EX.atlas) : null;
  if (EX.q) EX.results = exSearch(EX.q);
}

const exPill = (st) => {
  const s = EX_STATUS[st] || [st, 'muted', ''];
  return `<span class="ex-pill ${s[1]}" title="${esc(s[2])}">${esc(s[0])}</span>`;
};
const exFnStatus = { engine: ['In our engine', 'green'], named: ['Named', 'blue'], provisional: ['Named (provisional)', 'amber'], unknown: ['Not named yet', 'muted'] };

function exCard(r) {
  const d = r.d;
  const why = `<span class="muted ex-why" title="The words of your question it matched">matched: ${esc(r.why.join(', '))}</span>`;
  const go = (act, arg, label) => `<span class="btn small ghost" data-act="${act}" data-arg="${esc(arg)}">${label}</span>`;
  if (d.kind === 'function') {
    const st = exFnStatus[d.status] || ['', 'muted'];
    const lines = [];
    if (d.summary) lines.push(esc(d.summary));
    if (d.note) lines.push(`<span class="muted">${esc(d.note.slice(0, 220))}</span>`);
    if (d.ranIn && d.ranIn.length) lines.push(`<b>Ran when:</b> ${esc(d.ranIn.slice(0, 3).join(' · '))}${d.ranIn.length > 3 ? ` <span class="muted">(+${d.ranIn.length - 3})</span>` : ''}`);
    if (d.texts && d.texts.length) lines.push(`<b>Uses the text:</b> <span class="mono">${d.texts.slice(0, 5).map((t) => '“' + esc(t.slice(0, 50)) + '”').join(', ')}</span>`);
    if (d.debug && d.debug.length) lines.push(`<b>Its debug text names:</b> ${esc(d.debug.join(', '))}`);
    if (d.slots && d.slots.length) lines.push(`<b>In the class tables:</b> ${esc(d.slots.join(', '))}`);
    if (d.source) lines.push(`<b>Original source file:</b> <span class="mono">${esc(d.source)}</span>`);
    if (d.engine && d.engine.length) lines.push(`<b>Our engine:</b> ${d.engine.map((e) => `<span class="clickable blue mono" data-act="engOpen" data-arg="${esc(e)}">${esc(e)}</span>`).join(', ')}`);
    return `<div class="ex-card"><div class="ex-h"><span class="ex-kind">function</span><b class="mono">${esc(d.title)}</b> <span class="mono muted">${esc(d.id)}</span> <span class="ex-pill ${st[1]}">${st[0]}</span><span class="spacer"></span>${go('openCode', d.id, 'Open its code')}</div><div class="ex-b">${lines.join('<br>') || '<span class="muted">Nothing said about it yet.</span>'}</div>${why}</div>`;
  }
  if (d.kind === 'action') {
    return `<div class="ex-card"><div class="ex-h"><span class="ex-kind">recorded action</span><b>${esc(d.title)}</b> ${exPill(d.status)}<span class="spacer"></span>${go('exOpenAction', d.id, 'Details')}</div><div class="ex-b">${d.basis ? `${d.basis} functions run for this and little else; our engine cites ${d.inEngine} of them.` : 'Everything that ran here runs for other things too.'}</div>${why}</div>`;
  }
  if (d.kind === 'key') {
    const where = d.struct ? `<span class="mono">${esc(d.struct)} +${esc(d.offset)}</span>` : `global <span class="mono">${esc(d.global)}</span>`;
    return `<div class="ex-card"><div class="ex-h"><span class="ex-kind">config key</span><b class="mono">${esc(d.title)}</b> → ${where}<span class="spacer"></span>${go('exGameFiles', d.title, 'Which game files set it')}${go('exFinder', d.title, 'Where zoo.exe uses it')}</div><div class="ex-b">${esc(d.summary || '')}</div>${why}</div>`;
  }
  if (d.kind === 'struct' || d.kind === 'class') {
    const body = d.kind === 'struct' ? `${(d.fields || []).length} fields: <span class="mono">${esc((d.fields || []).slice(0, 8).join(', '))}</span>…` : `${d.slotCount} functions in its table (${esc(d.vtable || '')}). ${esc(d.summary || '')}`;
    return `<div class="ex-card"><div class="ex-h"><span class="ex-kind">${d.kind}</span><b class="mono">${esc(d.title)}</b></div><div class="ex-b">${esc(d.summary && d.kind === 'struct' ? d.summary : '')} ${body}</div>${why}</div>`;
  }
  if (d.kind === 'engine') {
    return `<div class="ex-card"><div class="ex-h"><span class="ex-kind">our engine</span><b class="mono">${esc(d.title)}</b><span class="spacer"></span>${go('engOpen', d.id, 'Open in Write Code')}</div><div class="ex-b">${esc(d.text.slice(0, 300))}${d.text.length > 300 ? '…' : ''}${d.cites && d.cites.length ? `<br><b>Copies zoo.exe's:</b> ${d.cites.slice(0, 6).map((c) => `<span class="clickable blue mono" data-act="openCode" data-arg="${c}">${c}</span>`).join(', ')}` : ''}</div>${why}</div>`;
  }
  return '';
}

// One action, opened: our engine's matching code, the functions that are
// its own (which our engine cites), and the recording itself
function exActionDetail(a) {
  const eng = (a.engineWords || []).length
    ? a.engineWords.map((e) => `<div><span class="clickable blue mono" data-act="engOpen" data-arg="${esc(e.at)}">${esc(e.at)}</span> <span class="muted">(${esc(e.words.join(', '))})</span><br><span class="muted">${esc(e.text)}</span></div>`).join('')
    : '<span class="muted">Nothing in our engine’s comments matches it.</span>';
  const fns = (a.top || []).map((f) => `<span class="ex-fn ${f.inEngine ? 'in' : ''}" data-act="openCode" data-arg="${f.address}" title="${f.inEngine ? 'Our engine cites it' : 'Not in our engine'}">${esc(f.name && !/^FUN_/.test(f.name) ? f.name : f.address)}</span>`).join(' ');
  const v = a.verdict;
  const checked = v
    ? `<div class="ex-verdict">Checked by ${esc(v.by)} (${esc(v.on)}): ${exPill(v.status)}${v.at ? ` at <span class="clickable blue mono" data-act="engOpen" data-arg="${esc(v.at)}">${esc(v.at)}</span>` : ''}${v.note ? ` - ${esc(v.note)}` : ''} <span class="clickable muted" data-act="exVerdict" data-arg="${esc(a.id)}|">clear</span></div>`
    : `<div class="ex-verdict"><span class="muted">Not checked yet - it's a guess from the words. Checked it?</span> <span class="btn small" data-act="exVerdict" data-arg="${esc(a.id)}|untraced|${esc((a.engineWords[0] || {}).at || '')}" title="Our engine does this (the closest code above, or say where)">Our engine does this</span> <span class="btn small" data-act="exVerdict" data-arg="${esc(a.id)}|missing|">Not in our engine</span></div>`;
  return `<div class="ex-detail">${checked}<div class="ex-sub">Our engine’s closest code (by its words - check it)</div>${eng}<div class="ex-sub">The ${a.basis} functions that run ${a.basisKind === 'own' ? 'only for this' : 'for this and at most two other things'} <span class="muted">(green: our engine cites it)</span></div><div>${fns || '<span class="muted">none</span>'}</div>${a.file ? `<div style="margin-top:6px"><span class="btn small" data-act="exRecording" data-arg="${esc(a.file)}">See everything that ran</span></div>` : ''}</div>`;
}

// The code 1:1 view: re/lab/fidelity.py's answer. The original's named
// gameplay by system (green: our engine cites it), and our own gameplay, UI
// and drawing functions by file (green: it says where it came from - a zoo.exe
// address, "measured:", "ours:" or "addition:")
function exFidelity() {
  const F = EX.fid;
  if (!F) return '<span class="muted">Reading our engine and the knowledge base…</span>';
  if (F.error) return `<span class="red">${esc(F.error)}</span>`;
  const bar = (n, of, title) => `<span class="ex-bar" title="${esc(title)}"><div style="width:${of ? Math.round((100 * n) / of) : 0}%"></div></span>`;
  const sys = Object.entries(F.original)
    .map(([name, v]) => {
      const n = v.ported.length, of = n + v.missing.length;
      const open = EX.fidOpen === 'o:' + name;
      const list = open
        ? `<div class="ex-detail"><div class="ex-sub">Not cited by our engine (${v.missing.length})</div><div>${v.missing.map((f) => `<span class="ex-fn" data-act="openCode" data-arg="${f.address}" title="${esc(f.summary)}">${esc(f.name)}${f.provisional ? '?' : ''}</span>`).join(' ') || '<span class="muted">none</span>'}</div><div class="ex-sub">Cited (${n})</div><div>${v.ported.map((f) => `<span class="ex-fn in" data-act="engOpen" data-arg="${esc((f.where || [''])[0])}:1" title="${esc((f.where || []).join(', '))}">${esc(f.name)}</span>`).join(' ') || '<span class="muted">none</span>'}</div></div>`
        : '';
      return `<div class="ex-act${open ? ' open' : ''}"><div class="ex-act-h clickable" data-act="exFidOpen" data-arg="o:${esc(name)}"><span>${esc(name)}</span><span class="spacer"></span><span class="muted">${n} / ${of}</span>${bar(n, of, n + ' of ' + of + ' cited by our engine')}</div>${list}</div>`;
    })
    .join('');
  const files = Object.entries(F.ours)
    .map(([file, v]) => {
      const all = Object.values(v.counts).reduce((a, b) => a + b, 0), un = v.counts.unmarked || 0;
      const open = EX.fidOpen === 'f:' + file;
      const kinds = ['copied', 'measured', 'ours', 'addition'].filter((k) => v.counts[k]).map((k) => `${k} ${v.counts[k]}`).join(' · ');
      const list = open
        ? `<div class="ex-detail"><div class="ex-sub">Says nothing about where it came from (${un})${kinds ? ` <span class="muted">- the rest: ${kinds}</span>` : ''}</div><div>${v.unmarked.map((f) => `<span class="ex-fn" data-act="engOpen" data-arg="${esc(file)}:${f.line}">${esc(f.name)}</span>`).join(' ') || '<span class="muted">none</span>'}</div>${(v.bare || []).length ? `<div class="ex-sub">Bare numbers - a number with a point on a line that doesn’t say where it’s from (${v.bare.length})</div><div>${v.bare.map((b) => `<span class="ex-fn mono" data-act="engOpen" data-arg="${esc(file)}:${b.line}" title="${esc(b.fn)}, line ${b.line}">${esc(b.number)}</span>`).join(' ')}</div>` : ''}</div>`
        : '';
      return `<div class="ex-act${open ? ' open' : ''}"><div class="ex-act-h clickable" data-act="exFidOpen" data-arg="f:${esc(file)}"><span class="mono">${esc(file.replace(/^src\//, ''))}</span> <span class="muted">${esc(v.kind)}</span><span class="spacer"></span>${(v.bare || []).length ? `<span class="muted" title="bare numbers">${v.bare.length} #</span> ` : ''}<span class="muted">${all - un} / ${all}</span>${bar(all - un, all, all - un + ' of ' + all + ' say where they came from')}</div>${list}</div>`;
    })
    .join('');
  const t = F.oursTotals, tot = Object.values(t).reduce((a, b) => a + b, 0);
  const ot = Object.values(F.originalTotals), on = ot.reduce((a, b) => a + b.ported, 0), of = ot.reduce((a, b) => a + b.ported + b.missing, 0);
  return `<div class="ex-intro muted">Our engine should be 1:1 with zoo.exe. Every function in our gameplay, UI and drawing code says where it came from in a comment: <span class="mono">zoo.exe 0x…</span> (copied), <span class="mono">measured:</span>, <span class="mono">ours:</span> or <span class="mono">addition:</span> (behind a Features switch). Click a name to open it.</div>
    ${typeof fxSection === 'function' ? fxSection() : ''}
    <div class="ex-area">The original’s gameplay, by system <span class="muted">- ${on} of ${of} named functions cited by our engine</span></div>${sys}
    <div class="ex-area">Our code, by file <span class="muted">- ${tot - (t.unmarked || 0)} of ${tot} functions say where they came from</span></div>${files}`;
}

function renderExplore() {
  const v = $('v-explore');
  if (!v) return;
  const A = EX.atlas;
  if (!A) {
    v.innerHTML = `<div class="ex-empty card"><h2>Explore</h2><p>Ask where anything is in the original game - selling animals, show tanks, a config key - and see what our engine is missing.</p><p class="muted">The map isn’t built yet: it joins the knowledge base, the recordings, Ghidra and our engine’s code. It takes about a minute (Ghidra should be up - the pill at the top).</p><div class="btn primary" data-act="atlasBuild">${EX.building ? 'Building…' : 'Build the map'}</div></div>`;
    return;
  }
  const results = EX.q
    ? EX.results.length
      ? EX.results.slice(0, 40).map(exCard).join('')
      : '<div class="muted" style="padding:12px">Nothing matched. Try other words - what the game calls it (“tank”, not “aquarium”), a config key (cHungerCheck), or an address (0x4378df).</div>'
    : `<div class="ex-intro"><p>Ask in plain words. Every answer says what it is, why it matched, and where to go next: its code in Original Game, the engine code that copies it, the game files that set a key.</p><p class="muted">Try: ${EX_EXAMPLES.map((e) => `<span class="chip clickable" data-act="exAsk" data-arg="${esc(e)}">${esc(e)}</span>`).join(' ')}</p></div>`;
  const groups = {};
  for (const a of A.actions) if (EX.filter === 'all' || a.status === EX.filter) (groups[a.area] = groups[a.area] || []).push(a);
  const totals = {};
  for (const a of A.actions) totals[a.status] = (totals[a.status] || 0) + 1;
  const filt = ['all', 'missing', 'unknown', 'untraced', 'traced', 'copied', 'shared']
    .map((k) => `<span class="chip clickable${EX.filter === k ? ' on' : ''}" data-act="exFilter" data-arg="${k}">${k === 'all' ? 'All' : EX_STATUS[k][0]} <span class="muted">${k === 'all' ? A.actions.length : totals[k] || 0}</span></span>`)
    .join(' ');
  const missing = Object.entries(groups)
    .map(
      ([area, list]) =>
        `<div class="ex-area">${esc(area)}</div>` +
        list
          .map((a) => {
            const open = EX.open === a.id;
            const bar = a.basis ? Math.round((100 * a.inEngine) / a.basis) : 0;
            return `<div class="ex-act${open ? ' open' : ''}"><div class="ex-act-h clickable" data-act="exOpenAction" data-arg="${esc(a.id)}">${exPill(a.status)} <span>${esc(a.title)}</span><span class="spacer"></span><span class="ex-bar" title="${a.inEngine} of ${a.basis} cited by our engine"><div style="width:${bar}%"></div></span></div>${open ? exActionDetail(a) : ''}</div>`;
          })
          .join(''),
    )
    .join('');
  v.innerHTML = `<div class="ex-wrap">
    <div class="ex-col ex-left card">
      <div class="ex-ask"><input id="ex-q" class="field" placeholder="Ask where anything is: selling animals, show tank, cHungerCheck, 0x4378df…" value="${esc(EX.q)}"><span class="btn primary" data-act="exAsk">Ask</span></div>
      <div class="ex-results">${results}</div>
    </div>
    <div class="ex-col ex-right card">
      <div class="ex-filters"><span class="chip clickable${EX.mode === 'actions' ? ' on' : ''}" data-act="exMode" data-arg="actions">Recorded actions</span> <span class="chip clickable${EX.mode === 'fidelity' ? ' on' : ''}" data-act="exMode" data-arg="fidelity">Code 1:1 with zoo.exe</span></div>
      ${EX.mode === 'fidelity' ? `<div class="ex-list">${exFidelity()}</div>` : `<div class="ex-title">What’s missing <span class="muted">- each action recorded in the original, against our engine</span></div>
      <div class="ex-filters">${filt}</div>
      <div class="ex-list">${missing || '<span class="muted">None like that.</span>'}</div>`}
      ${A.stale ? `<div class="ex-stale">Recorded or named something since the map was built: <span class="btn small primary" data-act="atlasBuild">${EX.building ? 'Building…' : 'Rebuild the map'}</span></div>` : ''}
      <div class="ex-foot muted">Map built ${esc(A.built.replace('T', ' '))} · ${A.counts.functions} functions, ${A.counts.actions} actions, ${A.counts.keys} keys, ${A.counts.engine} engine comments. Record more in Play › Play original, then <span class="clickable blue" data-act="atlasBuild">${EX.building ? 'building…' : 'rebuild the map'}</span>.</div>
    </div></div>`;
  const inp = $('ex-q');
  inp.onkeydown = (e) => {
    if (e.key === 'Enter') ACTIONS.exAsk(inp.value);
  };
}

Object.assign(ACTIONS, {
  // ("<question>"): the answers, shown - and, for the Lab's tools, the first ones in short
  exAsk: async (q) => {
    if (q === undefined && $('ex-q')) q = $('ex-q').value;
    EX.q = String(q || '').trim();
    store.set('exQ', EX.q);
    await exLoad();
    EX.results = EX.q ? exSearch(EX.q) : [];
    if (S.tab !== 'explore') setTab('explore');
    else renderExplore();
    return {
      q: EX.q,
      answers: EX.results.slice(0, 10).map((r) => ({ kind: r.d.kind, id: r.d.id, title: r.d.title, status: r.d.status, why: r.why, score: Math.round(r.s * 10) / 10 })),
    };
  },
  // ("actions" | "fidelity"): the right side's view
  exMode: async (m) => {
    EX.mode = m === 'fidelity' ? 'fidelity' : 'actions';
    store.set('exMode', EX.mode);
    if (S.tab !== 'explore') setTab('explore');
    renderExplore();
    if (EX.mode === 'fidelity') {
      EX.fid = await lab.fidelityLoad();
      renderExplore();
      return EX.fid && !EX.fid.error ? { original: EX.fid.originalTotals, ours: EX.fid.oursTotals } : EX.fid;
    }
    return { mode: EX.mode };
  },
  // ("o:<system>" | "f:<src file>"): open or close one row of the 1:1 view
  exFidOpen: (k) => {
    EX.fidOpen = EX.fidOpen === k ? null : k;
    renderExplore();
    if (k.startsWith('x:')) {
      const c = FX.results && FX.results.checks.find((x) => x.id === k.slice(2));
      return c ? { pass: c.pass, cases: c.cases, error: c.error, differ: c.rows.filter((r) => !r.ok).map((r) => `${r.in.join(',')}: zoo.exe ${r.zoo} ours ${r.ours}`) } : { error: 'not run yet' };
    }
    if (!EX.fid || EX.fid.error) return EX.fid;
    if (k.startsWith('o:')) {
      const v = EX.fid.original[k.slice(2)];
      return v ? { missing: v.missing.map((f) => `${f.address} ${f.name}`), cited: v.ported.map((f) => `${f.address} ${f.name}`) } : { error: 'no such system' };
    }
    const v = EX.fid.ours[k.slice(2)];
    return v ? { counts: v.counts, unmarked: v.unmarked.map((f) => `${f.name}:${f.line}`), bare: (v.bare || []).map((b) => `${b.number} at ${b.line} (${b.fn})`) } : { error: 'no such file' };
  },
  exFilter: (k) => {
    EX.filter = k;
    store.set('exFilter', k);
    renderExplore();
  },
  exOpenAction: (id) => {
    EX.open = EX.open === id ? null : id;
    renderExplore();
    const a = (EX.atlas.actions || []).find((x) => x.id === id);
    return a ? { title: a.title, status: a.status, own: a.basis, inEngine: a.inEngine, engine: (a.engineWords || []).map((e) => e.at) } : { error: 'no such action' };
  },
  exRecording: async (file) => showRecording(await lab.recLoad(file)),
  exGameFiles: (key) => {
    setTab('engine');
    gameFilesGo('has:' + key, null);
  },
  exFinder: (q) => {
    S.fview.q = q;
    setTab('functions');
  },
  // ("<action>|<status>|<file:line>|<note>"): a checked verdict (status
  // empty clears it), saved to re/lab/verdicts.json and shown at once
  exVerdict: async (arg) => {
    const [label, status, at, note] = String(arg).split('|');
    const r = await lab.atlasVerdict(label, status || null, at || '', note || '', S.me || '');
    const a = EX.atlas && EX.atlas.actions.find((x) => x.id === label);
    if (a) {
      if (!a.guess) a.guess = a.status;
      a.verdict = r.verdict;
      a.status = r.verdict ? r.verdict.status : a.guess;
      const d = EX.atlas.docs.find((x) => x.kind === 'action' && x.id === label);
      if (d) d.status = a.status;
    }
    renderExplore();
    return r;
  },
  // For the Lab's tools: the missing list in short ("<status>" to keep to one)
  exMissing: async (st) => {
    await exLoad();
    return { actions: (EX.atlas ? EX.atlas.actions : []).filter((a) => !st || a.status === st).map((a) => `${a.status} | ${a.area} | ${a.title} | ${a.inEngine}/${a.basis}`) };
  },
  atlasBuild: async () => {
    if (EX.building) return { busy: true };
    EX.building = true;
    renderExplore();
    toast('Building the map: the knowledge base, the recordings, Ghidra, our engine…', 4000);
    const r = await lab.atlasBuild();
    EX.building = false;
    if (r.error) {
      toast(esc(r.error), 8000);
      renderExplore();
      return r;
    }
    await exLoad(true);
    if (S.tab === 'explore') renderExplore();
    toast('The map is built.', 3000);
    return r;
  },
});
