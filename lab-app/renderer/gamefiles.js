// ZT1 Lab: the game's files, as a list on the left of Mods and Engine code
// (its own tab before). A smart filter: words anywhere in the path, all of
// them; type:lyt, in:mm, area:animals; and chips for each kind, with counts.
// Text (.ai, .cfg, .lyt…) opens in the middle, read-only, in the script
// viewer there; art and sound open in a viewer of their own: an animation
// playing (its directions, its frames), a picture, a palette's colours, a
// sound, anything else as bytes. The engine reads them all - the same files
// it plays with (an expansion's or update's copy over the base game's).
// (loaded after app.js)

const GF = {
  index: null, // [[name, from], …] - every file, from the engine
  asked: 0,
  q: store.get('gfQ', ''),
  chips: store.get('gfChips', { type: '', where: '', area: '' }),
  els: {}, // host -> its element (kept: typing never rebuilds the box)
  waits: new Map(), // file -> what's waiting for its view
  timer: null,
  refs: {}, // file -> where our engine's code names it
  grep: { q: '', hits: null }, // has:text - the engine's answer (file -> its lines), null while it looks
};
// (the old Game files tab: its place is Engine code's list now)
if (store.get('tab', 'scene') === 'files') store.set('tab', 'engine');

const GF_TYPES = [
  ['lyt', 'Layouts', /\.lyt$/],
  ['cfg', 'Configs', /\.(cfg|ini)$/],
  ['ai', 'Species & objects', /\.(ai|uca|ucb|ucs)$/],
  ['ani', 'Animations', /\.ani$/],
  ['img', 'Pictures', /\.(tga|bmp|png|jpg|pcx|gif)$/],
  ['pal', 'Palettes', /\.pal$/],
  ['wav', 'Sounds', /\.(wav|mp3|ogg)$/],
  ['txt', 'Text', /\.(txt|rtf|htm|html|lle)$/],
  ['map', 'Maps & scenarios', /\.(zoo|scn)$/],
  ['frm', 'Frames (art data)', /\/[^./]+$/],
  ['etc', 'Other', /./],
];
function gfType(name) {
  for (const [k, , re] of GF_TYPES) if (re.test(name)) return k;
  return 'etc';
}
function gfWhere(from) {
  const f = (from || '').toLowerCase();
  if (/^xpack1[\\/]|\/xpack1\//.test(f) || f.startsWith('xpack1')) return 'dd';
  if (f.startsWith('xpack2')) return 'mm';
  if (/^(zupdate1?|dupdate|updates|dlupdate)[\\/]/.test(f)) return 'patch';
  if (/^loc[\\/]/.test(f)) return 'loc';
  // (the game's own files are in its folder, ".", or one of the folders
  // above; anything else came from a mod's folder - "Fastsheep/....ztd")
  if (!/^\.[\\/]/.test(f) && /[\\/]/.test(f)) return 'mod';
  return 'base';
}
const GF_WHERE = [
  ['mod', 'Mods'],
  ['base', 'Base game'],
  ['patch', 'Patches'],
  ['dd', 'Dinosaur Digs'],
  ['mm', 'Marine Mania'],
  ['loc', 'Language'],
];
const TEXT_KINDS = new Set(['lyt', 'cfg', 'ai', 'txt']);

function gfAskIndex() {
  if (GF.index || Date.now() - GF.asked < 4000 || S.engine.status !== 'connected') return;
  GF.asked = Date.now();
  lab.engineSend({ cmd: 'filesIndex' });
}
SCENE_MSG.filesIndex = (m) => {
  GF.index = m.list.map(([n, f]) => ({ n, f, t: gfType(n), w: gfWhere(f), a: n.split('/')[0] }));
  for (const h of Object.keys(GF.els)) gfRender(h);
};
SCENE_MSG.fileView = (m) => {
  const w = GF.waits.get(m.name);
  if (w) {
    GF.waits.delete(m.name);
    w(m);
  }
};
// The game's text by number - a message id read in zoo.exe's code ("10007"
// is "%s has escaped."). The engine looks it up in the lang dlls
const TEXTS = { wait: null };
SCENE_MSG.texts = (m) => {
  const w = TEXTS.wait;
  TEXTS.wait = null;
  if (w) w(m.items);
};
ACTIONS.gameText = (arg) =>
  new Promise((resolve) => {
    // ("10287 0x2972 10287-10290": numbers, hex, ranges)
    const ids = [];
    for (const p of String(arg).trim().split(/[\s,]+/).filter(Boolean)) {
      const [a, b] = p.split('-').map((x) => parseInt(x, /^0x/i.test(x) ? 16 : 10));
      for (let i = a; i <= (b >= a ? Math.min(b, a + 400) : a); i++) ids.push(i);
    }
    if (!ids.length || S.engine.status !== 'connected') return resolve({ error: ids.length ? 'The engine isn’t running.' : 'Which numbers?' });
    TEXTS.wait = (items) => resolve({ texts: items.filter(([, t]) => t).map(([id, t]) => ({ id, hex: '0x' + id.toString(16), text: t })), empty: items.filter(([, t]) => !t).length });
    lab.engineSend({ cmd: 'texts', ids });
    setTimeout(() => TEXTS.wait && ((TEXTS.wait = null), resolve({ error: 'no answer' })), 15000);
  });
// UI elements by number ("0x11cb 7000"): which layout and section each is
ACTIONS.uiElement = async (arg) => {
  const nums = String(arg).split(/[\s,]+/).filter(Boolean).map((x) => parseInt(x, /^0x/i.test(x) ? 16 : 10));
  const r = await lab.uiIds(nums);
  if (r.error) return r;
  return { elements: nums.map((n) => ({ id: n, hex: '0x' + n.toString(16), element: r.elements[n] || 'not in any layout' })) };
};
function gfView(name, dir) {
  return new Promise((resolve) => {
    GF.waits.set(name, resolve);
    lab.engineSend({ cmd: 'fileView', name, dir: dir || '' });
    setTimeout(() => GF.waits.get(name) === resolve && (GF.waits.delete(name), resolve({ name, kind: 'missing', error: 'no answer' })), 30000);
  });
}

// has:text - inside the files: the text files with a line holding it (spaces
// and case don't count: has:cpreattack=1 finds "cPreattack = 1"). The engine
// looks, the same files it plays with; null until it answers
function gfHas() {
  const m = /(?:^|\s)has:(\S+)/i.exec(GF.q);
  return m ? m[1].toLowerCase() : '';
}
function gfGrep(text) {
  if (GF.grep.q === text) return GF.grep.hits;
  GF.grep = { q: text, hits: null };
  lab.engineSend({ cmd: 'filesGrep', text });
  return null;
}
SCENE_MSG.filesGrep = (m) => {
  if ((m.q || '').toLowerCase() !== GF.grep.q) return;
  GF.grep.hits = new Map(m.hits);
  for (const h of Object.keys(GF.els)) gfRender(h);
};

// What matches: the words (anywhere, all of them), the typed filters, the chips
function gfMatches(skip) {
  if (!GF.index) return [];
  const words = GF.q.toLowerCase().split(/\s+/).filter(Boolean);
  const want = { type: GF.chips.type, where: GF.chips.where, area: GF.chips.area };
  const plain = [];
  let has = '';
  for (const w of words) {
    const m = /^(type|in|area|has):(.+)$/.exec(w);
    if (!m) plain.push(w);
    else if (m[1] === 'type') {
      // (a kind - type:ai, type:layouts - or a file's extension: type:uca)
      const kind = GF_TYPES.find(([k, l]) => k === m[2] || l.toLowerCase().startsWith(m[2]));
      if (kind) want.type = kind[0];
      else plain.push('.' + m[2].replace(/^\./, ''));
    }
    else if (m[1] === 'in') want.where = (GF_WHERE.find(([k, l]) => k === m[2] || l.toLowerCase().replace(/\s/g, '').startsWith(m[2])) || [m[2]])[0];
    else if (m[1] === 'has') has = m[2];
    else want.area = m[2];
  }
  const inside = has ? gfGrep(has) : null;
  return GF.index.filter(
    (f) =>
      (skip === 'type' || !want.type || f.t === want.type) &&
      (skip === 'where' || !want.where || f.w === want.where) &&
      (skip === 'area' || !want.area || f.a === want.area) &&
      // (art data - the frames inside an animation's folder - only when asked)
      (want.type === 'frm' || f.t !== 'frm' || plain.length > 0 || has) &&
      (!has || (inside && inside.has(f.n))) &&
      plain.every((w) => f.n.includes(w)),
  );
}

// The list's element for a host (mods, engine): made once and kept, so the
// view around it can be drawn again without the search box losing you
function gfElement(host) {
  if (GF.els[host]) return GF.els[host];
  const el = document.createElement('div');
  el.className = 'gf';
  el.innerHTML = `<input class="search" placeholder="Search the game's files: words, type:lyt, in:mm, area:animals, has:text inside" spellcheck="false"><div class="gfchips"></div><div class="gflist"></div>`;
  const inp = el.querySelector('input');
  inp.value = GF.q;
  inp.oninput = () => {
    GF.q = inp.value;
    store.set('gfQ', GF.q);
    // (each list follows, after you stop typing a moment)
    clearTimeout(GF.timer);
    GF.timer = setTimeout(() => {
      for (const h of Object.keys(GF.els)) {
        if (h !== host) GF.els[h].querySelector('input').value = GF.q;
        gfRender(h);
      }
    }, 160);
  };
  el.onclick = (e) => {
    const chip = e.target.closest('[data-chip]');
    if (chip) {
      const [k, v] = chip.dataset.chip.split(':');
      GF.chips[k] = GF.chips[k] === v ? '' : v;
      store.set('gfChips', GF.chips);
      for (const h of Object.keys(GF.els)) gfRender(h);
      return;
    }
    const row = e.target.closest('[data-gf]');
    if (row) gfOpen(row.dataset.gf, host);
  };
  GF.els[host] = el;
  return el;
}
// Into its slot in the view (drawn again around it)
function gfMount(slot, host) {
  if (!slot) return;
  const el = gfElement(host);
  if (el.parentElement !== slot) slot.appendChild(el);
  gfAskIndex();
  gfRender(host);
}

function gfRender(host) {
  const el = GF.els[host];
  if (!el) return;
  const chipsEl = el.querySelector('.gfchips'),
    list = el.querySelector('.gflist');
  if (!GF.index) {
    setHtml(chipsEl, '');
    setHtml(list, `<div class="muted" style="padding:8px">${S.engine.status === 'connected' ? 'Asking the engine for its files…' : 'The engine reads the files: it isn’t running.'}</div>`);
    gfAskIndex();
    return;
  }
  const count = (key, of) => {
    const m = new Map();
    for (const f of gfMatches(key)) m.set(f[of], (m.get(f[of]) || 0) + 1);
    return m;
  };
  const tc = count('type', 't'),
    wc = count('where', 'w');
  const ac = count('area', 'a');
  const chip = (k, v, label, n) => `<span class="gfchip${GF.chips[k] === v ? ' on' : ''}" data-chip="${k}:${esc(v)}">${esc(label)}${n !== undefined ? ` <span class="dim">${n.toLocaleString()}</span>` : ''}</span>`;
  const areas = [...ac.entries()].sort((a, b) => b[1] - a[1]).slice(0, 14);
  setHtml(
    chipsEl,
    `<div class="gfrow">${GF_TYPES.filter(([k]) => tc.get(k) || GF.chips.type === k).map(([k, l]) => chip('type', k, l, tc.get(k) || 0)).join('')}</div><div class="gfrow">${GF_WHERE.filter(
      ([k]) => wc.get(k) || GF.chips.where === k,
    )
      .map(([k, l]) => chip('where', k, l, wc.get(k) || 0))
      .join('')}</div><div class="gfrow">${areas.map(([a, n]) => chip('area', a, a, n)).join('')}</div>`,
  );
  const hits = gfMatches();
  const open = (GF.open || {})[host];
  const has = gfHas();
  const inside = has && GF.grep.q === has ? GF.grep.hits : null;
  // (has: the lines that matched, under each file)
  // (every line when it's down to a few files; the first five when there are many)
  const few = hits.length <= 5;
  const lines = (f) => {
    const ls = inside && inside.get(f.n);
    if (!ls) return '';
    const show = few ? ls : ls.slice(0, 5);
    return show.map((l) => `<div class="sub mono gfline">${esc(l.trim())}</div>`).join('') + (ls.length > show.length ? `<div class="sub dim">+${ls.length - show.length} more lines</div>` : '');
  };
  setHtml(
    list,
    has && !inside
      ? `<div class="muted" style="padding:8px">Looking inside the game’s files for “${esc(has)}”…</div>`
      : hits.length
      ? (has ? `<div class="muted" style="padding:4px 8px">${hits.length.toLocaleString()} files with “${esc(has)}” in them</div>` : '') +
        hits
          .slice(0, 600)
          .map(
            (f) =>
              `<div class="item${open === f.n ? ' sel' : ''}" data-gf="${esc(f.n)}"><div class="name mono" style="font-size:12px">${esc(f.n)}</div><div class="sub">${esc(f.f)}</div>${lines(f)}</div>`,
          )
          .join('') + (hits.length > 600 ? `<div class="muted" style="padding:8px">${(hits.length - 600).toLocaleString()} more: add a word or a chip</div>` : '')
      : '<div class="muted" style="padding:8px">No files like that.</div>',
  );
}

// Where our engine's code names a file (by its name: animals/orca.ai, or
// orca.ai)
async function gfRefs(name) {
  if (GF.refs[name]) return GF.refs[name];
  const base = name.split('/').pop();
  const hits = (await lab.srcGrep(base)) || [];
  GF.refs[name] = hits.slice(0, 8);
  return GF.refs[name];
}

// A file opened from a host's list: text in the middle, the rest in a viewer
async function gfOpen(name, host) {
  GF.open = GF.open || {};
  GF.open[host] = name;
  gfRender(host);
  const t = gfType(name);
  if (!TEXT_KINDS.has(t)) return gfViewer(name);
  const m = await gfView(name);
  if (m.kind !== 'text') return gfViewer(name, m);
  if (host === 'engine') return openGameInEngine(name, m);
  S.modsGame = { name, text: m.text, from: m.from };
  renderMods();
}

// (Engine code: the file, read-only, as a tab beside our code)
async function openGameInEngine(name, m) {
  const key = 'game:' + name;
  await CodeEditor.open(key, m.text.replace(/\r\n/g, '\n'), { game: true });
  const E = S.eng;
  if (!E.opened.includes(key)) E.opened.push(key);
  E.file = key;
  // (found by has:text - opened at the line that has it)
  const has = gfHas();
  const at = has ? m.text.split(/\r?\n/).findIndex((l) => l.replace(/[ \t\r]/g, '').toLowerCase().includes(has)) : -1;
  E.goto = at >= 0 ? at + 1 : 1;
  E.col = 1;
  if (S.tab !== 'engine') setTab('engine');
  else renderEngine();
}

// From elsewhere (a species' .ai from the Scene, from Try): Engine code's
// list, searched for it, and the file open
function gameFilesGo(q, name) {
  GF.q = q || '';
  store.set('gfQ', GF.q);
  for (const h of Object.keys(GF.els)) GF.els[h].querySelector('input').value = GF.q;
  S.eng.left = 'game';
  store.set('engLeft', 'game');
  setTab('engine');
  if (name) gfOpen(name, 'engine');
}

// ------------------------------------------------------- a mod's own files
// A file inside a mod's .ztd, open to change in Write Code's editor (its key
// "mod:<folder>|<ztd>|<entry>"; saveEngineFile puts it back in the .ztd)
function modKeyParts(rel) {
  const m = /^mod:([^|]+)\|([^|]+)\|(.+)$/.exec(rel || '');
  return m ? { folder: m[1], ztd: m[2], entry: m[3] } : null;
}
async function openModEntry(folder, ztd, entry, have) {
  const key = `mod:${folder}|${ztd}|${entry}`;
  if (!CodeEditor.isOpen(key)) {
    const r = have || (await lab.modkitEntry(folder, ztd, entry));
    if (!r || !r.ok) return toast(esc((r && r.error) || 'Couldn’t read it.'), 4000);
    await CodeEditor.open(key, r.text, { crlf: r.crlf, data: true });
  }
  const E = S.eng;
  if (!E.opened.includes(key)) E.opened.push(key);
  E.file = key;
  E.goto = E.goto || 1;
  if (S.tab !== 'engine') setTab('engine');
  else renderEngine();
}

// ------------------------------------------------------------ the viewer
const GV = { name: null, m: null, frame: 0, playing: true, zoom: 2, timer: null, bg: store.get('gvBg', 'checker') };
async function gfViewer(name, have) {
  GV.name = name;
  GV.m = null;
  GV.frame = 0;
  GV.playing = true;
  gvDraw();
  GV.m = have && have.kind !== 'text' ? have : await gfView(name);
  if (GV.name !== name) return;
  GV.zoom = GV.m.kind === 'anim' ? (GV.m.w > 300 ? 1 : 2) : GV.m.kind === 'image' ? (GV.m.w > 500 ? 1 : 2) : 1;
  gvDraw();
  gvPlay();
}
function gvClose() {
  clearInterval(GV.timer);
  GV.name = null;
  const v = $('gview');
  if (v) v.remove();
}
function gvPlay() {
  clearInterval(GV.timer);
  const m = GV.m;
  if (!m || m.kind !== 'anim' || !GV.playing || (m.png || []).length < 2) return;
  GV.timer = setInterval(() => {
    GV.frame = (GV.frame + 1) % m.png.length;
    gvFrame();
  }, Math.max(30, m.frameMs || 100));
}
function gvFrame() {
  const img = $('gv-img'),
    n = $('gv-n');
  const m = GV.m;
  if (img && m && m.png && m.png[GV.frame]) img.src = 'data:image/png;base64,' + m.png[GV.frame];
  if (n && m) n.textContent = `frame ${GV.frame + 1} of ${m.png.length}`;
}
function gvDraw() {
  let v = $('gview');
  if (!v) {
    v = document.createElement('div');
    v.id = 'gview';
    v.className = 'gview';
    document.body.appendChild(v);
    v.onclick = (e) => {
      const a = e.target.closest('[data-gv]');
      if (!a) return;
      const [act, arg] = a.dataset.gv.split('|');
      if (act === 'close') return gvClose();
      if (act === 'play') {
        GV.playing = !GV.playing;
        gvPlay();
      } else if (act === 'step') {
        GV.playing = false;
        clearInterval(GV.timer);
        GV.frame = (GV.frame + +arg + GV.m.png.length) % GV.m.png.length;
        gvFrame();
      } else if (act === 'dir') {
        GV.playing = true;
        gfView(GV.name, arg).then((m) => {
          GV.m = m;
          GV.frame = 0;
          gvDraw();
          gvPlay();
        });
        return;
      } else if (act === 'zoom') GV.zoom = Math.max(0.5, Math.min(8, GV.zoom * (+arg > 0 ? 2 : 0.5)));
      else if (act === 'bg') {
        GV.bg = { checker: 'dark', dark: 'grass', grass: 'water', water: 'checker' }[GV.bg] || 'checker';
        store.set('gvBg', GV.bg);
      } else if (act === 'engine') return gameFilesGo(GV.name, null);
      else if (act === 'mod') return gvMakerOpen();
      else if (act === 'mk') return gvMaker(arg, a.dataset.v);
      gvDraw();
    };
    // (dragged by its title)
    v.onmousedown = (e) => {
      if (!e.target.closest('.gvtitle') || e.target.closest('[data-gv]')) return;
      const r = v.getBoundingClientRect(),
        x0 = e.clientX - r.left,
        y0 = e.clientY - r.top;
      const move = (ev) => {
        v.style.left = Math.max(0, ev.clientX - x0) + 'px';
        v.style.top = Math.max(0, ev.clientY - y0) + 'px';
        v.style.right = 'auto';
      };
      const up = () => {
        window.removeEventListener('mousemove', move);
        window.removeEventListener('mouseup', up);
      };
      window.addEventListener('mousemove', move);
      window.addEventListener('mouseup', up);
      e.preventDefault();
    };
  }
  const m = GV.m;
  const b = (act, label, title, on) => `<span class="dbg${on ? ' on' : ''}" data-gv="${act}" title="${esc(title || '')}">${label}</span>`;
  let body = '<div class="muted dkmsg">Reading it…</div>',
    tools = '';
  if (m && m.kind === 'anim') {
    tools =
      b('play', GV.playing ? '❚❚' : '▶', 'Play or hold') +
      b('step|-1', '◀', 'A frame back') +
      b('step|1', '▶', 'A frame on') +
      `<span class="mono dim" id="gv-n"></span><span class="sep2"></span>` +
      (m.dirs || []).map((d) => b('dir|' + d, d, 'Facing ' + d, d === m.dir)).join('');
    body = `<div class="gvstage bg-${GV.bg}"><img id="gv-img" style="width:${m.w * GV.zoom}px;height:${m.h * GV.zoom}px"></div><div class="dim gvfoot">${m.frames} frames${
      m.frameMs ? ` · ${m.frameMs} ms each` : ''
    } · ${m.w}×${m.h} · ${(m.dirs || []).length} direction${(m.dirs || []).length === 1 ? '' : 's'}</div>`;
  } else if (m && m.kind === 'image') {
    body = `<div class="gvstage bg-${GV.bg}"><img src="data:image/png;base64,${m.png}" style="width:${m.w * GV.zoom}px;height:${m.h * GV.zoom}px"></div><div class="dim gvfoot">${m.w}×${m.h} · ${(m.size || 0).toLocaleString()} bytes</div>`;
  } else if (m && m.kind === 'palette') {
    body = `<div class="gvpal">${(m.colors || [])
      .map((c, i) => `<span style="background:${c}" title="${i}: ${c}"></span>`)
      .join('')}</div><div class="dim gvfoot">${(m.colors || []).length} colours · hover one for its number and value</div>`;
  } else if (m && m.kind === 'sound') {
    body = m.data
      ? `<audio controls autoplay src="${URL.createObjectURL(new Blob([Uint8Array.from(atob(m.data), (c) => c.charCodeAt(0))], { type: m.type }))}"></audio><div class="dim gvfoot">${(m.size || 0).toLocaleString()} bytes</div>`
      : '<div class="muted dkmsg">Too big to play here.</div>';
  } else if (m && m.kind === 'binary') {
    const hex = m.hex || '';
    let rows = '';
    for (let i = 0; i < hex.length; i += 32) {
      const chunk = hex.slice(i, i + 32);
      const bytes = chunk.match(/../g) || [];
      rows += `<div><span class="dim">${(i / 2).toString(16).padStart(6, '0')}</span>  ${bytes.join(' ').padEnd(47)}  ${esc(bytes.map((h) => {
        const c = parseInt(h, 16);
        return c >= 32 && c < 127 ? String.fromCharCode(c) : '.';
      }).join(''))}</div>`;
    }
    body = `<div class="gvhex mono">${rows}</div><div class="dim gvfoot">${(m.size || 0).toLocaleString()} bytes${m.error ? ' · ' + esc(m.error) : ''} · the first 2 KB</div>`;
  } else if (m && m.kind === 'text') {
    body = `<pre class="gvtext mono">${esc(m.text)}</pre>`;
  } else if (m) body = `<div class="muted dkmsg">The engine can’t find ${esc(GV.name)}.</div>`;
  if (m && (m.kind === 'anim' || m.kind === 'image')) tools += `<span class="sep2"></span>${b('zoom|-1', '−', 'Smaller')}<span class="mono dim">${GV.zoom}×</span>${b('zoom|1', '+', 'Bigger')}${b('bg', 'Background', 'Checks, dark, grass, water')}`;
  if (m && m.kind === 'anim' && /\.ani$/i.test(GV.name))
    tools += `<span class="sep2"></span>${b('mod', '✦ Make a mod from this…', 'A new animation from this one - a colour that flickers through new ones, a new speed - packed as a mod')}`;
  if (m && m.kind === 'anim' && GV.maker) body += gvMakerHtml();
  setHtml(
    v,
    `<div class="gvtitle"><b class="mono">${esc(GV.name)}</b><span class="dim">${m ? esc((m.from || '').split(/[\\/]/).slice(-2).join('/')) : ''}</span><div class="spacer"></div>${b('close', '✕', 'Close')}</div>${
      tools ? `<div class="gvtools">${tools}</div>` : ''
    }${body}`,
  );
  if (m && m.kind === 'anim') gvFrame();
}
document.addEventListener('keydown', (e) => {
  if (e.key === 'Escape' && GV.name && !(e.target && /INPUT|TEXTAREA/.test(e.target.tagName))) gvClose();
});

// ------------------------------------------------- the animation maker
// From an animation open in the viewer: pick one of its colours, the new
// colours it flickers through (a frame each), its speed; re/lab/animaker.py
// writes the new frames and palette as a mod (mods/<folder>: mod.json and
// content.ztd); switched on, the engine loads it over the original
function gvAnimOf(name) {
  return name.replace(/\/[^/]+\.ani$/i, '');
}
async function gvMakerOpen() {
  const anim = gvAnimOf(GV.name);
  GV.maker = { anim, busy: 'Reading its colours…', colours: [], index: -1, made: null };
  gvDraw();
  const r = await lab.animTool({ op: 'colours', anim });
  if (!r.ok) {
    GV.maker.busy = '';
    GV.maker.error = r.error;
    return gvDraw();
  }
  const base = anim.split('/').filter((p) => !/^(f|g|m|y|idle)$/.test(p)).pop() || 'anim';
  Object.assign(GV.maker, {
    busy: '',
    colours: r.colours,
    palette: r.palette,
    frameMs: r.frameMs,
    index: r.colours[0] ? r.colours[0].index : -1,
    name: base.charAt(0).toUpperCase() + base.slice(1) + ' recoloured',
  });
  gvDraw();
}
function gvMakerHtml() {
  const k = GV.maker;
  if (k.busy) return `<div class="gvmaker"><div class="muted">${esc(k.busy)}</div></div>`;
  if (k.error) return `<div class="gvmaker"><div class="red">${esc(k.error)}</div></div>`;
  if (k.made)
    return `<div class="gvmaker"><div class="label">Made</div><div class="mono">${esc(k.made.mod)}</div><div class="dim">${k.made.changed || 1} colours changed · ${k.made.files.length} files: ${esc(k.made.files.join(', '))}</div>${
      k.made.flicker < [k.c1, k.c2, k.c3].filter(Boolean).length
        ? `<div class="amber">${k.made.flicker > 1 ? `Room for ${k.made.flicker} of the colours` : 'No room for a flicker'}: ${k.made.changed} shades and only so many free colours in the palette. Pick one shade (untick “every colour like it”) to flicker through them all.</div>`
        : ''
    }<div class="gvmkrow"><span class="dbg on" data-gv="mk|on">Test temporarily in the Lab</span><span class="dbg" data-gv="mk|close">Close</span></div></div>`;
  const pick = k.colours.find((c) => c.index === k.index);
  return `<div class="gvmaker"><div class="label">Make a mod from ${esc(k.anim)}</div>
    <div class="dim">1. The colour to change (its art's colours, most used first; ${esc(k.palette || '')})</div>
    <div class="gvmkcols">${k.colours
      .map((c) => `<span class="gvmkc${c.index === k.index ? ' on' : ''}" style="background:${c.hex}" data-gv="mk|pick" data-v="${c.index}" title="${c.index}: ${c.hex} · ${c.count} pixels"></span>`)
      .join('')}</div>
    <div class="gvmkrow dim">Picked: <span class="mono">${k.index}</span> ${pick ? `<span class="gvmkc" style="background:${pick.hex};display:inline-block;vertical-align:middle"></span> <span class="mono">${pick.hex}</span>` : ''}<label style="margin-left:12px"><input type="checkbox" id="gvmk-similar"${k.similar === false ? '' : ' checked'}> …and every colour like it (its other shades)</label></div>
    <div class="dim">2. What it becomes, a frame each (a flicker between them)</div>
    <div class="gvmkrow"><input type="color" id="gvmk-c1" value="${k.c1 || '#ff3a1f'}"><input type="color" id="gvmk-c2" value="${k.c2 || '#ffd23a'}"><input type="color" id="gvmk-c3" value="${k.c3 || '#ff3a1f'}"><span class="dim">3. Speed</span><input class="search" id="gvmk-ms" style="width:70px" value="${k.ms || Math.max(30, Math.round((k.frameMs || 100) * 0.6))}"><span class="dim">ms a frame (was ${k.frameMs})</span></div>
    <div class="dim">4. The mod</div>
    <div class="gvmkrow"><input class="search" id="gvmk-name" style="flex:1" value="${esc(k.name)}" placeholder="Its name"><input class="search" id="gvmk-author" style="width:140px" value="${esc(k.author || '')}" placeholder="You"></div>
    <div class="gvmkrow"><span class="dbg on" data-gv="mk|make">✦ Make the mod</span><span class="dbg" data-gv="mk|close">Cancel</span></div></div>`;
}
async function gvMaker(what, v) {
  const k = GV.maker;
  if (!k) return;
  const val = (id) => ($(id) ? $(id).value : '');
  if (['c1', 'c2', 'c3', 'ms', 'name', 'author'].some((f) => $('gvmk-' + f)))
    Object.assign(k, { c1: val('gvmk-c1'), c2: val('gvmk-c2'), c3: val('gvmk-c3'), ms: val('gvmk-ms'), name: val('gvmk-name'), author: val('gvmk-author'), similar: $('gvmk-similar') ? $('gvmk-similar').checked : true });
  if (what === 'close') GV.maker = null;
  else if (what === 'pick') k.index = +v;
  else if (what === 'make') {
    const folder = (k.name || 'mod').replace(/[^A-Za-z0-9]+/g, '');
    k.busy = 'Making the new frames…';
    gvDraw();
    const r = await lab.animTool({
      op: 'make',
      anim: k.anim,
      index: k.index,
      similar: k.similar !== false,
      colours: [k.c1, k.c2, k.c3].filter(Boolean),
      frameMs: +k.ms || 0,
      mod: { folder, name: k.name, author: k.author, about: `A new ${k.anim} animation, made in the ZT1 Lab` },
    });
    k.busy = '';
    if (!r.ok) k.error = r.error;
    else {
      k.made = r;
      k.folder = folder;
      toast(`Mod <b>${esc(k.name)}</b> made.`, 3000);
    }
  } else if (what === 'on') {
    if (!(await testModInZoo(k.folder))) return;
    const name = GV.name;
    k.busy = 'Restarting the engine with the mod on…';
    gvDraw();
    const t0 = Date.now();
    const wait = setInterval(async () => {
      if (Date.now() - t0 > 3000 && S.engine.status === 'connected') {
        clearInterval(wait);
        GV.maker = null;
        GV.m = await gfView(name);
        GV.frame = 0;
        GV.playing = true;
        gvDraw();
        gvPlay();
        toast('This is a temporary test of the new animation. Normal mod switches are unchanged.', 4000);
      } else if (Date.now() - t0 > 60000) clearInterval(wait);
    }, 400);
    return;
  }
  gvDraw();
}

// A game file's text, highlighted: [Section], key = value, ; comment
function gfTextHtml(text) {
  const hl = (l) => {
    const c = l.indexOf(';');
    const main = c >= 0 ? l.slice(0, c) : l;
    const com = c >= 0 ? `<span class="k-com">${esc(l.slice(c))}</span>` : '';
    let m;
    if ((m = main.match(/^(\s*)(\[.*\])(.*)$/))) return `${esc(m[1])}<span class="k-kw">${esc(m[2])}</span>${esc(m[3])}${com}`;
    if ((m = main.match(/^(\s*)([^=]+?)(\s*=\s*)(.*)$/)))
      return `${esc(m[1])}<span class="k-fn">${esc(m[2])}</span>${esc(m[3])}${/^-?\d+$/.test(m[4].trim()) ? `<span class="k-num">${esc(m[4])}</span>` : `<span class="k-str">${esc(m[4])}</span>`}${com}`;
    return esc(main) + com;
  };
  return String(text || '')
    .replace(/\r/g, '')
    .split('\n')
    .map((l, i) => `<div class="ln"><span class="no">${i + 1}</span><span>${hl(l)}</span></div>`)
    .join('');
}
// (the header's "our engine reads it" line, filled in when known)
async function gfRefsInto(el, name) {
  if (!el) return;
  const hits = await gfRefs(name);
  const at = (h) => `<span class="clickable blue mono" data-act="engOpen" data-arg="${esc(h.file)}:${h.line}">${esc(h.file.replace(/^src\//, ''))}:${h.line}</span>`;
  if (hits.length) {
    el.innerHTML = `Our engine names it: ${hits.slice(0, 3).map(at).join(', ')}${hits.length > 3 ? ` +${hits.length - 3}` : ''}`;
    return;
  }
  // (not named - a species' .ai never is, they all go through one loader -
  // but which of its keys does our engine read?)
  const text = (CodeEditor.textOf && CodeEditor.textOf('game:' + name)) || '';
  const keys = [...new Set([...text.matchAll(/^\s*([A-Za-z][A-Za-z0-9_]*)\s*=/gm)].map((m) => m[1]))];
  const read = keys.length ? (await lab.srcKeys(keys)) || {} : {};
  const got = keys.filter((k) => read[k]);
  el.innerHTML = got.length
    ? `Our engine names <b>${got.length}</b> of its ${keys.length} keys: ${got
        .slice(0, 4)
        .map((k) => `<span class="clickable blue mono" data-act="engOpen" data-arg="${esc(read[k].file)}:${read[k].line}" title="${esc(read[k].file)}:${read[k].line}">${esc(k)}</span>`)
        .join(', ')}${got.length > 4 ? ` +${got.length - 4}` : ''}`
    : '<span class="amber">Our engine doesn’t read it yet</span>';
}

Object.assign(ACTIONS, {
  engLeft: (k) => {
    S.eng.left = k;
    store.set('engLeft', k);
    renderEngine();
  },
  modsLeft: (k) => {
    S.modsLeft = k;
    store.set('modsSection', k);
    renderMods();
    renderTop();
  },
  modsGameClose: () => {
    S.modsGame = null;
    renderMods();
  },
  newModFromFile: (name) => newMod(null, name),
  // A new mod, starting as a copy of one there is
  copyMod: async (stem) => {
    const a = await ask('A new mod from ' + prettyStem(stem), [{ key: 'title', label: 'What is the new one called?', value: prettyStem(stem) + ' 2' }], 'Create');
    if (!a || !a.title) return;
    const to = a.title.toLowerCase().replace(/[^a-z0-9]+/g, '_').replace(/^_|_$/g, '') || 'mod';
    if (await lab.modsExists(to + '.cpp')) return toast(`There’s already a mod called ${esc(to)}.`);
    const code = (await lab.modsRead(stem + '.cpp')) || '';
    await lab.modsWrite(to + '.cpp', `// Started as a copy of ${stem}.cpp\n` + code);
    await lab.modsWrite(to + '.json', JSON.stringify({ name: a.title, about: 'Started as a copy of ' + prettyStem(stem) }, null, 2) + '\n');
    await loadMods();
    S.modsLeft = 'mods';
    pickMod(to);
    openEditor(to);
  },
});

// ------------------------------------------------ engine code from a file
// A game file open in Engine code: whether our engine reads it, and a reader
// for it made from a template (src/readers/<Name>Reader.cpp: the engine's
// IniReader, every value a step); a reader open: Build & run, and what it
// read in the Run tab under the editor
const RUN = { file: null, results: null, steps: [], fromN: 0, pending: null, busy: '' };
function readerFileOf(rel) {
  if (!rel || !/^src\/.+\.cpp$/.test(rel) || !CodeEditor.isOpen(rel)) return null;
  const m = /ZT_GAMEFILE_READER\(\s*"([^"]+)"/.exec(CodeEditor.textOf(rel) || '');
  return m ? m[1] : null;
}
function engBarHtml(rel) {
  if (/^game:/.test(rel || '')) {
    const name = rel.slice(5);
    const text = TEXT_KINDS.has(gfType(name));
    // (an animal's species file: a new animal from it; any of the game's text
    // files: a copy in a mod, to change)
    const animal = /^animals\/[^/]+\.(ai|uca)$/i.test(name);
    return `<div class="engbar"><span class="mono">${esc(name)}</span><span class="muted" id="eng-refs">Looking in our code…</span><div class="spacer"></div>${
      animal ? `<div class="btn small" data-act="mkSpecies" data-arg="${esc(name)}" title="A new animal made from this one, as a mod: its art copied from your game and tinted, this file as its species file with its own name">＋ New species from this</div>` : ''
    }${
      text ? `<div class="btn small" data-act="mkChangeInMod" data-arg="${esc(name)}" title="A copy of this file in a mod, to change: while the mod is on, the game reads yours">✎ Change it in a mod</div>` : ''
    }${
      text ? `<div class="btn small primary" data-act="engNewReader" data-arg="${esc(name)}" title="Engine code that reads this file, from a template: every value it holds, as steps">＋ New engine code that reads this</div>` : ''
    }</div>`;
  }
  // (a file inside a mod's .ztd: which mod, saving puts it back, and trying it)
  const mod = modKeyParts(rel);
  if (mod)
    return `<div class="engbar"><span class="mono">${esc(mod.entry)}</span><span class="muted">in the mod <b>${esc(mod.folder)}</b> (${esc(
      mod.ztd,
    )}) · Save (Ctrl+S) puts it back in the mod</span><div class="spacer"></div><div class="btn small" data-act="mkShowMod" data-arg="${esc(
      mod.folder,
    )}">Open the mod</div><div class="btn small primary" data-act="mkTryMod" data-arg="${esc(mod.folder)}" title="Save, switch the mod on, and restart the Test Zoo with it">▶ Try in the Test Zoo</div></div>`;
  // (a script: its own bar - run it, or switch it on)
  const sb = scriptBarHtml(rel);
  if (sb) return sb;
  const file = readerFileOf(rel);
  if (file)
    return `<div class="engbar"><span class="muted">Reads the game's</span> <span class="mono clickable blue" data-act="engOpenGame" data-arg="${esc(file)}">${esc(file)}</span><div class="spacer"></div>${
      RUN.busy ? `<span class="muted">${esc(RUN.busy)}</span>` : ''
    }<div class="btn small primary${RUN.busy ? ' disabled' : ''}" data-act="engRunReader" data-arg="${esc(file)}" title="Save, build the engine, run this reader: what it read shows in Run">▶ Build & run</div></div>`;
  return '';
}

// The template: a name from the file (animals/orca.ai -> OrcaAi)
function readerName(file) {
  const base = file.split('/').pop();
  return base
    .split(/[^A-Za-z0-9]+/)
    .filter(Boolean)
    .map((w) => w[0].toUpperCase() + w.slice(1).toLowerCase())
    .join('');
}
function readerTemplate(file, name) {
  return `// ${file}: the game's, read by our engine. Every value it holds comes out
// as a step (Engine code > Build & run, then Run). When the zoo.exe function
// that reads it is found, put its address in ZT_FN below, and use read${name}
// where the game needs this file.
#include "../IniReader.hpp"
#include "../LabTrace.hpp"
#include "../ResourceManager.hpp"

#include <string>

namespace {
std::string read${name}(ResourceManager *rm) {
  ZT_FN("", "Reads ${file}");
  IniReader *ini = rm->getIniReader("${file}");
  if (!ini) {
    ZT_STEP("", "It isn't there", {"file", "${file}"});
    return "not there";
  }
  int sections = 0, values = 0;
  for (const std::string &section : ini->getSections()) {
    sections++;
    for (const auto &[key, value] : ini->getSection(section)) {
      values++;
      ZT_STEP("", "A value", {"section", section}, {"key", key}, {"value", value});
    }
  }
  delete ini;
  return std::to_string(sections) + " sections, " + std::to_string(values) + " values";
}
} // namespace

ZT_GAMEFILE_READER("${file}", ${name}, read${name});
`;
}

SCENE_MSG.readerRan = (m) => {
  RUN.busy = '';
  RUN.file = m.file;
  RUN.results = m.results;
  RUN.steps = DK.steps.filter((x) => x.n > RUN.fromN && x.whoName === "a game file's reader");
  S.eng.tab = 'run';
  if (S.tab === 'engine') renderEngine();
};
function runHtml() {
  if (RUN.busy) return `<div class="step"><span class="muted">…</span>${esc(RUN.busy)}</div>`;
  if (!RUN.file) return '<div class="muted">Open a reader (src/readers/…) and press ▶ Build & run: what it read shows here, step by step.</div>';
  const r = RUN.results || [];
  let h = r.length
    ? r.map((x) => `<div class="step"><span class="green">✓</span><span><b>${esc(x.name)}</b> read <span class="mono">${esc(RUN.file)}</span>: ${esc(x.summary)}</span></div>`).join('')
    : `<div class="step"><span class="red">✕</span><span>No reader for <span class="mono">${esc(RUN.file)}</span> in this build: build it first.</span></div>`;
  h += `<div class="label">Its steps (${RUN.steps.length}${DK.dropped ? ', some too fast to show' : ''})</div>`;
  h += RUN.steps
    .slice(0, 800)
    .map((x) => `<div class="runrow"><span class="w">${esc(x.what)}</span><span class="vv">${(x.vals || []).map((v) => `<span class="vk">${esc(v.n)}</span> ${valText(v)}`).join('<span class="dim"> · </span>')}</span></div>`)
    .join('');
  return h;
}

Object.assign(ACTIONS, {
  engOpenGame: (name) => gfOpen(name, 'engine'),
  engNewReader: async (file) => {
    const name = readerName(file);
    const a = await ask('Engine code that reads ' + file, [{ key: 'rel', label: 'The new file', value: `src/readers/${name}Reader.cpp` }], 'Make it');
    if (!a || !a.rel) return;
    const r = await lab.srcCreate(a.rel.trim(), readerTemplate(file, name));
    if (!r.ok) return toast(esc(r.error), 4000);
    // (the engine's file list, fresh: the new one in it)
    S.eng.files = null;
    toast(`Made <b>${esc(a.rel)}</b>. ▶ Build & run to see what it reads.`, 3500);
    openEngineFile(a.rel.trim(), 1);
  },
  engRunReader: async (file) => {
    if (RUN.busy) return;
    RUN.busy = 'Building the engine…';
    RUN.fromN = DK.steps.length ? DK.steps[DK.steps.length - 1].n : 0;
    RUN.pending = file;
    S.eng.tab = 'run';
    renderEngine();
    await ACTIONS.engBuild();
  },
});

// (after the build: the engine back up, then the reader run)
lab.onBuildDone((r) => {
  if (!RUN.pending) return;
  const file = RUN.pending;
  if (!r.ok) {
    RUN.pending = null;
    RUN.busy = '';
    S.eng.tab = 'probs';
    return;
  }
  RUN.busy = 'Starting the engine on the new build…';
  const t0 = Date.now();
  // (the new engine: the old one seen going, then the new one's first state)
  let down = false,
    oldState = S.engine.state;
  const wait = setInterval(() => {
    if (S.engine.status !== 'connected') down = true;
    const fresh = down && S.engine.status === 'connected' && S.engine.state && S.engine.state !== oldState;
    if (fresh || (Date.now() - t0 > 20000 && S.engine.status === 'connected' && S.engine.state !== oldState)) {
      clearInterval(wait);
      RUN.pending = null;
      RUN.busy = 'Running the reader…';
      RUN.fromN = DK.steps.length ? DK.steps[DK.steps.length - 1].n : 0;
      lab.engineSend({ cmd: 'readerRun', file });
      if (S.tab === 'engine') renderEngine();
    } else if (Date.now() - t0 > 60000) {
      clearInterval(wait);
      RUN.pending = null;
      RUN.busy = '';
      toast('The engine didn’t come back after the build.', 5000);
    }
  }, 300);
});
