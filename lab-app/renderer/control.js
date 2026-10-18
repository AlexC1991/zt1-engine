// The Lab driven by code (an AI agent through MCP, lab-app/mcp/server.js): each
// call does what the UI does - the same views change, the same buttons
// act - so it's all seen happening on screen, and nothing goes around the
// Lab's own tools.
// (what's picked, for an agent: its animations and behaviour sets
// counted - lists of 40 or 50 - unless asked for in full)
function trimSel(sel, full) {
  if (!sel || full) return sel;
  const { anims, sets, ...rest } = sel;
  return { ...rest, anims: Array.isArray(anims) ? anims.length : anims, sets: Array.isArray(sets) ? sets.length : sets };
}

const CONTROL = {
  // The shared knowledge base (re/cloud): what every install of the Lab has
  // worked out about the original zoo.exe. Reporting where it is pointed needs
  // no network; pull/contribute/pending go through re/cloud/sync.py.
  async kbCloud(a) {
    const status = await lab.kbCloud();
    if (!status || !status.configured) {
      return {
        configured: false,
        hint: 'The shared knowledge base endpoint is missing from re/cloud/config.json. A release includes it by default; ZT1_KB_API or re/private/cloud.json can override it. See re/cloud/README.md.',
      };
    }
    const what = String((a && a.what) || '').trim().toLowerCase();
    if (!what) return { ...status, actions: ['pull', 'contribute', 'pending'] };
    const r = await lab.kbCloudSync(what, !!(a && a.dry));
    return { ...status, what, ...r };
  },
  // Where things are: the view, the selection, the engine, the editor
  state() {
    const st = S.engine.state;
    const E = S.eng;
    return {
      tab: S.tab,
      expert: S.expert,
      engine: S.engine.status,
      ghidra: S.ghidra.ok ? S.ghidra.program : 'not connected',
      selectedFunction: S.sel.addr ? { ...fnInfo(S.sel.addr), kb: undefined, summary: (fnInfo(S.sel.addr).kb || {}).summary } : null,
      mod: S.mod,
      mods: S.mods.map((m) => ({ stem: m.stem, about: m.about, status: modStatus(m.stem)[0] })),
      labZoo: st
        ? {
            script: st.script,
            step: st.step,
            running: st.live && st.auto,
            checks: st.checks,
            watches: st.watches,
            animals: st.animals.filter((a) => a.lab).map((a) => ({ id: a.id, species: a.species, happiness: a.happiness, hunger: a.hunger })),
          }
        : null,
      engineCode: {
        file: E.file,
        open: E.opened.map((f) => ({ file: f, unsaved: CodeEditor.isDirty(f) })),
        cursor: { line: E.goto, col: E.col },
        intelligence: CodeEditor.lsp.status,
        build: { state: E.build.state, secs: E.build.secs, problems: E.build.problems, kind: E.build.kind, release: E.build.release, log: E.build.kind === 'release' ? E.build.log.slice(-25) : undefined },
      },
      play: { running: S.play.running },
      // (the page's own errors, newest last: why a view might be empty)
      pageErrors: PAGE_ERRORS.length ? PAGE_ERRORS.slice(-10) : undefined,
      // (the last species check, in short: lab_species_check has it all)
      speciesCheckProgress: SC.checkProgress || undefined,
      speciesCheck: S.speciesCheck ? { want: S.speciesCheck.want, species: (S.speciesCheck.species || []).map(checkSummary) } : undefined,
    };
  },
  // A species in bulk (or "*marine", "*"): its art, layers, behaviour sets
  // against what the engine does; shown in the Scene, returned whole
  async speciesCheck({ species, brief }) {
    setTab('scene');
    const m = await speciesCheck(species || '*marine');
    showCheck(m);
    return brief ? { want: m.want, species: (m.species || []).map(checkSummary) } : m;
  },
  // A tab by its id or its name ("Write Code"), or a part that has no tab of
  // its own any more: Game files (in Write Code), Functions (Original Game)
  tab({ tab }) {
    const t = String(tab || '').trim().toLowerCase();
    if (t === 'files' || t === 'game files') {
      gameFilesGo(GF.q, null);
      return { tab: S.tab, side: 'Game files' };
    }
    const OLD = { functions: 'code', 'engine code': 'engine', 'our code': 'engine' };
    const hit = TABS.find(([id, name]) => id === t || name.toLowerCase() === t) || TABS.find(([id]) => id === OLD[t]);
    if (!hit) return { error: `No tab "${tab}".`, tabs: TABS.map(([id, name]) => `${id} (${name})`).concat('files (Game files, in Write Code)') };
    setTab(hit[0]);
    return { tab: S.tab, name: hit[1] };
  },
  // Any button's action by name (see ACTIONS in app.js), with its argument
  action({ name, arg }) {
    if (!ACTIONS[name]) return { error: 'no action ' + name, actions: Object.keys(ACTIONS) };
    const r = ACTIONS[name](arg);
    // (what it gives back, if anything: a recording, what a scan found...)
    return Promise.resolve(r).then((v) => (v && typeof v === 'object' && !Array.isArray(v) ? { ok: true, ...v } : { ok: true }));
  },
  // A click on what's on screen: a CSS selector, or the first button-like
  // thing whose text is this
  click({ selector, text }) {
    let el = null;
    if (selector) el = document.querySelector(selector);
    else if (text) {
      // (the exact text first, anywhere clickable - the viewer's own buttons
      // too - so "SE" is the SE button, not "Make release")
      const t = text.trim().toLowerCase();
      const shown = (sel) => [...document.querySelectorAll(sel)].filter((e) => e.offsetParent !== null);
      // (list rows in a window - a recording, a reference - and a checkbox's
      // label too)
      el = shown('[data-act], [data-go], [data-gv], [data-file], .xref, label, .dbg, .btn, .tab, .item, .chip, .box, .trow, button').find((e) => e.textContent.trim().toLowerCase() === t) ||
        shown('[data-act], [data-go], [data-file], .xref, label, .btn, .tab, .item, .chip, .box, .trow').find((e) => e.textContent.trim().toLowerCase().includes(t));
    }
    if (!el) return { error: 'nothing like that on screen' };
    // (brought into view in its own list, but the page itself never moves:
    // the window has no scrolling of its own, and a shifted page hides the
    // top bar)
    const fixed = [];
    for (let p = el.parentElement; p; p = p.parentElement) {
      const o = getComputedStyle(p);
      const scrolls = /(auto|scroll)/.test(o.overflowY + o.overflowX) && p !== document.body && p !== document.documentElement;
      if (!scrolls) fixed.push([p, p.scrollTop, p.scrollLeft]);
    }
    el.scrollIntoView({ block: 'nearest', inline: 'nearest' });
    for (const [p, t, l] of fixed) {
      p.scrollTop = t;
      p.scrollLeft = l;
    }
    el.click();
    return { clicked: el.textContent.trim().slice(0, 80) };
  },
  // Typing into a box (a CSS selector): as if typed, its input event fires
  type({ selector, text }) {
    const el = document.querySelector(selector);
    if (!el) return { error: 'no ' + selector };
    el.focus();
    el.value = text;
    el.dispatchEvent(new Event('input', { bubbles: true }));
    // (a drop-down acts on change, as when one's picked)
    if (el.tagName === 'SELECT') el.dispatchEvent(new Event('change', { bubbles: true }));
    return { ok: true };
  },
  // What's shown: the visible text of the view (for reading results)
  // (a selector matching several - ".codebox .ln.hit", the lines Find lit -
  // gives each, one a line)
  read({ selector }) {
    const els = selector ? [...document.querySelectorAll(selector)] : [document.querySelector('#v-' + S.tab)].filter(Boolean);
    if (els.length > 1) return { count: els.length, lines: els.slice(0, 400).map((e) => (e.innerText || e.textContent).replace(/\s+/g, ' ').trim()) };
    return { text: els[0] ? els[0].innerText.slice(0, 20000) : '' };
  },

  // ---- Functions
  async searchFunctions({ query }) {
    setTab('functions');
    S.fview.q = query;
    S.fview.limit = 300;
    renderFunctions();
    // (field:0x3d6 - every instruction touching the offset, as the view lists them)
    if (/^field:/i.test(query.trim())) {
      const [off, size = ''] = query.trim().slice(6).trim().toLowerCase().split(/\s+/);
      const r = await lab.zooBin({ op: 'field', off, size });
      if (r.error) return { error: r.error };
      // (each hit's function, by Ghidra - as the view shows them)
      if (r.hits.length && S.ghidra.ok) {
        const o = await lab.ghidra('/owners?addrs=' + r.hits.map((h) => plainHex(h.at)).join(','));
        if (o && o.json && o.json.owners) r.owners = o.json.owners;
      }
      FIELD[off + ' ' + size] = r;
      renderFunctions();
      return {
        field: off,
        size,
        hits: r.hits.map((h) => {
          const o = r.owners && r.owners[plainHex(h.at)];
          const fa = r.owners ? (o ? hexKey(o.address) : null) : fnContaining(h.at);
          return { at: h.at, text: h.text, kind: h.kind, size: h.size, function: fa ? `${fa} ${fnInfo(fa).name}` : '(no function)' };
        }),
      };
    }
    // (value:10282 - every instruction using that number as itself)
    if (/^value:/i.test(query.trim())) {
      const value = query.trim().slice(6).trim();
      const r = await lab.zooBin({ op: 'value', value });
      if (r.error) return { error: r.error };
      if (r.hits.length && S.ghidra.ok) {
        const o = await lab.ghidra('/owners?addrs=' + r.hits.map((h) => plainHex(h.at)).join(','));
        if (o && o.json && o.json.owners) r.owners = o.json.owners;
      }
      VALUE[value] = r;
      renderFunctions();
      return {
        value: r.value,
        hits: r.hits.map((h) => {
          const o = r.owners && r.owners[plainHex(h.at)];
          const fa = r.owners ? (o ? hexKey(o.address) : null) : fnContaining(h.at);
          return { at: h.at, text: h.text, function: fa ? `${fa} ${fnInfo(fa).name}` : '(no function)' };
        }),
      };
    }
    if (query.trim().length >= 3 && S.ghidra.ok) await G(stringsQuery(query));
    renderFunctions();
    const q = query.toLowerCase();
    const names = allFunctions()
      .filter((f) => `${f.name} ${f.cls} ${f.addr} ${f.kb ? f.kb.summary || '' : ''}`.toLowerCase().includes(q))
      .slice(0, 50)
      .map((f) => ({ address: f.addr, name: f.name, system: f.system, status: f.status }));
    const t = gNow(stringsQuery(query));
    return { functions: names, text: t && t.json ? t.json.strings : [] };
  },
  async openFunction({ address, find }) {
    ACTIONS.openCode(address);
    const p = '/decompile?addr=' + plainHex(address);
    const f = '/function?addr=' + plainHex(address);
    const [d, m] = await Promise.all([G(p), G(f)]);
    const info = fnInfo(address);
    // (no function there: the raw instructions the view shows instead)
    let raw = null;
    if (d && d.json && !(d.json.c || '').trim()) {
      RAW[info.addr] = await lab.zooBin({ op: 'code', addr: info.addr });
      const r = RAW[info.addr];
      // (120 lines round the one asked for - it can be far into the code)
      const at = r.error ? -1 : r.lines.findIndex((x) => x.at === r.at);
      const from = Math.max(0, at - 40);
      raw = r.error
        ? r.error
        : { start: r.start, at: r.at, lines: r.lines.slice(from, from + 120).map((x) => `${x.at}  ${x.text}${x.str ? '  ; "' + x.str + '"' : ''}`) };
    }
    renderCode();
    // (find: typed into the Code panel's Find - the first line lit and in
    // view - and only the lines holding it come back, numbered)
    let found;
    if (find) {
      S.cfind = find;
      S.cfindAt = 0;
      if ($('c-find')) $('c-find').value = find;
      codeFind(true);
      const q = find.toLowerCase();
      const src = d && d.json ? d.json.c || '' : '';
      found = src
        .split(/\r?\n/)
        .map((t, i) => ({ line: i + 1, text: t.trim() }))
        .filter((x) => x.text.toLowerCase().includes(q));
    }
    return {
      address: info.addr,
      name: info.name,
      summary: info.kb ? info.kb.summary : '',
      ...(raw ? { raw } : {}),
      ...(found ? { found } : { c: d && d.json ? d.json.c : d && d.error }),
      calledBy: m && m.json ? (m.json.callers || []).map((x) => `${x.address} ${x.name}`) : [],
      calls: m && m.json ? (m.json.callees || []).map((x) => `${x.address} ${x.name}`) : [],
      inEngine: S.refs[info.addr] || [],
    };
  },
  async whereUsed({ address }) {
    await ACTIONS.xrefs(address);
    const r = gNow('/xrefs?addr=' + plainHex(address));
    return { refs: r && r.json ? r.json.refs : [] };
  },
  async nameFunction({ address, name, note, cls }) {
    S.sel.addr = hexKey(address);
    setTab('functions');
    $('fname').value = name;
    if (note !== undefined) $('fnote').value = note;
    if (cls && $('fclass')) $('fclass').value = cls;
    // (saved under the Lab's user, re/private/me.txt, as from the Lab itself)
    await saveFn();
    return { ok: true };
  },

  // ---- Game files
  // The game's files (Engine code's list): those starting with prefix, or
  // matching a smart query (words, type:lyt, in:mm, area:animals)
  async gameFiles({ prefix, q, limit }) {
    // (an engine still starting - after a build or a restart - is waited for)
    for (let i = 0; i < 300 && !GF.index; i++) {
      gfAskIndex();
      await new Promise((r) => setTimeout(r, 100));
    }
    if (!GF.index) return { error: S.engine.status === 'connected' ? 'the engine hasn’t sent its files' : 'the engine isn’t running (' + S.engine.status + ')' };
    // (typed into the list's search box, so the list shows what was asked)
    gameFilesGo(q || prefix || '', null);
    let hits;
    if (q) {
      hits = gfMatches();
      // (has:text - the engine looks inside them: waited for)
      const has = gfHas();
      for (let i = 0; has && i < 600 && !(GF.grep.q === has && GF.grep.hits); i++) await new Promise((r) => setTimeout(r, 100));
      if (has) hits = gfMatches();
    } else hits = GF.index.filter((f) => f.n.startsWith((prefix || '').toLowerCase()));
    const inside = GF.grep.hits;
    // (the first 40 unless asked for more: the count says how many in all)
    const max = Math.min(+limit || 40, 1000);
    return { count: hits.length, shown: Math.min(max, hits.length), files: hits.slice(0, max).map((f) => ({ name: f.n, from: f.f, ...(gfHas() && inside && inside.get(f.n) ? { lines: inside.get(f.n) } : {}) })) };
  },
  gameFilesOld({ prefix }) {
    setTab('engine');
    S.files.q = prefix;
    S.files.want = null;
    renderFiles();
    return new Promise((res) => setTimeout(() => res({ files: S.files.list || [] }), 700));
  },
  // One file: its text, or what it is (an animation's frames, a picture,
  // a palette, a sound) - and it opens where it would for you
  async gameFile({ name }) {
    // (through the list, as a click would be: its name in the search box,
    // found and opened there - nothing opens that the list doesn't show)
    gameFilesGo(name, null);
    await new Promise((r) => setTimeout(r, 400));
    await gfOpen(name, 'engine');
    const m = await gfView(name);
    const out = { ...m };
    if (out.png) out.png = Array.isArray(out.png) ? `${out.png.length} frames (as pictures)` : 'a picture';
    if (out.data) out.data = `${(out.size || 0).toLocaleString()} bytes`;
    if (out.text && out.text.length > 200000) out.text = out.text.slice(0, 200000) + '…';
    return out;
  },
  gameFileOld({ name }) {
    setTab('engine');
    lab.engineSend({ cmd: 'file', name });
    return new Promise((res) => setTimeout(() => res(S.files.open || { error: 'no answer' }), 700));
  },

  // ---- Engine code
  async searchEngine({ query }) {
    setTab('engine');
    // (on screen: Our code's list, with the words in its box)
    S.eng.left = 'code';
    store.set('engLeft', 'code');
    S.eng.q = query;
    S.eng.hits = await lab.srcGrep(query);
    renderEngine();
    return { hits: S.eng.hits };
  },
  // Type into an engine file at a line (and column) as at the keyboard, with
  // the suggestions list open: returns what the Lab suggests there (zoo.exe's
  // functions, fields, the game's keys) - clangd's own show in the editor too
  async engineSuggest({ file, line, col, text }) {
    await openEngineFile(file, line || 1);
    await new Promise((r) => setTimeout(r, 400));
    const t = CodeEditor.typeAt(file, line || 1, col, text || '');
    if (t.error) return t;
    const got = await CodeEditor.hooks.zoo.complete(t.before);
    await new Promise((r) => setTimeout(r, 600));
    return { typedBefore: t.before, suggestions: got ? got.items.slice(0, 15).map((i) => `${i.label}  [${i.desc}]  ${i.detail || ''}`) : [] };
  },
  async openEngineFile({ file, line }) {
    await openEngineFile(file, line || 1);
    await new Promise((r) => setTimeout(r, 300));
    return CodeEditor.lines(file, Math.max(1, (line || 1) - 20), (line || 1) + 40);
  },
  readEngineFile({ file, from, to }) {
    return CodeEditor.lines(file || S.eng.file, from, to) || { error: 'not open: openEngineFile first' };
  },
  async editEngineFile({ file, find, replace, all, insertAtLine, text }) {
    file = file || S.eng.file;
    if (!CodeEditor.isOpen(file)) await openEngineFile(file, 1);
    else if (S.eng.file !== file) await openEngineFile(file, S.eng.goto);
    const r = insertAtLine ? CodeEditor.insert(file, insertAtLine, text) : CodeEditor.replace(file, find, replace, all);
    refreshEngineChrome();
    return r;
  },
  // (before the Lab's started again: the Test Zoo's set-up kept)
  async keepRebuild() {
    const m = await keepRebuild();
    return { kept: m && m.cmds ? m.cmds.length : 0 };
  },
  async saveEngine() {
    await saveAllEngineFiles();
    refreshEngineChrome();
    return { ok: true };
  },
  problems({ file }) {
    return { markers: CodeEditor.markers(file || S.eng.file) };
  },
  // Sync: the repo's state, or a step (pull, commit, push, propose, mirror,
  // resolve, branch) - shown in Engine code's Sync tab as it's done
  async syncStatus({ fetch }) {
    setTab('engine');
    ACTIONS.engBottom('sync');
    await syncRefresh(fetch);
    return SY.st;
  },
  async syncStep({ step, message, title, body, file, how, name }) {
    setTab('engine');
    ACTIONS.engBottom('sync');
    const args = { pull: {}, abort: {}, push: {}, mirror: {}, commit: { message }, propose: { title, body }, resolve: { file, how }, branch: { name } }[step];
    if (!args) return { error: 'step: pull, commit, push, propose, mirror, resolve, abort or branch' };
    const r = await syncDo(step, args, step + '…');
    return { ...r, status: SY.st && { branch: SY.st.branch, conflicts: SY.st.conflicts, changed: SY.st.files.length, pr: SY.st.pr } };
  },
  // Make release: x64 Release build + dist/ zip, as the Alpha was made
  async makeRelease() {
    setTab('engine');
    await ACTIONS.engRelease();
    return { started: true };
  },
  async buildEngine() {
    setTab('engine');
    await ACTIONS.engBuild();
    return { started: true };
  },

  // ---- Mods and the lab zoo
  async runMod({ name }) {
    pickMod(name);
    await runMod(name);
    setTab('scene');
    return { ok: true };
  },
  engineLog({ lines }) {
    return { log: S.engine.log.slice(-(lines || 60)) };
  },

  // ---- Scene: the lab zoo as an editor (scene.js); tiles are the map's
  // x, y (the hover readout shows them)
  async scene() {
    await showScene();
    await settle();
    const st = S.engine.state;
    return st ? { view: st.view, playing: st.playing, ...st.scene, labels: undefined, outline: outlineSummary() } : { error: 'the engine isn’t running' };
  },
  async sceneMaps() {
    await showScene();
    if (!SC.maps) lab.engineSend({ cmd: 'maps' });
    for (let i = 0; i < 30 && !SC.maps; i++) await settle(100);
    return { maps: SC.maps || [] };
  },
  async sceneLoad({ map, flat }) {
    if (!(await showScene())) return { error: 'the engine isn’t running' };
    const was = (sc() || {}).loads;
    lab.engineSend({ cmd: 'load', map, flat: !!flat });
    for (let i = 0; i < 100; i++) {
      await settle(200);
      const s = sc();
      if (s && s.loads !== was && s.map === map && s.flat === !!flat) {
        await settle(300);
        return { ok: true, map: s.mapName, flat: s.flat, counts: sc().counts };
      }
    }
    return { error: 'the map didn’t load (see lab_engine_log)' };
  },
  async sceneOutline({ full } = {}) {
    await showScene();
    SC.outline = null;
    lab.engineSend({ cmd: 'outline' });
    for (let i = 0; i < 30 && !SC.outline; i++) await settle(100);
    const o = SC.outline;
    if (!o || full) return o || { error: 'no answer' };
    // (guests and scenery counted, not listed: hundreds of each)
    return { ...o, guests: undefined, scenery: (o.scenery || []).map((g) => ({ file: g.file, name: g.name, count: (g.i || []).length })) };
  },
  // What can be put down: the palette's items (file, name, cost), by its tab
  async sceneItems({ category, query }) {
    await showScene();
    if (!SC.catalog) lab.engineSend({ cmd: 'catalog' });
    for (let i = 0; i < 50 && !SC.catalog; i++) await settle(100);
    // (read from the palette's own list - its tab and search - and the
    // palette put back as it was: open or shut, on its tab, with its words)
    const was = { cat: SC.palette.cat, q: SC.palette.q };
    if (category) SC.palette.cat = category;
    SC.palette.q = query || '';
    const items = paletteItems().slice(0, 300).map((x) => ({ item: x.key, name: x.name, cost: x.cost }));
    const cat = SC.palette.cat;
    SC.palette.cat = was.cat;
    SC.palette.q = was.q;
    return { category: cat, categories: PALETTE.map((p) => p[0]), items };
  },
  // A tool used at a tile (or dragged from one tile to another): an item
  // from sceneItems, or select / bulldoze / gate
  async scenePlace({ item, tool, x, y, toX, toY, female, facing, size, lineMode }) {
    await showScene();
    if (!SC.catalog) {
      lab.engineSend({ cmd: 'catalog' });
      for (let i = 0; i < 50 && !SC.catalog; i++) await settle(100);
    }
    if (female !== undefined) SC.opts.female = !!female;
    if (size) SC.opts.size = size;
    // (an item it doesn't know: said, with the ones it might mean - a short
    // name like dzbar finds fences/dzbar.ai - not a drag with nothing in hand)
    if (item && !/^(paint|height):/.test(item) && !itemOf(item)) {
      const q = item.toLowerCase();
      const like = (SC.catalog || []).filter((i) => i.file.toLowerCase().includes(q) || (i.name || '').toLowerCase().includes(q));
      if (like.length === 1) item = like[0].file;
      else return { error: `No item "${item}".` + (like.length ? ' Did you mean: ' + like.slice(0, 12).map((i) => `${i.file} (${i.name})`).join(', ') : ' lab_scene_items lists them.') };
    }
    if (item) pickItem(item);
    else sendTool({ kind: tool || 'select' });
    if (item && facing && toolKindOf(itemOf(item)) === 'object') sendTool({ ...(sc() || {}).tool, kind: 'object', item, facing });
    await settle(150);
    // (a fence or path drag's mode: Bend, Straight, Rectangle...)
    for (let i = 0; lineMode && i < 4 && ((sc() || {}).tool || {}).lineMode !== lineMode; i++) {
      lab.engineSend({ cmd: 'lineMode', step: 1 });
      await settle(200);
    }
    const before = S.engine.log.length;
    // fences go from grid point to grid point: x, y is a tile's corner
    const exact = item && toolKindOf(itemOf(item)) === 'fence';
    if (x !== undefined) {
      const ev = (tool || 'select') === 'select' && !item ? 'click' : 'down';
      lab.engineSend({ cmd: 'mouseTile', ev, tx: x, ty: y, exact });
      if (ev === 'down') {
        if (toX !== undefined) {
          // (the drag, a tile at a time, as a mouse would go)
          const n = Math.max(Math.abs(toX - x), Math.abs(toY - y));
          for (let i = 1; i <= n; i++) {
            lab.engineSend({ cmd: 'mouseTile', ev: 'move', tx: Math.round(x + ((toX - x) * i) / n), ty: Math.round(y + ((toY - y) * i) / n), exact });
            await settle(30);
          }
        }
        lab.engineSend({ cmd: 'mouseTile', ev: 'up', tx: toX !== undefined ? toX : x, ty: toY !== undefined ? toY : y, exact });
      }
    }
    await settle(400);
    const s = sc() || {};
    return { tool: s.tool, selected: trimSel(s.sel), counts: s.counts, said: S.engine.log.slice(before) };
  },
  async sceneSelect({ kind, id, x, y, full }) {
    await showScene();
    if (kind === 'tile' && x !== undefined) lab.engineSend({ cmd: 'select', kind: 'tile', x, y, focus: true });
    else if (x !== undefined) {
      sendTool({ kind: 'select' });
      lab.engineSend({ cmd: 'mouseTile', ev: 'click', tx: x, ty: y });
    } else {
      // (picking something puts the tool in hand down, as a click would)
      sendTool({ kind: 'select' });
      ACTIONS.scSelect(`${kind}:${id}`);
    }
    await settle(400);
    // (its animations and behaviour sets only when asked: lists of 40-50)
    const sel = (sc() || {}).sel;
    if (!sel || full) return { selected: sel };
    const { anims, sets, ...rest } = sel;
    return { selected: { ...rest, anims: anims ? anims.length : undefined, sets: sets ? sets.length : undefined } };
  },
  async sceneSet({ field, value }) {
    lab.engineSend({ cmd: 'set', field, value });
    await settle(400);
    const sel = (sc() || {}).sel;
    if (!sel) return { selected: sel };
    const { anims, sets, ...rest } = sel;
    return { selected: { ...rest, anims: anims ? anims.length : undefined, sets: sets ? sets.length : undefined } };
  },
  async sceneRemove() {
    ACTIONS.scRemove();
    await settle(400);
    return { selected: (sc() || {}).sel, counts: (sc() || {}).counts };
  },
  // The Watch panel: follow an animal (id, on) or let it go, clear the log;
  // returns the log's rows (game time, who, what it's doing, where)
  async sceneWatch({ id, on, clear, last }) {
    await showScene();
    if (typeof showPanel === 'function') showPanel('watch');
    if (clear) ACTIONS.watchClear();
    if (id !== undefined) lab.engineSend({ cmd: 'watch', id, on: on !== false });
    if (clear || id !== undefined) await settle(300);
    else lab.engineSend({ cmd: 'watch' }), await settle(300);
    return { watching: WT.watching, rows: WT.rows.slice(-(last || 80)).map((r) => `${wtClock(r.t)}  ${r.name}: ${r.doing}${r.doing === '(gone)' ? '' : ` @${r.x.toFixed(1)},${r.y.toFixed(1)}`}`) };
  },
  async sceneTime({ play, speed, advance }) {
    await showScene();
    if (speed) ACTIONS.scSpeed(speed);
    if (play !== undefined) lab.engineSend({ cmd: 'play', on: !!play });
    // (a skip: waited for, till the engine says it's done)
    let done = null;
    if (advance) {
      done = new Promise((r) => {
        SC.skipWait = r;
        setTimeout(r, 10 * 60 * 1000);
      });
      ACTIONS.scAdvance(advance);
    }
    if (done) await done;
    await settle(400);
    const st = S.engine.state;
    return { playing: st.playing, speed: st.scene.speed, clock: st.scene.clock };
  },
  // Engine code: who calls the function at file:line (or the one it's in),
  // or what it calls, by clangd; depth levels of callers opened
  async engineCalls({ file, line, dir, depth }) {
    await ACTIONS.engCalls(`${file}:${line || 1}:${dir === 'out' ? 'out' : 'in'}`);
    const flat = (c, d) => {
      const kids = CALLS.kids.get(callKey(c)) || [];
      return { name: c.name, at: c.rel ? `${c.rel}:${c.line}` : `${c.abs}:${c.line}`, times: c.times || undefined, ...(d > 0 && kids.length ? { calls: kids.map((k) => flat(k, d - 1)) } : {}) };
    };
    if (!CALLS.root) return { error: CALLS.error || 'nothing' };
    // (opened a level at a time, as clicking would)
    const open = async (c, d) => {
      if (d <= 0) return;
      for (const k of CALLS.kids.get(callKey(c)) || []) {
        CALLS.open.add(callKey(k));
        await loadCalls(k);
        await open(k, d - 1);
      }
    };
    await open(CALLS.root, Math.min(3, (depth || 1) - 1));
    renderEngineBuild();
    return { dir: CALLS.dir, of: flat(CALLS.root, Math.min(4, depth || 1)) };
  },
  async sceneView({ overlay, on, mode, rotate, zoom, species, anim, layers, frame, step }) {
    await showScene();
    // (as typed: a tool whose schema is older passes these as text)
    layers = layers === true || layers === 'true';
    frame = frame === undefined || frame === '' ? undefined : Number(frame);
    step = Number(step) || 0;
    if (overlay) lab.engineSend({ cmd: 'overlay', name: overlay, on: on !== false });
    // (the art viewer: a species' animation by name, its layers together)
    if (anim || species) {
      SC.viewLayered = !!layers;
      const sp = species || ((S.engine.state || {}).what || 'zebra').split('|')[0];
      lab.engineSend(anim ? { cmd: 'view', mode: 'anim', what: `${sp}|${anim}|${layers ? 1 : 0}` } : { cmd: 'view', mode: 'animal', what: sp });
    } else if (mode) ACTIONS.view(mode);
    if (rotate) ACTIONS.rotate();
    if (zoom) ACTIONS.scZoom(zoom);
    // (the art viewer held on a frame (-1 plays), or stepped on/back)
    if (step) lab.engineSend({ cmd: 'artFrame', step });
    else if (frame !== undefined) lab.engineSend({ cmd: 'artFrame', value: frame });
    await settle(300);
    const st = S.engine.state;
    return { view: st.view, what: st.what, overlays: st.scene.overlays };
  },
  // A route search with the Route tool: as a guest or staff from x, y to
  // toX, toY, or as the picked animal from where it stands to x, y
  async sceneRoute({ as, x, y, toX, toY }) {
    await showScene();
    const kind = as || 'guest';
    SC.routeKind = kind;
    lab.engineSend({ cmd: 'tool', kind: 'route', item: kind });
    await settle(200);
    if (kind !== 'animal') {
      lab.engineSend({ cmd: 'mouseTile', ev: 'down', tx: x, ty: y });
      await settle(150);
      lab.engineSend({ cmd: 'mouseTile', ev: 'down', tx: toX, ty: toY });
    } else lab.engineSend({ cmd: 'mouseTile', ev: 'down', tx: x, ty: y });
    await settle(500);
    const s = sc() || {};
    return (s.routeTest && s.routeTest.search) || { error: kind === 'animal' && (!s.sel || s.sel.kind !== 'animal') ? 'pick an animal first' : 'no answer' };
  },

  // An action on what's selected (the inspector's buttons): animal: anim
  // <name>, behave <set>, hungry, tired, sick, heal, happy, unhappy, dung,
  // dart, crate; guest: anim, hungry, thirst, bathroom, tired, leave; staff:
  // anim; exhibit: food, nofood, dung, wear, break, dirty (the console's)
  async sceneAct({ action, arg }) {
    await showScene();
    const before = S.engine.log.length;
    lab.engineSend({ cmd: 'act', action, arg: arg === undefined ? '' : String(arg) });
    await settle(400);
    const s = sc() || {};
    return { selected: s.sel, said: S.engine.log.slice(before) };
  },
  // Pin a value of what's selected (held there every update), or let it go
  async scenePin({ field, value, on }) {
    await showScene();
    const s = sc() || {};
    const v = value !== undefined ? value : s.sel ? s.sel[field] : 0;
    lab.engineSend({ cmd: 'pin', field, value: v, on: on !== false });
    await settle(300);
    return { pins: (sc() || {}).pins };
  },

  // ---- Stepping: the engine's code as it runs (the Scene's Steps dock)
  async sceneTrace({ mode, stepEach, breakpoint, on }) {
    await showScene();
    if (mode) ACTIONS.dkTrace(mode);
    if (stepEach !== undefined) lab.engineSend({ cmd: 'stepEach', on: !!stepEach });
    if (breakpoint) {
      const want = on !== false;
      if (DK.bps.has(breakpoint) !== want) ACTIONS.dkBp(breakpoint);
    }
    if (!S.consoleOpen || DK.tab !== 'steps') ACTIONS.dkTab('steps');
    await settle(300);
    return { trace: (sc() || {}).trace, breakpoints: [...DK.bps] };
  },
  // Go on from a stop (step, stepOver, stepOut, continue), or stop at the
  // next step; waits for where it stops (or that it ran on)
  async sceneStep({ how }) {
    await showScene();
    const was = DK.steps.length ? DK.steps[DK.steps.length - 1].n : 0;
    const cmd = { step: 'stepNext', next: 'stepNext', over: 'stepOver', stepOver: 'stepOver', out: 'stepOut', stepOut: 'stepOut', continue: 'continue' }[how || 'step'] || 'stepNext';
    lab.engineSend({ cmd });
    for (let i = 0; i < 40; i++) {
      await settle(150);
      if (DK.paused && DK.paused.n > was) return { stopped: stepSummary(DK.paused) };
    }
    return DK.paused ? { stopped: stepSummary(DK.paused) } : { running: true, note: 'not stopped (nothing traced came yet: is the zoo running, or something picked?)' };
  },
  sceneSteps({ last, who, q }) {
    let list = DK.steps;
    if (who) list = list.filter((x) => x.whoName.toLowerCase().includes(who.toLowerCase()));
    // (q: words in what it did - "catches", "prey" - typed into the Steps
    // filter too, so the screen shows the same)
    if (q) {
      const t = q.toLowerCase();
      list = list.filter((x) => JSON.stringify(stepSummary(x)).toLowerCase().includes(t));
      if ($('dk-q')) {
        $('dk-q').value = q;
        $('dk-q').dispatchEvent(new Event('input'));
      }
    }
    return { stopped: DK.paused ? stepSummary(DK.paused) : null, steps: list.slice(-(last || 30)).map(stepSummary) };
  },

  // ---- Try zoo.exe: a function of the original run on what's picked
  async tryFunction({ address, struct, calls, fill }) {
    await showScene();
    if (struct) TRY.struct = struct;
    if (calls) {
      TRY.calls = calls;
      store.set('tryCalls', calls);
    }
    if (fill && fill.struct && fill.offset !== undefined) {
      TRY.fills[fill.struct] = TRY.fills[fill.struct] || {};
      const off = '0x' + Number(fill.offset).toString(16);
      if (fill.value === null || fill.value === undefined) delete TRY.fills[fill.struct][off];
      else TRY.fills[fill.struct][off] = fill.value;
      store.set('tryFills', TRY.fills);
    }
    await tryRun(address || TRY.addr);
    const r = TRY.res;
    if (!r) return { error: (sc() || {}).sel ? 'nothing ran' : 'pick something in the Scene first (lab_scene_select)' };
    const hx = (n) => '0x' + (n >>> 0).toString(16);
    return {
      function: fnInfo(TRY.addr).name,
      ranOn: TRY.ranOn,
      ok: r.ok,
      returned: r.returned ? r.eax : null,
      instructions: r.count,
      missing: (r.errors || []).map((e) => (e.kind === 'unmapped' ? `${e.struct}+${hx(e.off)} (at ${hx(e.at)})` : `${e.text} (at ${hx(e.at)})`)),
      used: (r.touched || [])
        .filter((t) => t.known)
        .map((t) => `${t.name || hx(t.off)} ${t.writes ? `${t.first} -> ${t.last}` : `= ${t.first}`}`),
      text: (r.strings || []).map((x) => x.text),
      calls: (r.calls || []).map((c) => `${c.name} x${c.count} (${c.why})`),
    };
  },

  // The game's dev console (help lists its commands)
  async sceneConsole({ command }) {
    await showScene();
    const n = S.engine.log.length;
    lab.engineSend({ cmd: 'console', line: command });
    for (let i = 0; i < 30; i++) {
      await settle(100);
      const at = S.engine.log.indexOf('› ' + command, n);
      if (at >= 0) return { text: S.engine.log.slice(at + 1).join('\n') };
    }
    return { error: 'no answer' };
  },

  // ---- Play
  async play({ start, game, shot }) {
    setTab('play');
    // (shot: a picture of the game in Play, ours or the original, as it's
    // drawn, saved at that .png)
    if (shot) return lab.playShot(shot);
    if (start && !S.play.running) await (game === 'original' ? ACTIONS.playOriginal() : ACTIONS.playStart());
    // (only start: false stops it - no start at all just asks what's running)
    if (start === false && S.play.running) await ACTIONS.playStop();
    return { running: S.play.running, game: S.play.running ? S.play.kind : null };
  },
};

// A step, for reading: where, whose, what, its values (before -> after)
function stepSummary(x) {
  return {
    n: x.n,
    time: x.time,
    who: x.whoName,
    what: x.what,
    at: `${x.file}:${x.line}`,
    fn: x.fn,
    zoo: x.zoo || (x.stack || []).map((f) => f.zoo).filter(Boolean).pop() || '',
    values: Object.fromEntries((x.vals || []).map((v) => [v.n, v.s !== undefined ? v.s : v.f !== undefined ? `${v.f} -> ${v.v}` : v.v])),
    stack: (x.stack || []).map((f) => `${f.fn}${f.what ? ' (' + f.what + ')' : ''} ${f.file}:${f.line}`),
    paused: x.paused,
  };
}

function showScene() {
  if (S.tab !== 'scene') setTab('scene');
  return engineUp();
}
// (the engine restarting - after a build - takes a few seconds: what's sent
// before it's listening is lost, so the scene's calls wait for it)
async function engineUp() {
  for (let i = 0; i < 150 && (S.engine.status !== 'connected' || !sc()); i++) await settle(200);
  return S.engine.status === 'connected';
}
const settle = (ms = 250) => new Promise((r) => setTimeout(r, ms));
function outlineSummary() {
  const o = SC.outline;
  if (!o) return null;
  return {
    exhibits: o.exhibits,
    animals: o.animals,
    staff: o.staff,
    guests: o.guestCount,
    buildings: o.buildings.length,
    sceneryKinds: o.scenery.length,
  };
}

lab.onControl(async ({ id, method, params }) => {
  try {
    if (!CONTROL[method]) return lab.controlResult({ id, error: 'no call ' + method + '; there are: ' + Object.keys(CONTROL).join(', ') });
    const result = await CONTROL[method](params || {});
    lab.controlResult({ id, result });
  } catch (e) {
    lab.controlResult({ id, error: String(e && e.stack ? e.stack : e) });
  }
});

// (the same as a button action: "file|line|col|text" - col may be empty)
Object.assign(ACTIONS, {
  engSuggest: (arg) => {
    const [file, line, col, ...rest] = String(arg).split('|');
    return CONTROL.engineSuggest({ file, line: +line, col: col ? +col : undefined, text: rest.join('|') });
  },
});
