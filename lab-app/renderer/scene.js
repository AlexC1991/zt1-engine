// ZT1 Lab: the Scene view. The engine's real World as a scene editor: any
// map loaded (as it is, or made flat), everything in it listed on the left,
// what's clicked inspected (and changed) on the right, things put down with
// the game's own tools (the buy panels' items, fences, paths, staff,
// terrain, the bulldozer), the zoo run at a speed or a step at a time, and
// the game's dev console underneath. A mod (re/lab/scripts) still runs in
// it, its checks and graphs on the right.
// (loaded after app.js: S, $, lab, store, ACTIONS are its)

const SC = {
  left: store.get('scLeft', 'zoo'), // zoo | mods
  right: 'inspect', // inspect | mod
  outline: null,
  countsKey: '',
  outlineAsked: 0,
  q: '',
  open: store.get('scOpen', { exhibits: true, animals: true, staff: false, guests: false, buildings: false, scenery: false }),
  cycle: {}, // scenery kind -> the next of them to pick
  maps: null,
  catalog: null,
  terrains: [],
  filterFile: '',
  icons: {}, // icon path -> data: URL ('' asked, 'none' no art)
  palette: { open: false, cat: store.get('scCat', 'animals'), q: '' },
  opts: { female: false, facing: 0, size: 2 },
  conHist: store.get('scConHist', []),
  conOut: [], // the console's commands and answers
  picked: {}, // the inspector's dropdowns: what's chosen in each
  conAt: -1,
  lastScript: '',
};

const sc = () => (S.engine.state && S.engine.state.scene) || null;
S.consoleOpen = store.get('dkOpen', true);
lab.onEngineStatus((st) => {
  if (st.status !== 'connected') return;
  lab.engineSend({ cmd: 'breakpoints', list: [...DK.bps] });
  // (a new engine: its steps start again from 1, so the old ones go)
  DK.steps = [];
  DK.byN.clear();
  DK.cur = null;
  DK.paused = null;
  DK.dropped = 0;
  if (S.tab === 'scene') renderConsole();
});
const setHtml = (el, html) => {
  if (el && el._h !== html) {
    el.innerHTML = html;
    el._h = html;
  }
};
const money = (n) => '$' + Math.round(n).toLocaleString();
const clockText = (s) => {
  s = Math.floor(s || 0);
  const h = Math.floor(s / 3600),
    m = Math.floor((s % 3600) / 60),
    sec = s % 60;
  return `${h}:${String(m).padStart(2, '0')}:${String(sec).padStart(2, '0')}`;
};

// ------------------------------------------------------------ the palette
// The buy panels' tabs (an item's [Member] categories), then terrain
const PALETTE = [
  ['animals', 'Animals', (i) => i.section === 'animals' || (i.members || []).includes('animals')],
  ['shelters', 'Shelters', (i) => i.members.includes('shelters')],
  ['toys', 'Toys', (i) => i.members.includes('toys')],
  ['buildings', 'Buildings', (i) => i.section === 'building' && !i.members.includes('shelters') && !i.members.includes('toys')],
  ['scenery', 'Scenery', (i) => i.members.includes('scenery')],
  ['foliage', 'Foliage', (i) => i.members.includes('foliage')],
  ['rocks', 'Rocks', (i) => i.members.includes('rocks')],
  ['fences', 'Fences', (i) => i.section === 'fences' || i.section === 'tankwall' || i.file === SC.filterFile],
  ['paths', 'Paths', (i) => i.section === 'paths'],
  ['staff', 'Staff', (i) => i.section === 'staff'],
  ['terrain', 'Terrain', null],
];
const HEIGHT_TOOLS = [
  [0, 'Hills and valleys', 'Raise or lower the ground, sloping round it'],
  [1, 'Cliffs and moats', 'Raise or lower the brush alone'],
  [2, 'Level (hills)', 'Bring the brush to the first tile’s height, sloping'],
  [3, 'Level (cliffs)', 'Bring the brush to the first tile’s height'],
];
const PACKS = ['', 'Dinosaur Digs', 'Marine Mania'];

function toolKindOf(item) {
  if (!item || !item.file) return '';
  if (item.file === SC.filterFile) return 'filter';
  if (item.section === 'animals' || (item.members || []).includes('animals')) return 'animal';
  if (item.section === 'staff') return 'staff';
  if (item.section === 'fences' || item.section === 'tankwall') return 'fence';
  if (item.section === 'paths') return 'path';
  return 'object';
}
const itemOf = (file) => (SC.catalog || []).find((i) => i.file === file);

function iconImg(path, cls = '') {
  if (!path) return `<span class="noicon ${cls}"></span>`;
  if (SC.icons[path] === undefined) {
    SC.icons[path] = '';
    lab.engineSend({ cmd: 'icon', path });
  }
  const src = SC.icons[path];
  return src && src !== 'none'
    ? `<img class="${cls}" data-icon="${esc(path)}" src="${src}">`
    : `<img class="${cls}" data-icon="${esc(path)}" src="data:image/gif;base64,R0lGODlhAQABAAAAACw=">`;
}

function paletteItems() {
  const cat = PALETTE.find((p) => p[0] === SC.palette.cat) || PALETTE[0];
  const q = SC.palette.q.trim().toLowerCase();
  if (cat[0] === 'terrain') {
    const types = SC.terrains.map((t) => ({
      key: 'paint:' + t.type,
      name: t.name,
      icon: t.icon,
      cost: t.cost,
      sub: 'paint',
    }));
    const heights = HEIGHT_TOOLS.map(([mode, name, about]) => ({ key: 'height:' + mode, name, about, sub: 'height' }));
    return [...heights, ...types].filter((x) => !q || x.name.toLowerCase().includes(q));
  }
  return (SC.catalog || [])
    .filter(cat[2])
    .filter((i) => !q || i.name.toLowerCase().includes(q))
    .map((i) => ({ key: i.file, name: i.name || i.file, icon: i.icon, cost: i.cost, pack: i.pack }));
}

function renderPalette() {
  const el = $('st-palette');
  if (!el) return;
  el.classList.toggle('hidden', !SC.palette.open);
  $('stage').classList.toggle('pal', SC.palette.open);
  if (!SC.palette.open) return;
  if (!SC.catalog) {
    setHtml(el, '<div class="muted" style="padding:14px">Reading the game’s items…</div>');
    return;
  }
  if (!$('pal-grid')) {
    el._h = null;
    el.innerHTML = `<div class="phead"><div class="chips" id="pal-cats"></div><div class="spacer"></div><input class="search small" id="pal-q" placeholder="Find an item" value="${esc(
      SC.palette.q,
    )}"><span class="muted clickable" data-act="scPalette" title="Close (P)">✕</span></div><div class="pgrid" id="pal-grid"></div>`;
    $('pal-q').oninput = (e) => {
      SC.palette.q = e.target.value;
      renderPaletteGrid();
    };
  }
  setHtml(
    $('pal-cats'),
    PALETTE.map(([id, label]) => `<div class="chip${SC.palette.cat === id ? ' on' : ''}" data-act="scCat" data-arg="${id}">${label}</div>`).join(''),
  );
  renderPaletteGrid();
}

function renderPaletteGrid() {
  const g = $('pal-grid');
  if (!g) return;
  const t = (sc() || {}).tool || {};
  const on = (x) =>
    x.sub === 'paint'
      ? t.kind === 'terrain' && t.painting && 'paint:' + t.terrainType === x.key
      : x.sub === 'height'
        ? t.kind === 'terrain' && !t.painting && 'height:' + t.mode === x.key
        : t.item === x.key && t.kind !== 'select';
  const items = paletteItems();
  setHtml(
    g,
    items.length
      ? items
          .map(
            (x) =>
              `<div class="pitem${on(x) ? ' on' : ''}" data-act="scPick" data-arg="${esc(x.key)}" title="${esc(x.about || x.name)}${
                x.pack ? ' (' + PACKS[x.pack] + ')' : ''
              }">${x.sub === 'height' ? `<span class="hicon">${['⛰', '▮', '⛰', '▮'][+x.key.split(':')[1]]}</span>` : iconImg(x.icon)}<span class="pn">${esc(
                x.name,
              )}</span>${x.cost ? `<span class="pc">${money(x.cost)}</span>` : ''}</div>`,
          )
          .join('')
      : '<div class="muted" style="padding:10px">Nothing matches.</div>',
  );
}

// ---------------------------------------------------------------- tools
function sendTool(t) {
  lab.engineSend({ cmd: 'tool', ...t });
}

function pickItem(key) {
  if (key.startsWith('paint:')) return sendTool({ kind: 'terrain', painting: true, terrainType: +key.slice(6), size: SC.opts.size });
  if (key.startsWith('height:')) return sendTool({ kind: 'terrain', painting: false, mode: +key.slice(7), size: SC.opts.size });
  const item = itemOf(key);
  if (!item) return;
  SC.opts.facing = 0;
  sendTool({ kind: toolKindOf(item), item: key, female: SC.opts.female, facing: 0 });
}

function toolLabel(t) {
  if (t.kind === 'terrain') {
    if (t.painting) return (SC.terrains.find((x) => x.type === t.terrainType) || { name: 'terrain' }).name;
    return (HEIGHT_TOOLS.find((h) => h[0] === t.mode) || [0, 'Height'])[1];
  }
  if (t.kind === 'gate') return 'Gate';
  const it = itemOf(t.item);
  return it ? it.name : t.item;
}

// ---------------------------------------------------------------- left
// A mod's line under its name: building, failed, running, finished...
function modStatus(stem) {
  const st = S.engine.state;
  const b = S.builds[stem];
  if (b && b.state === 'building') return ['building…', 'muted'];
  if (b && b.state === 'failed') return [`build failed · ${b.problems.length || 1} problem${b.problems.length === 1 ? '' : 's'}`, 'red'];
  if (st && st.script === stem && st.loaded) {
    const fails = st.checks.filter((c) => !c.ok).length;
    const checks = st.checks.length ? ` · ${st.checks.length - fails}/${st.checks.length} checks` : '';
    if (st.live) return [`${st.auto ? 'running' : 'paused'} · step ${st.step}${checks}`, fails ? 'red' : 'green'];
    return [`finished · ${st.step} steps${checks}`, fails ? 'red' : 'green'];
  }
  if (b && b.state === 'ok') return ['built', 'green'];
  const m = S.mods.find((x) => x.stem === stem);
  return [m && m.about ? m.about : 'not run yet', 'muted'];
}

function renderSceneLeft(force) {
  const left = $('scene-left');
  if (!$('sc-lefttabs') || force) {
    left._h = null;
    left.innerHTML = `<div class="seg ltabs" id="sc-lefttabs"></div><div id="sc-leftbody" class="lbody"></div>`;
  }
  setHtml(
    $('sc-lefttabs'),
    [
      ['zoo', 'Zoo'],
      ['mods', 'Mods'],
    ]
      .map(([id, l]) => `<div class="${SC.left === id ? 'on' : ''}" data-act="scLeft" data-arg="${id}">${l}</div>`)
      .join(''),
  );
  const body = $('sc-leftbody');
  if (SC.left === 'mods') {
    body.dataset.k = 'mods';
    setHtml(
      body,
      `<div class="sidehead label"><span>Mods</span><span class="green clickable" data-act="newMod">+ New</span></div>` +
        (S.mods.length
          ? S.mods
              .map((m) => {
                const [st, cls] = modStatus(m.stem);
                return `<div class="item${m.stem === S.mod ? ' sel' : ''}" data-act="pickMod" data-arg="${esc(m.stem)}"><div class="name">${esc(
                  (m.meta && m.meta.name) || prettyStem(m.stem),
                )}</div><div class="sub ${cls}">${esc(st)}</div></div>`;
              })
              .join('')
          : `<div class="item muted">No mods yet.</div>`) +
        `<div class="hintbox" style="margin:10px 4px 0;font-size:12px">A mod is a small C++ script that sets up the zoo and checks what the engine does. Pick one and press <b style="color:var(--text)">Run</b>; click it again to edit it.</div>`,
    );
    return;
  }
  // The zoo's outline: the map, then what's in it
  if (body.dataset.k !== 'zoo') {
    body.dataset.k = 'zoo';
    body._h = null;
    body.innerHTML = `<div class="mapbtn" data-act="scMaps" id="sc-map"></div><input class="search small" id="sc-q" placeholder="Find in the zoo" style="width:100%"><div id="sc-tree" class="tree"></div>`;
    $('sc-q').value = SC.q;
    $('sc-q').oninput = (e) => {
      SC.q = e.target.value;
      renderTree();
    };
  }
  const s = sc();
  setHtml(
    $('sc-map'),
    s
      ? `<div><div class="name">${esc(s.mapName || s.map)}</div><div class="sub">${s.flat ? 'made flat · ' : ''}change map ▾</div></div>`
      : '<div class="muted">No map yet</div>',
  );
  renderTree();
}

function renderTree() {
  const tree = $('sc-tree');
  if (!tree) return;
  const o = SC.outline;
  const s = sc();
  if (!o) return setHtml(tree, '<div class="muted" style="padding:8px">Reading the zoo…</div>');
  const q = SC.q.trim().toLowerCase();
  const sel = s && s.sel;
  const isSel = (kind, id) => sel && sel.kind === kind && sel.id === id;
  const match = (t) => !q || (t || '').toLowerCase().includes(q);
  const group = (key, label, rows, total) => {
    const shown = rows.filter((r) => match(r.text + ' ' + (r.sub || '')));
    if (q && !shown.length) return '';
    const open = q || SC.open[key];
    return `<div class="tgroup" data-act="scOpen" data-arg="${key}"><span class="tw">${open ? '▾' : '▸'}</span>${label}<span class="n">${
      total !== undefined ? total : rows.length
    }</span></div>${
      open
        ? shown
            .slice(0, 300)
            .map(
              (r) =>
                `<div class="titem${r.sel ? ' sel' : ''}" data-act="scSelect" data-arg="${esc(r.arg)}"><span>${esc(r.text)}</span><span class="sub">${esc(
                  r.sub || '',
                )}</span></div>`,
            )
            .join('') + (shown.length > 300 ? `<div class="titem muted">and ${shown.length - 300} more…</div>` : '')
        : ''
    }`;
  };
  const exName = (id) => (o.exhibits.find((e) => e.id === id) || {}).name || '';
  let h = '';
  h += group(
    'exhibits',
    'Exhibits',
    o.exhibits.map((e) => ({ text: e.name, sub: `${e.tank ? 'tank · ' : ''}${e.animals} animal${e.animals === 1 ? '' : 's'}`, arg: 'exhibit:' + e.id, sel: isSel('exhibit', e.id) })),
  );
  h += group(
    'animals',
    'Animals',
    o.animals.map((a) => ({ text: a.name || a.species, sub: a.exhibit >= 0 ? exName(a.exhibit) : 'loose', arg: 'animal:' + a.id, sel: isSel('animal', a.id) })),
  );
  h += group('staff', 'Staff', o.staff.map((m) => ({ text: m.name, sub: m.type, arg: 'staff:' + m.id, sel: isSel('staff', m.id) })));
  h += group('guests', 'Guests', o.guests.map((g) => ({ text: g.name, arg: 'guest:' + g.id, sel: isSel('guest', g.id) })), o.guestCount);
  h += group('buildings', 'Buildings', o.buildings.map((b) => ({ text: b.name, arg: 'object:' + b.i, sel: isSel('object', b.i) })));
  h += group(
    'scenery',
    'Scenery',
    o.scenery.map((k) => ({ text: k.name, sub: '×' + k.i.length, arg: 'kind:' + k.file, sel: sel && sel.kind === 'object' && k.i.includes(sel.id) })),
    o.scenery.reduce((n, k) => n + k.i.length, 0),
  );
  setHtml(tree, h);
}

// The engine's counts changed (something put down, born, gone): the
// outline again, at most twice a second
function checkCounts() {
  const s = sc();
  if (!s) return;
  const key = JSON.stringify(s.counts);
  if (key !== SC.countsKey && Date.now() - SC.outlineAsked > 500) {
    SC.countsKey = key;
    SC.outlineAsked = Date.now();
    lab.engineSend({ cmd: 'outline' });
  }
}

// ---------------------------------------------------------------- stage
function ensureStage() {
  if ($('st-tools')) return;
  $('stage-ui').innerHTML = `<div id="st-labels"></div>
    <div class="float seg tools" id="st-tools" style="left:12px;top:12px"></div>
    <div class="float seg tools hidden" id="st-toolopts" style="left:12px;top:58px"></div>
    <div class="float seg" id="st-view" style="right:12px;top:12px"></div>
    <div class="float playctl" id="st-time" style="left:12px;bottom:12px"></div>
    <div class="float hoverbox" id="st-hover" style="right:12px;bottom:12px"></div>
    <div class="palette hidden" id="st-palette"></div>
    <div class="float stopbar hidden" id="st-break"></div>
    <div id="st-msg"></div>`;
}

function renderStageUi() {
  ensureStage();
  const st = S.engine.state;
  const s = sc();
  const up = S.engine.status === 'connected' && st && s;
  setHtml(
    $('st-msg'),
    up
      ? ''
      : `<div class="stagemsg"><b>${S.engine.status === 'starting' || S.engine.status === 'connected' ? 'Starting the lab zoo…' : 'The engine isn’t running'}</b><span>${esc(
          S.engine.detail || 'It loads the game’s files and a map to work in.',
        )}</span>${S.engine.status === 'starting' || S.engine.status === 'connected' ? '' : '<div class="btn" data-act="restartEngine">Start it</div>'}</div>`,
  );
  for (const id of ['st-tools', 'st-view', 'st-time', 'st-hover']) $(id).classList.toggle('hidden', !up);
  if (!up) $('st-toolopts').classList.add('hidden');
  if (!up) return;
  const view = st.view || 'scene';
  const t = s.tool;
  const scene = view === 'scene';
  canvas.classList.toggle('tool', scene && t.kind !== 'select');

  // tools (top left)
  const placing = !['select', 'bulldoze', 'route'].includes(t.kind);
  let tools = `<div class="${t.kind === 'select' ? 'on' : ''}" data-act="scTool" data-arg="select" title="Select (Esc)">↖ Select</div><div class="${
    SC.palette.open || placing ? 'on' : ''
  }" data-act="scPalette" title="Put things down (P)">＋ Place</div><div class="${t.kind === 'bulldoze' ? 'on' : ''}" data-act="scTool" data-arg="bulldoze" title="Bulldozer (B)">⌫ Bulldoze</div><div class="${t.kind === 'route' ? 'on' : ''}" data-act="scTool" data-arg="route" title="Can it get there? A route search, as a guest, staff or the picked animal would (T)">⤳ Route</div><div class="${
    s.canUndo ? '' : 'off'
  }" data-act="scUndo" title="Undo (Ctrl+Z)">↶ Undo</div>`;
  if (placing) {
    let opts = '';
    if (t.kind === 'animal')
      opts = `<span class="opt${t.female ? '' : ' on'}" data-act="scOpt" data-arg="male">♂</span><span class="opt${t.female ? ' on' : ''}" data-act="scOpt" data-arg="female">♀</span>`;
    else if (t.kind === 'object' && (itemOf(t.item) || {}).facings > 1)
      opts = `<span class="opt" data-act="scOpt" data-arg="turn" title="Turn it (R)">⟳ ${['SE', 'SW', 'NW', 'NE'][t.facing]}</span>`;
    else if (t.kind === 'terrain')
      opts = `<span class="opt" data-act="scOpt" data-arg="smaller">−</span><span>${t.size}×${t.size}</span><span class="opt" data-act="scOpt" data-arg="bigger">+</span>`;
    else if (t.kind === 'path')
      opts = `<span class="opt" data-act="scOpt" data-arg="lower">▼</span><span>${t.walkway > 0 ? 'walkway +' + t.walkway : 'on the ground'}</span><span class="opt" data-act="scOpt" data-arg="higher">▲</span>`;
    if (t.kind === 'fence' || t.kind === 'path')
      opts += `<span class="opt" data-act="scOpt" data-arg="lineMode" title="How a drag lays it out (Tab)">${esc(t.lineMode)}</span>`;
    tools += `<div class="toolchip"><span class="muted">${t.kind === 'terrain' ? (t.painting ? 'Painting' : 'Shaping') : 'Placing'}</span><b>${esc(
      toolLabel(t),
    )}</b>${opts}<span class="muted clickable" data-act="scTool" data-arg="select" title="Stop (Esc or right-click)">✕</span></div>`;
  }
  if (t.kind === 'route') {
    const rt = s.routeTest || {};
    const who = t.item || 'guest';
    const kinds = [
      ['guest', 'Guest'],
      ['staff', 'Staff'],
      ['animal', 'Picked animal'],
    ]
      .map(([k, l]) => `<span class="opt${who === k ? ' on' : ''}" data-act="scRouteKind" data-arg="${k}">${l}</span>`)
      .join('');
    const r = rt.search;
    const said = r
      ? r.found
        ? `<span class="green">${r.routeTiles} tiles</span> <span class="muted">· looked at ${r.looked}</span>`
        : `<span class="red">${esc(r.why)}</span>`
      : who === 'animal'
        ? '<span class="muted">click where to</span>'
        : rt.startSet
          ? `<span class="muted">from ${rt.startX}, ${rt.startY}: click where to</span>`
          : '<span class="muted">click where from</span>';
    tools += `<div class="toolchip"><span class="muted">Route as</span>${kinds}${said}<span class="muted clickable" data-act="scTool" data-arg="select" title="Stop (Esc)">✕</span></div>`;
  }
  // (the tool's options on a row of their own, under the tools)
  const cut = tools.indexOf('<div class="toolchip"');
  setHtml($('st-tools'), cut >= 0 ? tools.slice(0, cut) : tools);
  setHtml($('st-toolopts'), cut >= 0 ? tools.slice(cut) : '');
  $('st-toolopts').classList.toggle('hidden', cut < 0);

  // view (top right)
  const segs = [
    ['scene', 'Scene'],
    ['heightmap', 'Terrain'],
    ['animal', 'Animal'],
  ];
  const ov = s.overlays;
  let v = `<div class="mapname" data-act="scMaps" title="Load another map">${esc(s.mapName || s.map)} ▾</div><span class="sep"></span>`;
  v += segs.map(([m, l]) => `<div class="${view === m || (m === 'animal' && view === 'anim') ? 'on' : ''}" data-act="view" data-arg="${m}">${l}</div>`).join('');
  v += '<span class="sep"></span>';
  if (scene)
    v +=
      [
        ['grid', 'Grid', 'The tile grid'],
        ['exhibits', 'Exhibits', 'Exhibits’ outlines and names'],
        ['routes', 'Routes', 'Where everyone’s walking (animals orange, guests blue, staff green), and the picked one’s last route search'],
        ['paths', 'Blocked', 'Where staff can’t walk (red), and the tiles of staff routes'],
        ['footprints', 'Footprints', 'Every object’s footprint: the quarter tiles it blocks (pink, buildings orange); a red cross where none was found, so it blocks nothing'],
      ]
        .map(([k, l, tip]) => `<div class="${ov[k] ? 'on' : ''}" data-act="scOverlay" data-arg="${k}" title="${tip}">${l}</div>`)
        .join('') +
      `<span class="sep"></span><div data-act="rotate" title="Turn the view">⟳</div><div data-act="scZoom" data-arg="-1" title="Zoom out">−</div><div data-act="scZoom" data-arg="1" title="Zoom in">+</div><div class="${
        s.sel ? '' : 'off'
      }" data-act="scFocus" title="Centre on what's picked (F)">⌖</div>`;
  else if (view === 'animal' || view === 'anim') {
    // the art viewer: a species, any of its animations, its layers together
    const [species, anim] = (st.what || 'zebra').split('|');
    const list = SC.viewAnims && SC.viewAnims.species === species ? SC.viewAnims.anims : [];
    v += `<div data-act="turnArt" title="Turn it">⟳ Turn</div><div data-act="pickSpecies">${esc(species)} ▾</div>${
      list.length
        ? `<select class="sel" id="st-anim" title="One of its animations"><option value="">(its default look)</option>${list
            .map((a) => `<option${a === anim ? ' selected' : ''}>${esc(a)}</option>`)
            .join('')}</select>`
        : ''
    }<div class="${SC.viewLayered ? 'on' : ''}" data-act="scLayers" title="Draw its layers together at one anchor: shadow + art, under + art, then the animation (Marine Mania's tank art)">Layers</div><div data-act="scArtZoom" data-arg="-1" title="Smaller">−</div><div data-act="scArtZoom" data-arg="1" title="Bigger">+</div><div data-act="scArtFrame" data-arg="-1" title="Hold it a frame back">◀</div><div class="${SC.artHeld ? 'on' : ''}" data-act="scArtFrame" data-arg="0" title="${SC.artHeld ? 'Held: click to play' : 'Hold every layer on this frame, to compare them'}">${SC.artHeld ? '❚❚' : '▶'}</div><div data-act="scArtFrame" data-arg="1" title="Hold it a frame on">▶</div><div data-act="scCheck" data-arg="" title="Check this species in bulk: its art, its Marine Mania layers, and its behaviour sets against what the engine does">Check</div><div data-act="scCheck" data-arg="*marine" title="The same for every tank animal">Check marine</div>`;
  }
  else v += `<div class="on">${esc(st.what || 'heights, top down')}</div>`;
  setHtml($('st-view'), v);
  fitStageBars();

  // time (bottom left): the zoo's clock, or a mod's steps
  let tm;
  if (st.loaded) {
    tm = `<div class="round" data-act="togglePlay" title="Step the mod automatically">${st.live && st.auto ? '❚❚' : '▶'}</div><span>Step ${(
      st.step || 0
    ).toLocaleString()}</span><span class="muted clickable" data-act="stepOnce" title="One step">+1</span><span class="muted clickable" data-act="cycleSpeed" title="Steps a second">${Math.round(
      st.sps,
    )}/s</span><span class="muted clickable" data-act="stopMod" title="Stop the mod">■</span><span class="sep"></span><span class="dim">${esc(
      prettyStem(st.script),
    )}</span>`;
  } else {
    tm = `<div class="round" data-act="togglePlay" title="Run the zoo (Space)">${st.playing ? '❚❚' : '▶'}</div><span class="mono">${clockText(
      s.clock,
    )}</span><span class="muted clickable" data-act="scAdvance" data-arg="1" title="One second of the game">+1s</span><span class="muted clickable" data-act="scAdvance" data-arg="60" title="A minute of the game">+1m</span><span class="sep"></span>${[1, 2, 4, 8]
      .map((x) => `<span class="spd${Math.abs(s.speed - x) < 0.01 ? ' on' : ''}" data-act="scSpeed" data-arg="${x}">${x}×</span>`)
      .join('')}`;
  }
  setHtml($('st-time'), tm);

  // what's under the cursor (bottom right)
  const hv = s.hover;
  setHtml(
    $('st-hover'),
    scene && hv
      ? `<span class="mono">${hv.x}, ${hv.y}</span><span>height ${fmt(hv.h)}</span><span>${esc(hv.terrain)}</span>${hv.path ? `<span>${esc(hv.path)}</span>` : ''}${
          hv.exhibit ? `<span class="green">${esc(hv.exhibit)}</span>` : hv.inZoo ? '' : '<span class="dim">outside the zoo</span>'
        }`
      : `<span class="dim">${scene ? 'Over the map: the tile, its height and ground' : ''}</span>`,
  );
  $('st-hover').classList.toggle('hidden', !scene);

  // the exhibits' names over them
  const dpr = window.devicePixelRatio || 1;
  setHtml(
    $('st-labels'),
    scene
      ? s.labels
          .map(
            (l) =>
              `<div class="xlabel${s.sel && s.sel.kind === 'exhibit' && s.sel.id === l.id ? ' on' : ''}${l.tank ? ' tank' : ''}" style="left:${Math.round(
                l.sx / dpr,
              )}px;top:${Math.round(l.sy / dpr)}px" data-act="scSelect" data-arg="exhibit:${l.id}">${esc(l.name)}</div>`,
          )
          .join('')
      : '',
  );
  // stopped at a step: what, whose, where - and on from here
  const p = DK.paused;
  $('st-break').classList.toggle('hidden', !p);
  if (p)
    setHtml(
      $('st-break'),
      `<span class="amber">⏸</span><span><b>${esc(p.what)}</b> <span class="muted">· ${esc(p.whoName)} · ${esc(p.file)}:${p.line}</span></span><span class="dbg go" data-act="dkGo" data-arg="continue">▶ Continue</span><span class="dbg" data-act="dkGo" data-arg="stepOver">⤼ Over</span><span class="dbg" data-act="dkGo" data-arg="stepNext">↓ Step</span>`,
    );
  renderPalette();
}

// ---------------------------------------------------------------- right
function renderRight() {
  const right = $('scene-right');
  if (S.drawer.open) return;
  // (not while a box in it is being typed in)
  const a = document.activeElement;
  if (a && right.contains(a) && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.tagName === 'SELECT')) return;
  right.className = 'card side wide insp';
  const st = S.engine.state;
  if (st && st.loaded && st.script !== SC.lastScript) {
    SC.lastScript = st.script;
    SC.right = 'mod';
  }
  if (st && !st.loaded) SC.lastScript = '';
  const tabs =
    st && st.loaded
      ? `<div class="seg rtabs">${[
          ['inspect', 'Inspector'],
          ['mod', 'Mod checks'],
        ]
          .map(([id, l]) => `<div class="${SC.right === id ? 'on' : ''}" data-act="scRight" data-arg="${id}">${l}</div>`)
          .join('')}</div>`
      : '';
  const body = st && st.loaded && SC.right === 'mod' ? modChecksHtml(st) : inspectorHtml();
  setHtml(right, tabs + body);
}

function modChecksHtml(st) {
  const checks = st.checks.length
    ? st.checks
        .map(
          (c) =>
            `<div class="check"><span class="${c.ok ? 'green' : 'red'}">${c.ok ? '✓' : '✕'}</span><span>${esc(c.what)}${
              S.expert || !c.ok ? ` <span class="dim">${c.times - c.fails}/${c.times}</span>` : ''
            }</span></div>`,
        )
        .join('')
    : '<div class="muted">This mod has no checks.</div>';
  const watches = st.watches.length
    ? st.watches
        .map(
          (w) =>
            `<div class="watch"><span>${esc(w.label)}${S.expert && w.where ? `<div class="w">${esc(w.where)}</div>` : ''}</span><span class="green">${esc(fmt(w.value))}</span></div>`,
        )
        .join('')
    : '<div class="muted">Nothing watched.</div>';
  const plots = Object.keys(st.plots).length ? chartSvg(st.plots) : '<div class="muted">No graphs.</div>';
  return `<div class="section"><div class="label">Checks</div>${checks}</div><div class="section"><div class="label">Watching</div>${watches}</div><div class="section"><div class="label">Last 200 steps</div>${plots}</div>`;
}

// A stat: its bar, its value (typed in to change it), the zoo.exe code
// behind it
// (a value held where it's set, every update: pin testing)
function pinOf(field) {
  const s = sc();
  if (!s || !s.sel) return null;
  return (s.pins || []).find((p) => p.kind === s.sel.kind && p.id === s.sel.id && p.field === field) || null;
}

function statRow(label, field, value, { min = 0, max = 100, color = '#5cc489', fn = null, edit = true, pin = true } = {}) {
  const pct = Math.max(0, Math.min(100, ((value - min) / (max - min)) * 100));
  const on = S.drawer.open && S.drawer.field === fn ? ' on' : '';
  const s = sc();
  const canPin = edit && pin && s && s.sel && (s.sel.kind === 'animal' || s.sel.kind === 'guest');
  const pinned = canPin && pinOf(field);
  return `<div class="stat${pinned ? ' pinned' : ''}"><span class="k">${label}</span><div class="b"><div style="width:${pct}%;background:${color}"></div></div>${
    edit ? `<input class="num" data-set="${field}" value="${fmt(value)}">` : `<span class="v">${fmt(value)}</span>`
  }${
    canPin
      ? `<span class="pin${pinned ? ' on' : ''}" data-act="scPin" data-arg="${field}" title="${pinned ? 'Pinned at ' + fmt(pinned.value) + ': click to let go' : 'Pin it: held at this value every update'}">⊙</span>`
      : ''
  }${fn ? `<span class="fx${on}" data-act="openFn" data-arg="${fn}" title="The zoo.exe code behind it">ƒ</span>` : '<span class="fx-none"></span>'}</div>`;
}

// What can be done to it now: buttons, its animations and behaviours
function actionsHtml(x) {
  const btn = (action, label, title, arg = '') =>
    `<span class="dbg" data-act="scAct" data-arg="${esc(action)}|${esc(arg)}" title="${esc(title || '')}">${label}</span>`;
  const pick = (id, list, action, label) =>
    list && list.length
      ? `<div class="actrow"><select class="sel" id="${id}">${list.map((a) => `<option${SC.picked[id] === a ? ' selected' : ''}>${esc(a)}</option>`).join('')}</select><span class="dbg" data-act="scActPick" data-arg="${action}|${id}">${label}</span></div>`
      : '';
  let h = '';
  if (x.kind === 'animal')
    h =
      `<div class="acts">${btn('hungry', 'Hungry', 'Hunger to 100')}${btn('tired', 'Tired', 'Energy to its threshold: it goes to sleep')}${btn('sick', 'Sick', 'Ill, health 0')}${btn(
        'heal',
        'Heal',
        'Health full, not sick',
      )}${btn('unhappy', 'Unhappy', 'Happiness to -100')}${btn('happy', 'Happy', 'Happiness to 100')}${btn('dung', 'Dung', 'A dung pile where it stands')}${btn(
        'dart',
        'Dart',
        'Tranquilised, as a keeper’s dart')}${btn('crate', 'Crate', 'Boxed up, as caught')}</div>` +
      pick('act-anim', x.anims, 'anim', '▶ Play') +
      pick('act-set', x.sets, 'behave', '▶ Do');
  else if (x.kind === 'guest')
    h =
      `<div class="acts">${btn('hungry', 'Hungry', 'Hunger to 100')}${btn('thirst', 'Thirsty', 'Thirst to 100')}${btn('bathroom', 'Bathroom', 'Bathroom to 100')}${btn(
        'tired',
        'Tired',
        'Tired to 100',
      )}${btn('leave', 'Leave', 'Off home now')}</div>` + pick('act-anim', x.anims, 'anim', '▶ Play');
  else if (x.kind === 'staff') h = pick('act-anim', x.anims, 'anim', '▶ Play');
  else if (x.kind === 'exhibit')
    h = `<div class="acts">${btn('food', 'Food down', 'The console: food')}${btn('nofood', 'No food', 'The console: nofood')}${x.tank ? '' : btn('dung', 'Dung ×3', 'The console: dung', '3')}${btn(
      'wear',
      'Wear fences',
      'The console: wear',
    )}${btn('break', 'Break fences', 'The console: break')}${x.tank ? btn('dirty', 'Dirty water', 'The console: dirty (purity 30)', '30') : ''}</div>`;
  if (!h) return '';
  return `<div class="section"><div class="label">Actions</div>${h}</div>`;
}
// An object's footprint: what it blocks, and where that came from; none
// found is red (it blocks nothing here: zoo.exe must have a default)
function footprintHtml(x) {
  const f = x.footprint;
  if (!f) return '';
  const head = `<div class="label">Footprint <span class="clickable blue" data-act="scOverlay" data-arg="footprints" style="float:right;text-transform:none;letter-spacing:0">${
    (sc() || {}).overlays && sc().overlays.footprints ? 'hide' : 'show'
  } footprints</span></div>`;
  if (f.x > 0 && f.y > 0)
    return `<div class="section">${head}${kv('Size', `${f.x} × ${f.y} half tiles <span class="muted">(${f.x / 2} × ${f.y / 2} tiles)</span>`)}${kv(
      'From',
      `<span class="mono">${esc(f.from)}</span>`,
    )}${f.turned ? kv('Turned', 'yes: facing east or west, x and y swap') : ''}</div>`;
  return `<div class="section">${head}<div class="need bad"><div><span class="red">No footprint found</span>: its <span class="mono">${esc(
    x.file,
  )}</span> has no <span class="mono">cFootprintX/Y</span>, so here it blocks nothing: guests, staff and animals walk through it. zoo.exe must use a default when the key is missing.</div><div class="nacts"><span class="clickable" data-act="fpFind">Find cFootprintX in zoo.exe</span><span class="clickable" data-act="fpFile" data-arg="${esc(x.file)}">Its .ai in Game files</span></div></div></div>`;
}

// Where a walker's going, and how its last route search went
function routeHtml(r) {
  if (!r) return '';
  let h = `<div class="section"><div class="label">Route <span class="clickable blue" data-act="scOverlay" data-arg="routes" style="float:right;text-transform:none;letter-spacing:0">${
    (sc() || {}).overlays && sc().overlays.routes ? 'hide' : 'show'
  } routes</span></div>`;
  h += r.going ? `<div>Walking to <span class="mono">${fmt(r.goalX)}, ${fmt(r.goalY)}</span> <span class="muted">· ${r.legsLeft} leg${r.legsLeft === 1 ? '' : 's'} left</span></div>` : '<div class="muted">Not walking anywhere.</div>';
  const x = r.search;
  if (!x) h += `<div class="dim" style="font-size:12px">Its route searches show here while it’s traced (the Steps dock: Trace › What’s picked).</div>`;
  else
    h += `<div class="need${x.found ? '' : ' bad'}" style="flex-direction:column;align-items:stretch"><div>${
      x.found ? `<span class="green">Last search: found</span>` : `<span class="red">Last search: ${esc(x.why)}</span>`
    } <span class="muted mono">${x.fromX},${x.fromY} → ${x.toX},${x.toY}</span></div><div class="muted" style="font-size:12px">looked at ${x.looked}${x.gaveUp ? ` (its limit is ${x.limit})` : ''} · ${
      x.found ? `route ${x.routeTiles} tiles · ` : ''
    }couldn’t go onto ${x.blocked} · steps refused ${x.walls} · ${fmt(x.ms)} ms</div></div>`;
  return h + '</div>';
}

const kv = (k, v) => (v === undefined || v === null || v === '' ? '' : `<div class="kv"><span class="muted">${k}</span><span>${v}</span></div>`);
const flagChip = (on, text, cls = '') => (on ? `<span class="badge ${cls}">${text}</span>` : '');

function inspectorHtml() {
  const s = sc();
  if (!s) return '<div class="muted">Waiting for the engine…</div>';
  const x = s.sel;
  const c = s.counts;
  if (!x)
    return `<div class="section"><div class="label">Inspector</div><div class="muted" style="line-height:1.5">Click anything in the view (an animal, a guest, a keeper, a building, a rock, an exhibit, the ground) or in the list on the left to see it here, live, and change it.</div></div>
      <div class="section"><div class="label">In this zoo</div>${kv('Exhibits', c.exhibits)}${kv('Animals', c.animals)}${kv('Staff', c.staff)}${kv('Guests', c.guests)}${kv(
        'Objects',
        c.objects.toLocaleString(),
      )}</div>
      <div class="section"><div class="label">Keys</div><div class="keys"><span>Esc</span> select · <span>P</span> place · <span>B</span> bulldoze · <span>T</span> route<br><span>R</span> turn · <span>Del</span> remove · <span>F</span> centre<br><span>Space</span> run · <span>Ctrl+Z</span> undo · <span>G</span> grid<br>drag or arrow keys: move · wheel: zoom</div></div>`;
  const head = (kind, name, actions = '') =>
    `<div class="ihead"><div><div class="label">${kind}</div><div class="iname">${esc(name)}</div></div><div class="iacts"><span class="clickable blue" data-act="scFocus" title="Centre on it (F)">⌖</span>${actions}<span class="clickable muted" data-act="scDeselect" title="Deselect (Esc)">✕</span></div></div>`;
  const exLink = (id, name) => (id >= 0 && name ? `<span class="clickable blue" data-act="scSelect" data-arg="exhibit:${id}">${esc(name)}</span>` : '<span class="dim">loose</span>');
  if (x.kind === 'animal') {
    const happyColor = x.happiness >= 50 ? '#5cc489' : x.happiness >= 0 ? '#e8b45a' : '#f08a7a';
    return (
      head(`${x.speciesName}${x.egg ? ' (egg)' : x.baby ? ' (young)' : ''} · ${x.female ? 'female' : 'male'}`, x.name || x.speciesName, `<span class="clickable red" data-act="scRemove" title="Sell it (Del)">Sell</span>`) +
      `<div class="section">${kv('Exhibit', exLink(x.exhibit, x.exhibitName))}${kv('Doing', esc(x.doing))}${x.egg ? kv('Hatches in', `<span class="mono">${x.hatchIn} s</span>`) : ''}${kv('Where', `<span class="mono">${fmt(x.x)}, ${fmt(x.y)}</span>`)}<div class="chips">${flagChip(
        x.sick,
        'sick',
        'amber',
      )}${flagChip(x.sleeping, 'asleep', 'blue')}${flagChip(x.escaped, 'escaped', 'amber')}</div></div>
      <div class="section"><div class="label">Needs</div>${statRow('Happy', 'happiness', x.happiness, { min: -100, max: 100, color: happyColor, fn: 'happiness' })}${statRow(
        'Pending',
        'pending',
        x.pending,
        { min: -100, max: 100, color: '#6c737b' },
      )}${statRow('Hunger', 'hunger', x.hunger, { color: '#e8b45a', fn: 'hunger' })}${statRow('Food', 'food', x.food, { color: '#9ad49c' })}${statRow('Health', 'health', x.health, {
        color: '#5cc489',
        fn: 'health',
      })}${statRow('Energy', 'energy', x.energy, { color: '#6aa8ef', fn: 'energy' })}${statRow('Dirty', 'dirty', x.dirty, { color: '#c39bf0' })}${
        x.exhibit >= 0
          ? x.suitability >= 0
            ? statRow('Suits it', 'suitability', x.suitability, { color: '#7fb4f5', edit: false })
            : kv('Suits it', '<span class="dim">not while stopped</span>')
          : ''
      }</div>${actionsHtml(x)}${routeHtml(x.route)}${S.expert ? `<div class="mono dim" style="font-size:11px">${esc(x.file)} · #${x.id}</div>` : ''}`
    );
  }
  if (x.kind === 'guest')
    return (
      head(`Guest · ${x.type}`, x.name) +
      `<div class="section">${kv('Doing', esc(x.state + (x.need ? ' (wants ' + x.need + ')' : '')))}${kv('Watching', x.watching ? esc(x.watching) : '')}${kv(
        'Exhibits seen',
        x.seen,
      )}<div class="chips">${flagChip(x.souvenir, 'souvenir', 'blue')}${flagChip(x.trash, 'holding litter', 'amber')}${flagChip(x.leaving, 'leaving', 'amber')}</div></div>
      <div class="section"><div class="label">Needs</div>${statRow('Happy', 'happiness', x.happiness, { min: -100, max: 100, color: '#5cc489' })}${statRow('Hunger', 'hunger', x.hunger, {
        color: '#e8b45a',
      })}${statRow('Thirst', 'thirst', x.thirst, { color: '#6aa8ef' })}${statRow('Bathroom', 'bathroom', x.bathroom, { color: '#c39bf0' })}${statRow('Tired', 'tired', x.tired, {
        color: '#8a929b',
      })}</div>
      ${actionsHtml(x)}${routeHtml(x.route)}<div class="section"><div class="label">Thinking</div>${x.thoughts.length ? x.thoughts.map((t) => `<div class="thought">${esc(t)}</div>`).join('') : '<div class="muted">Nothing yet.</div>'}</div>`
    );
  if (x.kind === 'staff')
    return (
      head(`Staff · ${x.type}`, x.name, `<span class="clickable red" data-act="scRemove" title="Fire (Del)">Fire</span>`) +
      `<div class="section">${kv('Duty', esc(x.duty || '—'))}${kv('Job', esc(x.job))}${kv('Doing', esc(x.doing))}${kv('Where', `<span class="mono">${fmt(x.x)}, ${fmt(x.y)}</span>`)}</div>
      ${actionsHtml(x)}${routeHtml(x.route)}<div class="section"><div class="label">Looks after</div>${x.exhibits.length ? x.exhibits.map((e) => `<div>${esc(e)}</div>`).join('') : '<div class="muted">The whole zoo.</div>'}</div>`
    );
  if (x.kind === 'object')
    return (
      head(x.class === 'building' ? 'Building' : x.class, x.name, `<span class="clickable red" data-act="scRemove" title="Remove (Del)">Remove</span>`) +
      `<div class="section">${kv('Type', `<span class="mono">${esc(x.type)}</span>`)}${kv('Where', `<span class="mono">${fmt(x.x)}, ${fmt(x.y)}${x.z ? ' · ' + fmt(x.z) + ' up' : ''}</span>`)}${kv(
        'Facing',
        `${['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW'][x.facing] || x.facing} <span class="clickable blue" data-act="scTurnSel">⟳</span>`,
      )}${kv('In', x.exhibitName ? esc(x.exhibitName) : '')}${kv('Costs', x.cost ? money(x.cost) : '')}${kv('Put down', x.bought ? 'in this game' : 'with the map')}</div>${footprintHtml(x)}${
        x.price !== undefined
          ? `<div class="section"><div class="label">Business</div>${kv('Price', money(x.price))}${kv('Takings', money(x.income))}${kv('Upkeep', money(x.upkeep))}${kv('Visitors', x.visitors)}</div>`
          : ''
      }${S.expert ? `<div class="mono dim" style="font-size:11px">${esc(x.file)} · object ${x.id}</div>` : ''}`
    );
  if (x.kind === 'exhibit')
    return (
      head(x.tank ? 'Tank' : 'Exhibit', x.name) +
      `<div class="section"><div class="label">Name</div><input class="field" data-set="name" data-str="1" value="${esc(x.name)}"></div>
      <div class="section">${kv('Size', x.tiles + ' tiles')}${kv('Food down', fmt(x.food))}${kv('Dung', x.dung)}${x.tank ? kv('Water', Math.round(x.water * 100) + '%') + kv('Clean', Math.round(x.purity) + '%') + kv('Water', x.salt ? 'salt' : 'fresh') : ''}${
        x.fakeAnimals ? kv('Console animals', x.fakeAnimals) : ''
      }</div>
      <div class="section"><div class="label">Animals</div>${
        x.animals.length
          ? x.animals
              .map(
                (a) =>
                  `<div class="watch clickable" data-act="scSelect" data-arg="animal:${a.id}"><span>${esc(a.species)}</span><span><span class="dim">suits ${Math.round(
                    a.suitability,
                  )}</span> · <span class="green">${Math.round(a.happiness)}</span></span></div>`,
              )
              .join('')
          : '<div class="muted">None. Place › Animals, then click inside it.</div>'
      }</div>${actionsHtml(x)}`
    );
  if (x.kind === 'tile')
    return (
      head('Ground', `Tile ${x.x}, ${x.y}`) +
      `<div class="section">${kv('Ground', esc(x.terrainName))}${kv('Corners', `<span class="mono">${x.corners.join(' ')}</span>`)}${kv('Path', x.path ? esc(x.path) : '')}${kv(
        'Exhibit',
        x.exhibitName ? esc(x.exhibitName) : '',
      )}${kv('In the zoo', x.inZoo ? 'yes' : 'no')}${kv('Objects on it', x.objects)}</div>`
    );
  return '';
}

// ------------------------------------------------------------- the dock
// Under the view: Steps (the engine's code as it runs, with zoo.exe's
// beside it), Log (what the engine says) and Console (the game's own)
const DK = {
  tab: store.get('dkTab', 'steps'),
  height: store.get('dkHeight', 300),
  steps: [], // the engine's steps, newest last
  byN: new Map(),
  cur: null, // the step shown (a number)
  follow: true, // show each new one as it comes
  paused: null, // the step it's stopped at
  dropped: 0,
  src: {}, // file -> its lines ('' while asked for)
  bps: new Set(store.get('dkBps', [])), // "src/Animals.cpp:2065"
  frame: null, // a frame of the stack picked to look at: {file, line, zoo}
  filter: '',
  listAt: 0,
};

// (a build changes the code: its files read again, so steps show the lines
// the new engine was built from)
lab.onBuildDone(() => {
  DK.src = {};
});

function dkStep(n) {
  return DK.byN.get(+n) || null;
}
function valText(v) {
  if (v.s !== undefined) return esc(v.s);
  const num = (x) => (Math.abs(x - Math.round(x)) < 1e-6 ? String(Math.round(x)) : (+x).toFixed(2));
  if (v.f !== undefined && v.v !== undefined) {
    const d = v.v - v.f;
    return `${num(v.f)} → <b>${num(v.v)}</b>${d ? ` <span class="${d > 0 ? 'up' : 'down'}">(${d > 0 ? '+' : ''}${num(d)})</span>` : ''}`;
  }
  return v.v !== undefined ? num(v.v) : '';
}
const valsText = (st, max = 3) =>
  (st.vals || [])
    .slice(0, max)
    .map((v) => `<span class="vk">${esc(v.n)}</span> ${valText(v)}`)
    .join('<span class="dim"> · </span>');

// (panels on: layout.js lays the Scene out, the dock's parts in panels)
function dockedOn() {
  return typeof LAYOUT !== 'undefined' && LAYOUT.on && !!LAYOUT.api;
}

// The stepping controls (Trace, stop at each step, step/over/out) and what
// it's stopped at
function dkControls() {
  const st = S.engine.state;
  const s = sc();
  const tr = (s && s.trace) || {};
  const paused = DK.paused;
  let ctl = `<span class="muted">Trace</span><div class="seg mini">${[
    ['off', 'Off'],
    ['selected', 'What’s picked'],
    ['all', 'Everything'],
  ]
    .map(([m, l]) => `<div class="${(tr.mode || 'selected') === m ? 'on' : ''}" data-act="dkTrace" data-arg="${m}">${l}</div>`)
    .join('')}</div><span class="opt2${tr.stepEach ? ' on' : ''}" data-act="dkStepEach" title="Stop at every step traced">⏸ Stop at each step</span>`;
  ctl += paused
    ? `<span class="sep2"></span><span class="dbg go" data-act="dkGo" data-arg="continue" title="Continue (F8)">▶ Continue</span><span class="dbg" data-act="dkGo" data-arg="stepOver" title="Step over (F10): the next step here or out">⤼ Over</span><span class="dbg" data-act="dkGo" data-arg="stepNext" title="Step (F11): the very next step">↓ Step</span><span class="dbg" data-act="dkGo" data-arg="stepOut" title="Step out (Shift+F11)">↑ Out</span>`
    : `<span class="sep2"></span><span class="dbg" data-act="dkGo" data-arg="stepNext" title="Stop at the next step traced (F11)">⏯ Stop at the next</span>`;
  const right = paused
    ? `<span class="amber">⏸ Stopped: ${esc(paused.what)} · ${esc(paused.whoName)}</span>`
    : st && st.loaded
      ? `<span class="mono dim">${esc(st.script)}: step ${st.step}${st.live ? '' : ' (finished)'}</span>`
      : s
        ? `<span class="mono dim">${DK.steps.length ? DK.steps.length.toLocaleString() + ' steps' : ''}${DK.dropped ? ` · ${DK.dropped.toLocaleString()} too fast to show` : ''}</span>`
        : '';
  return { ctl, right };
}

function renderConsole() {
  if (dockedOn()) return renderDocked();
  const st = S.engine.state;
  const s = sc();
  const tr = (s && s.trace) || {};
  const paused = DK.paused;
  const tabs = [
    ['steps', 'Steps'],
    ['try', 'Try zoo.exe'],
    ['log', 'Log'],
    ['console', 'Console'],
  ]
    .map(([id, l]) => `<div class="${DK.tab === id && S.consoleOpen ? 'on' : ''}" data-act="dkTab" data-arg="${id}">${l}</div>`)
    .join('');
  let ctl = '';
  if ((DK.tab === 'steps' || paused) && DK.tab !== 'try') {
    ctl += `<span class="muted">Trace</span><div class="seg mini">${[
      ['off', 'Off'],
      ['selected', 'What’s picked'],
      ['all', 'Everything'],
    ]
      .map(([m, l]) => `<div class="${(tr.mode || 'selected') === m ? 'on' : ''}" data-act="dkTrace" data-arg="${m}">${l}</div>`)
      .join('')}</div><span class="opt2${tr.stepEach ? ' on' : ''}" data-act="dkStepEach" title="Stop at every step traced">⏸ Stop at each step</span>`;
    ctl += paused
      ? `<span class="sep2"></span><span class="dbg go" data-act="dkGo" data-arg="continue" title="Continue (F8)">▶ Continue</span><span class="dbg" data-act="dkGo" data-arg="stepOver" title="Step over (F10): the next step here or out">⤼ Over</span><span class="dbg" data-act="dkGo" data-arg="stepNext" title="Step (F11): the very next step">↓ Step</span><span class="dbg" data-act="dkGo" data-arg="stepOut" title="Step out (Shift+F11)">↑ Out</span>`
      : `<span class="sep2"></span><span class="dbg" data-act="dkGo" data-arg="stepNext" title="Stop at the next step traced (F11)">⏯ Stop at the next</span>`;
  }
  const right = paused
    ? `<span class="amber">⏸ Stopped: ${esc(paused.what)} · ${esc(paused.whoName)}</span>`
    : st && st.loaded
      ? `<span class="mono dim">${esc(st.script)}: step ${st.step}${st.live ? '' : ' (finished)'}</span>`
      : s
        ? `<span class="mono dim">${DK.steps.length ? DK.steps.length.toLocaleString() + ' steps' : ''}${DK.dropped ? ` · ${DK.dropped.toLocaleString()} too fast to show` : ''}</span>`
        : '';
  setHtml(
    $('consolebar'),
    `<div class="dkgrip" id="dk-grip" title="Drag to resize"></div><span class="clickable dkfold" data-act="toggleConsole">${S.consoleOpen ? '▾' : '▸'}</span><div class="seg mini dktabs">${tabs}</div>${ctl}<div class="spacer"></div>${right}`,
  );
  const grip = $('dk-grip');
  if (grip && !grip.onmousedown) grip.onmousedown = dkResize;
  const c = $('console');
  c.classList.toggle('hidden', !S.consoleOpen);
  c.style.height = DK.height + 'px';
  if (!S.consoleOpen) return;
  if (c.dataset.tab !== DK.tab) {
    c.dataset.tab = DK.tab;
    c._h = null;
    if (DK.tab === 'steps')
      c.innerHTML = stepsLayoutHtml(); // (stepview.js: the list, the step, its panes)
    else if (DK.tab === 'try') c.innerHTML = `<div id="try" class="try"></div>`;
    else if (DK.tab === 'log') c.innerHTML = `<div id="con-log" class="conlog"></div>`;
    else c.innerHTML = `<div id="con-log" class="conlog"></div><div class="conin"><span class="green">›</span><input id="con-in" spellcheck="false" placeholder="The game’s console: type help"></div>`;
    if ($('con-in')) $('con-in').onkeydown = conKey;
    if ($('dk-q'))
      $('dk-q').oninput = (e) => {
        DK.filter = e.target.value.toLowerCase();
        renderSteps(true);
      };
  }
  if (DK.tab === 'steps') return renderSteps();
  if (DK.tab === 'try') return renderTry();
  const log = $('con-log');
  const atEnd = log.scrollTop + log.clientHeight >= log.scrollHeight - 20;
  setHtml(
    log,
    (DK.tab === 'console' ? SC.conOut : S.engine.log.slice(-600))
      .map((l) => (l.startsWith('› ') ? `<span class="cmd">${esc(l)}</span>` : /FAIL|error|crashed|couldn't/i.test(l) ? `<span class="bad">${esc(l)}</span>` : esc(l)))
      .join('\n') || (DK.tab === 'console' ? '<span class="dim">The game’s dev console: type help below.</span>' : ''),
  );
  if (atEnd) log.scrollTop = log.scrollHeight;
}

function dkResize(e) {
  const y0 = e.clientY,
    h0 = DK.height;
  const move = (ev) => {
    DK.height = Math.max(120, Math.min(window.innerHeight - 260, h0 + (y0 - ev.clientY)));
    $('console').style.height = DK.height + 'px';
    sizeCanvas();
  };
  const up = () => {
    window.removeEventListener('mousemove', move);
    window.removeEventListener('mouseup', up);
    store.set('dkHeight', DK.height);
  };
  window.addEventListener('mousemove', move);
  window.addEventListener('mouseup', up);
  e.preventDefault();
}

// The steps list, the source at the step shown, zoo.exe's function
let stepsTimer = null;
function renderSteps(now) {
  if (!now) {
    if (!stepsTimer) stepsTimer = setTimeout(() => ((stepsTimer = null), renderSteps(true)), 120);
    return;
  }
  const list = $('dk-list');
  if (!list) return;
  const q = DK.filter;
  const rows = (q ? DK.steps.filter((x) => `${x.what} ${x.whoName} ${x.fn} ${x.file} ${(x.vals || []).map((v) => v.n).join(' ')}`.toLowerCase().includes(q)) : DK.steps).slice(-400);
  const atEnd = list.scrollTop + list.clientHeight >= list.scrollHeight - 30;
  const s = sc();
  setHtml(
    list,
    rows.length
      ? rows
          .map(
            (x) =>
              `<div class="srow${x.n === DK.cur ? ' on' : ''}${x.paused ? ' stop' : ''}${x.enter ? ' enter' : ''}" data-act="dkPick" data-arg="${x.n}"><span class="t">${fmtTime(
                x.time,
              )}</span><span class="who">${esc(x.whoName)}</span><span class="w" style="padding-left:${Math.min(6, Math.max(0, x.depth - 1)) * 10}px">${x.enter ? '▸ ' : ''}${esc(
                x.what,
              )}</span><span class="vv">${valsText(x, 2)}</span></div>`,
          )
          .join('')
      : `<div class="muted" style="padding:10px;line-height:1.5;white-space:normal">${
          !s || (s.trace || {}).mode === 'off'
            ? 'Tracing is off.'
            : (s.trace || {}).mode === 'selected' && !s.sel
              ? 'Pick something in the view (an animal, an exhibit…) and run the zoo: its code shows here as it runs. Using a tool shows the tool’s code.'
              : 'Nothing yet: run the zoo (▶), or use a tool.'
        }</div>`,
  );
  if (DK.follow && atEnd) list.scrollTop = list.scrollHeight;
  renderStepSource();
  renderStepView();
}
const fmtTime = (t) => (t || 0).toFixed(1) + 's';

function renderStepSource() {
  const st = dkStep(DK.cur);
  const head = $('dk-srchead'),
    box = $('dk-src'),
    zh = $('dk-zoohead'),
    zb = $('dk-zoo');
  if (!head) return;
  if (!st) {
    setHtml(head, '<span class="muted">The engine’s code</span>');
    setHtml(box, '<div class="muted" style="padding:10px 14px;font-family:var(--sans)">Pick a step to see its line here.</div>');
    setHtml(zh, '<span class="muted">zoo.exe</span>');
    setHtml(zb, '');
    return;
  }
  // the line looked at: the step's, or a frame of its stack picked
  const at = DK.frame || st;
  const crumbs = (st.stack || [])
    .map(
      (f, i) =>
        `<span class="crumb${DK.frame && DK.frame.i === i ? ' on' : ''}" data-act="dkFrame" data-arg="${i}" title="${esc(f.file)}:${f.line}">${esc(shortFn(f.fn))}${
          f.what ? ` <span class="dim">(${esc(f.what)})</span>` : ''
        }</span>`,
    )
    .join('<span class="dim"> › </span>');
  setHtml(
    head,
    `<div class="crumbs">${crumbs || esc(shortFn(st.fn))}</div><span class="mono dim clickable" data-act="engOpen" data-arg="${esc(at.file)}:${at.line}" title="Open in Engine code">${esc(
      at.file,
    )}:${at.line} ↗</span>`,
  );
  const lines = DK.src[at.file];
  if (lines === undefined) {
    DK.src[at.file] = '';
    lab.srcReadAll(at.file).then((r) => {
      DK.src[at.file] = r ? r.text.replace(/\r/g, '').split('\n') : ['(couldn’t read ' + at.file + ')'];
      renderStepSource();
    });
  }
  if (!lines) setHtml(box, '<div class="muted" style="padding:10px 14px">Reading…</div>');
  else {
    const from = Math.max(1, at.line - 18),
      to = Math.min(lines.length, at.line + 14);
    // what ran since its last step in this function: from that line on
    let ranFrom = 0;
    if (!DK.frame) {
      const i = DK.steps.indexOf(st);
      for (let j = i - 1; j >= 0 && j > i - 50; j--) {
        const p = DK.steps[j];
        if (p.who !== st.who || p.whoId !== st.whoId) continue;
        if (p.file === st.file && p.fn === st.fn && p.line < st.line && st.line - p.line < 60) ranFrom = p.line + 1;
        break;
      }
    }
    // (the trace calls themselves dimmed: the game's code stands out)
    const traceLine = [];
    for (let i = from - 6, open = false; i <= to; i++) {
      const t = (lines[i - 1] || '').trim();
      const starts = /^ZT_(STEP|FN|WHO)\(/.test(t);
      traceLine[i] = starts || open;
      if (starts) open = !/\);\s*$/.test(t);
      else if (open && /\);\s*$/.test(t)) open = false;
    }
    let h = '';
    for (let i = from; i <= to; i++) {
      const key = `${at.file}:${i}`;
      const here = i === at.line;
      const ran = ranFrom && i >= ranFrom && i < at.line && !traceLine[i];
      const live = here && !DK.frame && (st.vals || []).length ? `<div class="inl">${(st.vals || []).map((v) => `<span><span class="vk">${esc(v.n)}</span> ${valText(v)}</span>`).join('')}</div>` : '';
      h += `<div class="ln${here ? ' hl' : ''}${ran ? ' ran' : ''}${traceLine[i] && !here ? ' tr' : ''}${DK.paused && here && DK.paused.n === st.n ? ' stopline' : ''}"><span class="no bp${DK.bps.has(key) ? ' on' : ''}" data-act="dkBp" data-arg="${esc(
        key,
      )}" title="Stop here (F9)">${i}</span><span>${highlightC(lines[i - 1] || ' ')}</span></div>${live}`;
    }
    setHtml(box, h);
    const hl = box.querySelector('.ln.hl');
    if (hl && box._line !== `${at.file}:${at.line}`) {
      box._line = `${at.file}:${at.line}`;
      // (the line a third of the way down, its values under it in view)
      const y = hl.getBoundingClientRect().top - box.getBoundingClientRect().top + box.scrollTop;
      box.scrollTop = Math.max(0, y - box.clientHeight / 3);
    }
  }
  // zoo.exe: the step's function, else the nearest frame's with one
  let zoo = at.zoo || st.zoo;
  if (!zoo) for (let i = (st.stack || []).length - 1; i >= 0 && !zoo; i--) zoo = st.stack[i].zoo;
  if (!zoo) {
    setHtml(zh, '<span class="muted">zoo.exe</span>');
    setHtml(zb, `<div class="muted" style="padding:10px 14px;font-family:var(--sans);white-space:normal;line-height:1.5">This code isn’t tied to a zoo.exe function yet. When you find the one it copies, add its address to the step (<span class="mono">ZT_STEP("0x…", …)</span>).</div>`);
    return;
  }
  const f = fnInfo(zoo);
  setHtml(
    zh,
    `<span><span class="mono amber">${esc(f.addr)}</span> ${esc(displayName(f))}</span><span class="dklinks"><span class="clickable blue" data-act="openCode" data-arg="${f.addr}">Open in Code</span><span class="clickable green" data-act="tryFn" data-arg="${f.addr}" title="Run the original on what's picked">Try it here</span></span>`,
  );
  const path = '/decompile?addr=' + plainHex(f.addr);
  const r = gNow(path);
  if (!r) {
    setHtml(zb, '<div class="muted" style="padding:10px 14px">Asking Ghidra…</div>');
    G(path).then(() => renderStepSource());
    return;
  }
  if (r.error) return setHtml(zb, `<div class="muted" style="padding:10px 14px;font-family:var(--sans);white-space:normal">Ghidra isn’t connected, and this function hasn’t been looked at before. In Ghidra: Tools › zt1-engine › Start Lab bridge.</div>`);
  CTX = contextStruct(f);
  const zl = (r.json.c || '').replace(/\r/g, '').split('\n');
  while (zl.length && !zl[0].trim()) zl.shift();
  // (lines that use what the step names - a field, a constant - lit)
  const names = (st.vals || []).map((v) => v.n.toLowerCase().replace(/^c/, ''));
  setHtml(
    zb,
    zl
      .map((l, i) => {
        const hit = names.some((n) => n.length > 3 && l.toLowerCase().includes(n));
        return `<div class="ln${hit ? ' hl2' : ''}"><span class="no">${i + 1}</span><span>${highlightC(l, fieldLookup)}</span></div>`;
      })
      .join(''),
  );
}
const shortFn = (fn) => (fn || '').replace(/^.*?([A-Za-z_]\w*::[A-Za-z_~]\w*)$/, '$1').replace(/<lambda_[^>]*>/, 'λ');

function onStepMsg(m) {
  DK.steps.push(m);
  DK.byN.set(m.n, m);
  // (the last 30,000 kept - a minute and a half with everything traced, the
  // guests and all - so a filter still finds what happened at the start of
  // a recording; only the last 400 are ever drawn)
  if (DK.steps.length > 30000) for (const x of DK.steps.splice(0, 5000)) DK.byN.delete(x.n);
  if (m.paused) {
    DK.paused = m;
    DK.cur = m.n;
    DK.frame = null;
    if (!S.consoleOpen || DK.tab !== 'steps') {
      S.consoleOpen = true;
      DK.tab = 'steps';
      requestAnimationFrame(sizeCanvas);
    }
    if (S.tab !== 'scene') setTab('scene');
    renderConsole();
    renderSteps(true);
    renderStageUi();
    return;
  }
  if (DK.follow && !DK.paused) {
    DK.cur = m.n;
    DK.frame = null;
  }
  if (S.tab === 'scene' && (dockedOn() ? $('dk-list') : S.consoleOpen && DK.tab === 'steps')) renderSteps();
}
function conKey(e) {
  const inp = e.target;
  if (e.key === 'Enter' && inp.value.trim()) {
    const line = inp.value.trim();
    SC.conHist = [line, ...SC.conHist.filter((x) => x !== line)].slice(0, 50);
    store.set('scConHist', SC.conHist);
    SC.conAt = -1;
    lab.engineSend({ cmd: 'console', line });
    inp.value = '';
  } else if (e.key === 'ArrowUp' || e.key === 'ArrowDown') {
    SC.conAt = Math.max(-1, Math.min(SC.conHist.length - 1, SC.conAt + (e.key === 'ArrowUp' ? 1 : -1)));
    inp.value = SC.conAt >= 0 ? SC.conHist[SC.conAt] : '';
    e.preventDefault();
  } else if (e.key === 'Escape') inp.blur();
}

// ------------------------------------------------------------ the view
function renderScene() {
  ensureStage();
  renderSceneLeft(true);
  renderStageUi();
  if (S.drawer.open) renderDrawer();
  else renderRight();
  renderConsole();
  const s = sc();
  if (S.engine.status === 'connected') {
    if (!SC.catalog && !SC.catalogAsked) {
      SC.catalogAsked = true;
      lab.engineSend({ cmd: 'catalog' });
    }
    if (!SC.outline || !s) lab.engineSend({ cmd: 'outline' });
  }
}

// live parts only (the engine's state comes 6 times a second)
function renderLive() {
  if (S.tab === 'scene') {
    renderStageUi();
    if (!S.drawer.open) renderRight();
    else liveDrawerValues();
    renderConsole();
    if (SC.left === 'mods') renderSceneLeft();
    else {
      const s = sc();
      if (s) {
        setHtml(
          $('sc-map'),
          `<div><div class="name">${esc(s.mapName || s.map)}</div><div class="sub">${s.flat ? 'made flat · ' : ''}change map ▾</div></div>`,
        );
        // (the picked one lit in the list)
        const k = s.sel ? s.sel.kind + ':' + s.sel.id : '';
        if (k !== SC.selKey) {
          SC.selKey = k;
          renderTree();
        }
      }
    }
    checkCounts();
  } else if (S.tab === 'mods') renderBuildPanel();
}

// ---- the stat bars' code (a drawer over the right side): ƒ beside a stat
function currentAnimal() {
  const st = S.engine.state;
  if (!st) return null;
  const s = st.scene;
  if (s && s.sel && s.sel.kind === 'animal') return st.animals.find((a) => a.id === s.sel.id) || null;
  return st.animals.find((a) => a.id === S.sel.animal) || st.animals.find((a) => a.lab) || null;
}

function chartSvg(plots) {
  const names = Object.keys(plots);
  if (!names.length) return '';
  let lo = Infinity,
    hi = -Infinity,
    n = 0;
  for (const k of names)
    for (const v of plots[k]) {
      lo = Math.min(lo, v);
      hi = Math.max(hi, v);
      n = Math.max(n, plots[k].length);
    }
  if (hi - lo < 1e-6) hi = lo + 1;
  const W = 230,
    H = 64;
  const lines = names
    .map((k, i) => {
      const v = plots[k];
      const pts = v.map((y, j) => `${((j / Math.max(1, n - 1)) * W).toFixed(1)},${(H - 4 - ((y - lo) / (hi - lo)) * (H - 8)).toFixed(1)}`);
      return `<polyline points="${pts.join(' ')}" fill="none" stroke="${CHART_COLORS[i % CHART_COLORS.length]}" stroke-width="2" vector-effect="non-scaling-stroke"></polyline>`;
    })
    .join('');
  return `<div class="chart"><svg width="100%" height="100%" viewBox="0 0 ${W} ${H}" preserveAspectRatio="none">${lines}</svg></div><div class="legend">${names
    .map((k, i) => `<span><i style="background:${CHART_COLORS[i % CHART_COLORS.length]}"></i>${esc(k)} ${esc(fmt(plots[k][plots[k].length - 1]))}</span>`)
    .join('')}</div>`;
}

const fmt = (v) => (v === undefined || v === null ? '' : Math.abs(v - Math.round(v)) < 1e-6 ? String(Math.round(v)) : (+v).toFixed(2));

function liveValueFor(field) {
  const a = currentAnimal();
  if (!a || !(field in a)) return null;
  return a[field];
}

function renderDrawer() {
  const right = $('scene-right');
  right._h = null;
  right.className = 'card side drawer';
  const f = fnInfo(S.drawer.addr);
  const a = currentAnimal();
  const path = '/decompile?addr=' + plainHex(f.addr);
  const r = gNow(path);
  if (!r) G(path).then(() => S.drawer.open && renderDrawer());
  const field = S.drawer.field ? animalFieldOffset(STAT_FIELD[S.drawer.field]) : null;
  CTX = contextStruct(f);
  const plain = f.kb ? [f.kb.comment, f.kb.summary].filter(Boolean)[0] : '';
  let code = '';
  if (!r) code = `<div class="muted" style="padding:10px 14px">Asking Ghidra…</div>`;
  else if (r.error)
    code = `<div class="muted" style="padding:10px 14px;line-height:1.5;white-space:normal">Ghidra isn’t connected, and this function hasn’t been looked at before. In Ghidra: Tools › zt1-engine › Start Lab bridge.</div>`;
  else {
    const lines = (r.json.c || '').replace(/\r/g, '').split('\n');
    while (lines.length && !lines[0].trim()) lines.shift();
    code = lines
      .map((l, i) => {
        // a line using the stat's field: lit, with the animal's live value
        const offs = [...l.matchAll(/\+\s*(0x[0-9a-f]+)\)/gi)].map((m) => m[1].toLowerCase());
        const hit = field && offs.some((o) => parseInt(o, 16) === parseInt(field, 16));
        let live = '';
        if (CTX === 'ZTAnimal' && /param_1|this/.test(l))
          for (const o of offs)
            for (const fd of fieldsAt(o))
              if (fd.struct === 'ZTAnimal' && a && fd.name in a) live = `<span class="live">${esc(fd.name)} ${fmt(a[fd.name])}</span>`;
        return `<div class="ln${hit ? ' hl' : ''}"><span class="no">${i + 1}</span><span>${highlightC(l, fieldLookup)}${live}</span></div>`;
      })
      .join('');
    if (r.cached) code = `<div class="dim" style="padding:0 14px 6px;font-family:var(--sans)">(saved copy: Ghidra isn't connected)</div>` + code;
  }
  let asm = '';
  if (S.expert) {
    const dp = '/disasm?addr=' + plainHex(f.addr);
    const d = gNow(dp);
    if (!d) G(dp).then(() => S.drawer.open && renderDrawer());
    else if (d.json && d.json.lines) {
      let rows = d.json.lines;
      if (field) rows = rows.filter((x) => x.text.toLowerCase().includes(field.replace(/^0x0*/, '0x')));
      asm = `<div style="margin:12px 0 0;padding:10px 0 0;border-top:1px solid var(--line)" class="code asm">${rows
        .slice(0, 8)
        .map((x) => `<div class="ln"><span class="k-addr">${esc(x.at.slice(2))}</span>&nbsp; ${highlightAsm(x.text)}</div>`)
        .join('')}</div>`;
    }
  }
  const mods = modsFor(f.addr);
  right.innerHTML = `<div class="dhead"><b>${esc(displayName(f))}</b><div class="links"><span class="blue clickable" data-act="openCode" data-arg="${f.addr}">Open in Original Game</span><span class="clickable" data-act="closeDrawer">✕</span></div></div>
    <div class="plain">${esc(plain || 'Not described yet. Name it and add a note in Original Game.')}${a ? ` <span class="muted">Live values are for the ${esc(prettyStem(a.species))}.</span>` : ''}</div>
    <div class="code" style="flex:1;padding:10px 0">${code}${asm}</div>
    <div class="dfoot"><div class="btn primary" data-act="modFor" data-arg="${f.addr}">${mods.length ? 'Open its experiment' : 'New experiment'}</div><div class="btn" data-act="openCode" data-arg="${f.addr}">${
      S.expert ? 'Assembly' : 'Full code'
    }</div></div>`;
}

// The struct a function's own object (param_1, this) is: its class in the
// knowledge base, else an animal for an animal's function. Only that
// struct's fields label its offsets: an offset alone doesn't say which
// struct it's in
function contextStruct(f) {
  if (f.cls && S.kb && S.kb.structs[f.cls]) return f.cls;
  const text = `${f.name} ${f.kb ? f.kb.summary || '' : ''}`;
  if (/^animal|ZTAnimal::/i.test(f.name) || (systemOf(text) === 'Animals' && !/species/i.test(text))) return 'ZTAnimal';
  if (/^(exhibit|tank)/i.test(f.name)) return 'ZTHabitat';
  return null;
}
let CTX = null; // (the struct of the code being drawn)
function fieldLookup(offHex, base) {
  if (!CTX || !/^(param_1|this)$/.test(base || '')) return null;
  const f = fieldsAt(offHex).find((x) => x.struct === CTX);
  if (!f) return null;
  return { key: `${f.struct}|${offHex}`, name: f.name, title: `${f.struct}.${f.name} (${f.type})${f.summary ? ': ' + f.summary : ''}` };
}

function liveDrawerValues() {
  const a = currentAnimal();
  if (!a) return;
  document.querySelectorAll('#scene-right .live').forEach((el) => {
    const [name] = el.textContent.split(' ');
    if (name in a) el.textContent = `${name} ${fmt(a[name])}`;
  });
}

// ---- the canvas: the engine's frames, and the mouse over them
const canvas = $('canvas');
const ctx2d = canvas.getContext('2d');
let lastViewport = '';

// The view bar (top right) kept clear of the tools (top left): in a narrow
// Scene panel it takes what's left and wraps onto a second row, rather than
// lying over the tools
function fitStageBars() {
  const t = $('st-tools'), v = $('st-view');
  if (!t || !v || !v.parentElement) return;
  const room = v.parentElement.clientWidth - (t.classList.contains('hidden') ? 0 : t.offsetWidth) - 36;
  v.style.flexWrap = 'wrap';
  v.style.justifyContent = 'flex-end';
  v.style.maxWidth = Math.max(200, room) + 'px';
}

function sizeCanvas() {
  fitStageBars();
  const r = $('stage').getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  const w = Math.max(64, Math.round(r.width * dpr)),
    h = Math.max(64, Math.round(r.height * dpr));
  if (canvas.width !== w || canvas.height !== h) {
    canvas.width = w;
    canvas.height = h;
  }
  const v = `${w}x${h}`;
  if (v !== lastViewport) {
    lastViewport = v;
    lab.engineSend({ cmd: 'viewport', w, h });
  }
}

// The engine's frames: raw pixels (RGBA) put straight on the canvas, at its
// size (else through a canvas of the frame's size, stretched). The next is
// asked for as this one comes, so the engine draws it while this is shown -
// only while the Scene is seen
let frameCanvas = null;
lab.onFrame(async (f) => {
  if (S.tab === 'scene') lab.engineSend({ cmd: 'frame' });
  try {
    if (f.fmt === 'rgba') {
      const px = new Uint8ClampedArray(f.data.buffer, f.data.byteOffset, f.data.byteLength);
      const img = new ImageData(px, f.w, f.h);
      if (f.w === canvas.width && f.h === canvas.height) ctx2d.putImageData(img, 0, 0);
      else {
        if (!frameCanvas || frameCanvas.width !== f.w || frameCanvas.height !== f.h) frameCanvas = new OffscreenCanvas(f.w, f.h);
        frameCanvas.getContext('2d').putImageData(img, 0, 0);
        ctx2d.drawImage(frameCanvas, 0, 0, canvas.width, canvas.height);
      }
    } else {
      const bmp = await createImageBitmap(new Blob([f.data], { type: 'image/jpeg' }));
      ctx2d.drawImage(bmp, 0, 0, canvas.width, canvas.height);
      bmp.close();
    }
  } catch {}
});

// Left button: with Select, a click picks and a drag moves the view; with a
// tool, press, drag and release are the tool's (as in the game). Right or
// middle drag always moves the view; a right click stops the tool.
let drag = null;
let movePending = null;
const canvasXY = (e) => {
  const r = canvas.getBoundingClientRect();
  const dpr = window.devicePixelRatio || 1;
  return { x: Math.round((e.clientX - r.left) * dpr), y: Math.round((e.clientY - r.top) * dpr) };
};
const sceneView = () => ((S.engine.state || {}).view || 'scene') === 'scene';
const toolOn = () => {
  const s = sc();
  return s && s.tool.kind !== 'select';
};

canvas.addEventListener('contextmenu', (e) => e.preventDefault());
canvas.addEventListener('mousedown', (e) => {
  if (document.activeElement && document.activeElement.blur) document.activeElement.blur();
  const p = canvasXY(e);
  drag = { x: e.clientX, y: e.clientY, button: e.button, moved: false, tool: e.button === 0 && toolOn() && sceneView() };
  if (drag.tool) lab.engineSend({ cmd: 'mouse', ev: 'down', ...p });
  else canvas.classList.add('drag');
});
window.addEventListener('mousemove', (e) => {
  if (drag) {
    const dx = e.clientX - drag.x,
      dy = e.clientY - drag.y;
    if (Math.abs(dx) + Math.abs(dy) > 3) drag.moved = true;
    if (!drag.tool && drag.moved && sceneView()) {
      const dpr = window.devicePixelRatio || 1;
      lab.engineSend({ cmd: 'pan', dx: dx * dpr, dy: dy * dpr });
      drag.x = e.clientX;
      drag.y = e.clientY;
      return;
    }
  }
  if (e.target !== canvas && !(drag && drag.tool)) return;
  // (the cursor's place, once a frame at most)
  const first = !movePending;
  movePending = canvasXY(e);
  if (first)
    requestAnimationFrame(() => {
      if (movePending) lab.engineSend({ cmd: 'mouse', ev: 'move', ...movePending });
      movePending = null;
    });
});
window.addEventListener('mouseup', (e) => {
  if (!drag) return;
  canvas.classList.remove('drag');
  const p = canvasXY(e);
  if (drag.tool) lab.engineSend({ cmd: 'mouse', ev: 'up', ...p });
  else if (!drag.moved && e.target === canvas) {
    if (drag.button === 0 && sceneView()) lab.engineSend({ cmd: 'mouse', ev: 'click', ...p });
    else if (drag.button === 2) {
      if (toolOn()) lab.engineSend({ cmd: 'mouse', ev: 'cancel', ...p });
      else lab.engineSend({ cmd: 'select', kind: '' });
    }
  }
  drag = null;
});
canvas.addEventListener('mouseleave', () => !drag && lab.engineSend({ cmd: 'mouse', ev: 'leave', x: 0, y: 0 }));
canvas.addEventListener('wheel', (e) => {
  e.preventDefault();
  lab.engineSend({ cmd: 'zoom', dir: e.deltaY < 0 ? 1 : -1 });
});

// Keys over the Scene view (not while typing in a box)
// (the view moves the way the key points: the camera, not the ground)
const PAN_KEYS = { ArrowLeft: [-1, 0], ArrowRight: [1, 0], ArrowUp: [0, -1], ArrowDown: [0, 1], a: [-1, 0], d: [1, 0], w: [0, -1], s: [0, 1] };
document.addEventListener('keydown', (e) => {
  if (S.tab !== 'scene' || !$('modal').classList.contains('hidden')) return;
  const a = document.activeElement;
  if (a && (a.tagName === 'INPUT' || a.tagName === 'TEXTAREA' || a.isContentEditable)) return;
  const s = sc();
  if (!s) return;
  const k = e.key;
  // (stepping: as a debugger's keys)
  if (k === 'F8' || k === 'F10' || k === 'F11' || k === 'F9') {
    if (k === 'F9') {
      const st = dkStep(DK.cur);
      const at = DK.frame || st;
      if (at) ACTIONS.dkBp(`${at.file}:${at.line}`);
    } else ACTIONS.dkGo(k === 'F8' ? 'continue' : k === 'F10' ? 'stepOver' : e.shiftKey ? 'stepOut' : 'stepNext');
    e.preventDefault();
    return;
  }
  if ((e.ctrlKey || e.metaKey) && k.toLowerCase() === 'z') ACTIONS.scUndo();
  else if (e.ctrlKey || e.metaKey || e.altKey) return;
  else if (k === 'Escape') {
    if (SC.palette.open && s.tool.kind === 'select') ACTIONS.scPalette();
    else if (s.tool.kind !== 'select') ACTIONS.scTool('select');
    else ACTIONS.scDeselect();
  } else if (k === 'Delete' || k === 'Backspace') ACTIONS.scRemove();
  else if (k === ' ') ACTIONS.togglePlay();
  else if (k === 'p' || k === 'P') ACTIONS.scPalette();
  else if (k === 'b' || k === 'B') ACTIONS.scTool(s.tool.kind === 'bulldoze' ? 'select' : 'bulldoze');
  else if (k === 't' || k === 'T') ACTIONS.scTool(s.tool.kind === 'route' ? 'select' : 'route');
  else if (k === 'r' || k === 'R') s.tool.kind === 'object' ? ACTIONS.scOpt('turn') : ACTIONS.rotate();
  else if (k === 'f' || k === 'F') ACTIONS.scFocus();
  else if (k === 'Tab' && (s.tool.kind === 'fence' || s.tool.kind === 'path')) ACTIONS.scOpt(e.shiftKey ? 'lineModeBack' : 'lineMode');
  else if (k === 'g' || k === 'G') ACTIONS.scOverlay('grid');
  else if (PAN_KEYS[k.length === 1 ? k.toLowerCase() : k]) {
    panHeld.add(k.length === 1 ? k.toLowerCase() : k);
    if (!panLoop) panLoop = requestAnimationFrame(panTick);
  } else if (k === '+' || k === '=') lab.engineSend({ cmd: 'zoom', dir: 1 });
  else if (k === '-') lab.engineSend({ cmd: 'zoom', dir: -1 });
  else return;
  e.preventDefault();
});

// Keys held: the view moves while they're down, smoothly (about 900 screen
// pixels a second, as the game's edge scrolling)
const panHeld = new Set();
let panLoop = 0,
  panLast = 0;
function panTick(t) {
  const dt = panLast ? Math.min(0.05, (t - panLast) / 1000) : 0.016;
  panLast = t;
  if (!panHeld.size || S.tab !== 'scene') {
    panLoop = 0;
    panLast = 0;
    return;
  }
  let dx = 0,
    dy = 0;
  for (const k of panHeld) {
    dx += PAN_KEYS[k][0];
    dy += PAN_KEYS[k][1];
  }
  const step = 900 * dt * (window.devicePixelRatio || 1);
  if (dx || dy) lab.engineSend({ cmd: 'pan', dx: dx * step, dy: dy * step });
  panLoop = requestAnimationFrame(panTick);
}
document.addEventListener('keyup', (e) => panHeld.delete(e.key.length === 1 ? e.key.toLowerCase() : e.key));
window.addEventListener('blur', () => panHeld.clear());

// The inspector's boxes: a value typed in goes to the engine
$('scene-right').addEventListener('change', (e) => {
  const el = e.target;
  // (an animation or behaviour picked: kept across the inspector's redraws)
  if (el.tagName === 'SELECT' && el.id) {
    SC.picked[el.id] = el.value;
    el.blur();
    return;
  }
  if (!el.dataset || !el.dataset.set) return;
  const value = el.dataset.str ? el.value : parseFloat(el.value);
  if (!el.dataset.str && !isFinite(value)) return;
  lab.engineSend({ cmd: 'set', field: el.dataset.set, value });
  if (!el.dataset.str && pinOf(el.dataset.set)) lab.engineSend({ cmd: 'pin', field: el.dataset.set, value, on: true });
  el.blur();
});
$('scene-right').addEventListener('keydown', (e) => {
  if (e.key === 'Enter' && e.target.dataset && e.target.dataset.set) e.target.dispatchEvent(new Event('change', { bubbles: true }));
  if (e.key === 'Escape' && e.target.blur) e.target.blur();
});

// ---------------------------------------------------------------- maps
async function chooseMap() {
  if (!SC.maps) {
    lab.engineSend({ cmd: 'maps' });
    for (let i = 0; i < 20 && !SC.maps; i++) await new Promise((r) => setTimeout(r, 100));
  }
  const s = sc() || {};
  const m = $('modal');
  let pick = s.map;
  let flat = false;
  const draw = () => {
    m.innerHTML = `<div class="dialog" style="width:520px"><div class="h">Load a map into the lab zoo</div><div class="muted" style="line-height:1.5">The game’s freeform maps, as our engine loads them. Whatever’s in the lab zoo now goes (a running mod stops).</div><div class="maplist">${(SC.maps || [])
      .map(
        (x) =>
          `<div class="mapitem${x.stem === pick ? ' on' : ''}" data-map="${esc(x.stem)}"><span>${esc(x.name || x.stem)}</span><span class="muted">${esc(x.size || '')}</span></div>`,
      )
      .join('')}</div><label class="check clickable" data-flat="1"><span class="box2">${flat ? '✓' : ''}</span><span>Make it flat: the zoo all grass, nothing in it (the entrance kept)</span></label><div class="btns"><div class="btn" data-x="no">Cancel</div><div class="btn primary" data-x="ok">Load</div></div></div>`;
    m.classList.remove('hidden');
  };
  draw();
  return new Promise((resolve) => {
    const close = (ok) => {
      m.classList.add('hidden');
      m.innerHTML = '';
      m.onclick = null;
      if (ok && pick) {
        SC.outline = null;
        SC.countsKey = '';
        renderTree();
        lab.engineSend({ cmd: 'load', map: pick, flat });
        toast('Loading the map…', 1500);
      }
      resolve();
    };
    m.onclick = (e) => {
      const it = e.target.closest('[data-map]');
      if (it) {
        pick = it.dataset.map;
        draw();
      } else if (e.target.closest('[data-flat]')) {
        flat = !flat;
        draw();
      } else if (e.target.dataset.x === 'ok') close(true);
      else if (e.target.dataset.x === 'no' || e.target === m) close(false);
    };
    m.ondblclick = (e) => {
      const it = e.target.closest('[data-map]');
      if (it) {
        pick = it.dataset.map;
        close(true);
      }
    };
  });
}

// --------------------------------------------------------------- actions
Object.assign(ACTIONS, {
  scLeft: (t) => {
    SC.left = t;
    store.set('scLeft', t);
    renderSceneLeft(true);
  },
  scRight: (t) => {
    SC.right = t;
    renderRight();
  },
  scOpen: (key) => {
    SC.open[key] = !SC.open[key];
    store.set('scOpen', SC.open);
    renderTree();
  },
  scSelect: (arg) => {
    const i = arg.indexOf(':');
    const kind = arg.slice(0, i),
      rest = arg.slice(i + 1);
    if (kind === 'kind') {
      // a kind of scenery: each click the next one of them
      const k = SC.outline && SC.outline.scenery.find((x) => x.file === rest);
      if (!k) return;
      const n = (SC.cycle[rest] || 0) % k.i.length;
      SC.cycle[rest] = n + 1;
      lab.engineSend({ cmd: 'select', kind: 'object', id: k.i[n], focus: true });
      return;
    }
    lab.engineSend({ cmd: 'select', kind, id: +rest, focus: true });
  },
  scDeselect: () => lab.engineSend({ cmd: 'select', kind: '' }),
  scFocus: () => lab.engineSend({ cmd: 'focusSel' }),
  scRemove: () => {
    const s = sc();
    if (s && s.sel && ['animal', 'staff', 'object'].includes(s.sel.kind)) lab.engineSend({ cmd: 'remove' });
  },
  scTurnSel: () => {
    const s = sc();
    if (s && s.sel && s.sel.kind === 'object') lab.engineSend({ cmd: 'set', field: 'facing', value: (s.sel.facing + 2) % 8 });
  },
  scUndo: () => lab.engineSend({ cmd: 'undo' }),
  scTool: (kind) => {
    if (SC.palette.open && kind !== 'select') {
      SC.palette.open = false;
      renderStageUi();
    }
    if (kind === 'select' || kind === 'bulldoze') sendTool({ kind });
    else if (kind === 'route') sendTool({ kind, item: SC.routeKind || 'guest' });
  },
  scRouteKind: (k) => {
    SC.routeKind = k;
    sendTool({ kind: 'route', item: k });
  },
  scPalette: () => {
    SC.palette.open = !SC.palette.open;
    renderStageUi();
    if (SC.palette.open && $('pal-q')) $('pal-q').focus();
  },
  scCat: (c) => {
    SC.palette.cat = c;
    store.set('scCat', c);
    renderPalette();
  },
  scPick: (key) => pickItem(key),
  scLayers: () => {
    SC.viewLayered = !SC.viewLayered;
    showAnim();
  },
  scCheck: (which) => {
    const st = S.engine.state || {};
    const species = which || (st.what || 'zebra').split('|')[0];
    speciesCheck(species).then(showCheck);
  },
  scArtFrame: (d) => {
    if (+d) {
      SC.artHeld = true;
      lab.engineSend({ cmd: 'artFrame', step: +d });
    } else {
      SC.artHeld = !SC.artHeld;
      lab.engineSend(SC.artHeld ? { cmd: 'artFrame', value: 0 } : { cmd: 'artFrame', value: -1 });
    }
    renderStageUi();
  },
  scArtZoom: (d) => {
    SC.artZoom = Math.max(0.5, Math.min(6, (SC.artZoom || 1.5) * (+d > 0 ? 1.25 : 0.8)));
    lab.engineSend({ cmd: 'artZoom', value: SC.artZoom });
  },
  scAct: (arg) => {
    const i = arg.indexOf('|');
    lab.engineSend({ cmd: 'act', action: arg.slice(0, i), arg: arg.slice(i + 1) });
  },
  scActPick: (arg) => {
    const [action, id] = arg.split('|');
    const el = $(id);
    if (el) lab.engineSend({ cmd: 'act', action, arg: el.value });
  },
  scPin: (field) => {
    const s = sc();
    if (!s || !s.sel) return;
    const was = pinOf(field);
    lab.engineSend({ cmd: 'pin', field, value: s.sel[field], on: !was });
    toast(was ? `${esc(field)} let go` : `${esc(field)} pinned at ${fmt(s.sel[field])}: held there every update`, 2000);
  },
  fpFind: () => {
    S.fview.q = 'cFootprintX';
    S.fview.limit = 300;
    setTab('functions');
    if (!S.ghidra.ok) toast('Searching zoo.exe’s text needs Ghidra: in Ghidra, Tools › zt1-engine › Start Lab bridge.', 5000);
  },
  fpFile: (file) => {
    gameFilesGo(file, file);
    lab.engineSend({ cmd: 'file', name: file });
  },
  tryFn: (addr) => (ACTIONS.tryRun ? ACTIONS.tryRun(addr) : toast('Running zoo.exe’s own code here comes next.', 2500)),
  toggleConsole: () => {
    S.consoleOpen = !S.consoleOpen;
    store.set('dkOpen', S.consoleOpen);
    renderConsole();
    requestAnimationFrame(sizeCanvas);
  },
  dkTab: (t) => {
    if (S.consoleOpen && DK.tab === t) return ACTIONS.toggleConsole();
    DK.tab = t;
    store.set('dkTab', t);
    if (!S.consoleOpen) {
      S.consoleOpen = true;
      store.set('dkOpen', true);
      requestAnimationFrame(sizeCanvas);
    }
    renderConsole();
    if (t === 'console' && $('con-in')) $('con-in').focus();
  },
  dkTrace: (m) => lab.engineSend({ cmd: 'trace', mode: m }),
  dkStepEach: () => lab.engineSend({ cmd: 'stepEach', on: !((sc() || {}).trace || {}).stepEach }),
  dkGo: (how) => {
    if (how === 'stepNext' && !DK.paused) toast('Stops at the next step of what’s traced.', 2000);
    lab.engineSend({ cmd: how });
  },
  dkPick: (n) => {
    DK.cur = +n;
    DK.frame = null;
    const last = DK.steps[DK.steps.length - 1];
    DK.follow = !!last && last.n === +n;
    renderSteps(true);
  },
  dkFrame: (i) => {
    const st = dkStep(DK.cur);
    const f = st && st.stack && st.stack[+i];
    if (!f) return;
    DK.frame = DK.frame && DK.frame.i === +i ? null : { ...f, i: +i };
    renderStepSource();
  },
  dkBp: (key) => {
    const on = !DK.bps.has(key);
    if (on) DK.bps.add(key);
    else DK.bps.delete(key);
    store.set('dkBps', [...DK.bps]);
    const i = key.lastIndexOf(':');
    lab.engineSend({ cmd: 'breakpoint', file: key.slice(0, i), line: +key.slice(i + 1), on });
    toast(on ? `Stops at ${esc(key)} (whoever’s code it is)` : `No longer stops at ${esc(key)}`, 2000);
    renderStepSource();
  },
  scOpt: (o) => {
    const t = { ...(sc() || {}).tool };
    if (!t.kind) return;
    if (o === 'male' || o === 'female') SC.opts.female = t.female = o === 'female';
    else if (o === 'turn') t.facing = (t.facing + 1) % 4;
    else if (o === 'smaller' || o === 'bigger') SC.opts.size = t.size = Math.max(1, Math.min(5, t.size + (o === 'bigger' ? 1 : -1)));
    else if (o === 'higher' || o === 'lower') return lab.engineSend({ cmd: 'walkway', by: o === 'higher' ? 1 : -1 });
    else if (o === 'lineMode' || o === 'lineModeBack') return lab.engineSend({ cmd: 'lineMode', step: o === 'lineMode' ? 1 : -1 });
    sendTool(t);
  },
  scOverlay: (name) => {
    const s = sc();
    if (s) lab.engineSend({ cmd: 'overlay', name, on: !s.overlays[name] });
  },
  scZoom: (d) => lab.engineSend({ cmd: 'zoom', dir: +d }),
  scSpeed: (x) => lab.engineSend({ cmd: 'speed', value: +x }),
  scAdvance: (secs) => lab.engineSend({ cmd: 'advance', seconds: +secs }),
  scMaps: () => chooseMap(),
  view: (m) => lab.engineSend({ cmd: 'view', mode: m, what: m === 'animal' ? (currentAnimal() || { species: (S.engine.state || {}).what || 'zebra' }).species : '' }),
  pickSpecies: async () => {
    const animals = (SC.catalog || []).filter((i) => toolKindOf(i) === 'animal');
    if (!animals.length) {
      const a = await ask('Show a species', [{ key: 'sp', label: 'Species (its file name: zebra, lion, elephant…)', value: (S.engine.state || {}).what || 'zebra' }], 'Show');
      if (a && a.sp) lab.engineSend({ cmd: 'view', mode: 'animal', what: a.sp.toLowerCase() });
      return;
    }
    // (the palette's animals, picked from)
    SC.palette.open = true;
    SC.palette.cat = 'animals';
    SC.pickingSpecies = true;
    renderStageUi();
    toast('Pick a species in the palette to see it.', 2500);
  },
  togglePlay: () => {
    const st = S.engine.state;
    if (!st) return;
    if (st.loaded && st.live) lab.engineSend({ cmd: 'auto', on: !st.auto });
    else lab.engineSend({ cmd: 'play', on: !st.playing });
  },
  pickAnimal: (id) => ACTIONS.scSelect('animal:' + id),
});

// (the Animal view's species, picked in the palette)
const pickItemPlain = pickItem;
pickItem = function (key) {
  if (SC.pickingSpecies) {
    SC.pickingSpecies = false;
    const it = itemOf(key);
    if (it && toolKindOf(it) === 'animal') {
      lab.engineSend({ cmd: 'view', mode: 'animal', what: it.file.replace(/^animals\//, '').replace(/\.ai$/, '') });
      SC.palette.open = false;
      renderStageUi();
      return;
    }
  }
  pickItemPlain(key);
};

// ------------------------------------------------- the engine's answers
// The art viewer's animation, as picked (and its layers, if on)
function showAnim() {
  const st = S.engine.state || {};
  const [species] = (st.what || 'zebra').split('|');
  const el = $('st-anim');
  const anim = el ? el.value : '';
  if (!anim) lab.engineSend({ cmd: 'view', mode: 'animal', what: species });
  else lab.engineSend({ cmd: 'view', mode: 'anim', what: `${species}|${anim}|${SC.viewLayered ? 1 : 0}` });
}
document.addEventListener('change', (e) => {
  if (e.target && e.target.id === 'st-anim') {
    showAnim();
    e.target.blur();
  }
});

// ------------------------------------------------ Species check
// A species in bulk, from the engine: its art, its layers, its behaviour
// sets against what the engine does (speciesCheck in Lab.cpp)
function speciesCheck(species) {
  return new Promise((resolve) => {
    SC.checkWait = (m) => {
      if (m.want !== species) return;
      SC.checkWait = null;
      resolve(m);
    };
    SC.checkWant = species;
    lab.engineSend({ cmd: 'speciesCheck', species });
    // (every animation of every species is loaded: minutes for them all. A
    // late answer is still shown, by SCENE_MSG.speciesCheck)
    toast(`Checking ${esc(species)}… (the engine loads every animation: ${species.startsWith('*') ? 'a few minutes' : 'a few seconds'})`, 4000);
    setTimeout(
      () => SC.checkWait && ((SC.checkWait = null), resolve({ want: species, species: [], error: 'Still checking: the report shows here when the engine is done.' })),
      species.startsWith('*') ? 600000 : 60000,
    );
  });
}
// (one species' findings, in short)
function checkSummary(sp) {
  const n = (o) => Object.values(o || {}).reduce((a, b) => a + b, 0);
  return {
    species: sp.species,
    underwater: sp.underwater,
    anims: sp.anims,
    sets: sp.sets,
    missingArt: sp.missingArt.length,
    layered: sp.layered,
    badLayers: sp.badLayers,
    stepsNotDone: sp.stepsNotDone,
    movesNotKnown: sp.movesNotKnown,
    namedNotThere: sp.namedNotThere.length,
    ambientNotThere: sp.ambientNotThere,
    problems: sp.missingArt.length + sp.badLayers + n(sp.stepsNotDone) + n(sp.movesNotKnown) + sp.namedNotThere.length + sp.ambientNotThere.length,
  };
}
function showCheck(m) {
  S.speciesCheck = m;
  const md = $('modal');
  const kv = (o) =>
    Object.entries(o || {})
      .map(([k, v]) => `<span class="mono">${esc(k || '(none)')}</span> ×${v}`)
      .join(', ') || '<span class="green">none</span>';
  const rows = (m.species || [])
    .map((sp) => {
      const s = checkSummary(sp);
      const bad = sp.layers.filter((l) => l.bad);
      return `<div class="section"><div class="label">${esc(sp.species)}${sp.underwater ? ` · tank, depth ${sp.depth}` : ''} · ${sp.anims} animations · ${sp.sets} behaviour sets · ${
        s.problems ? `<span class="red">${s.problems} to look at</span>` : '<span class="green">all there</span>'
      }</div>
      <div class="kv"><span>Art missing</span><span>${
        sp.missingArt.length ? sp.missingArt.map((x) => `<span class="mono red">${esc(x.sub)}:${esc(x.anim)}</span>`).join(', ') : '<span class="green">none</span>'
      }</span></div>
      <div class="kv"><span>Layered (under / shadow)</span><span>${sp.layered}${
        bad.length ? ` · <span class="red">wrong: ${bad.map((l) => `${esc(l.anim)} (${l.frames} frames; under ${l.under ?? '-'}, shadow ${l.shadow ?? '-'})`).join(', ')}</span>` : ''
      }</span></div>
      <div class="kv"><span>Steps the engine skips</span><span>${kv(sp.stepsNotDone)}</span></div>
      <div class="kv"><span>Moves to places it doesn’t know</span><span>${kv(sp.movesNotKnown)}</span></div>
      <div class="kv"><span>Named, not there</span><span>${
        sp.namedNotThere.length
          ? sp.namedNotThere.map((x) => `<span class="mono">${esc(x.set)} › ${esc(x.anim || x.missingSet)}</span>`).join(', ')
          : '<span class="green">none</span>'
      }</span></div>
      ${sp.ambientNotThere.length ? `<div class="kv"><span>Ambient sets not there</span><span class="red">${sp.ambientNotThere.map(esc).join(', ')}</span></div>` : ''}</div>`;
    })
    .join('');
  md.innerHTML = `<div class="dialog" style="width:760px;max-height:80vh;overflow:auto"><div class="h">Species check: ${esc(m.want)}</div>${
    m.error ? `<div class="muted">${esc(m.error)}</div>` : rows || '<div class="muted">No species by that name.</div>'
  }<div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
  md.classList.remove('hidden');
  md.onclick = (e) => {
    if (e.target.dataset.x === 'no' || e.target === md) {
      md.classList.add('hidden');
      md.innerHTML = '';
    }
  };
}

const SCENE_MSG = {
  outline: (m) => {
    SC.outline = m;
    if (S.tab === 'scene') renderTree();
  },
  maps: (m) => {
    SC.maps = m.list;
  },
  catalog: (m) => {
    SC.catalog = m.items;
    SC.terrains = m.terrains;
    SC.filterFile = m.filter;
    if (S.tab === 'scene') renderPalette();
  },
  icon: (m) => {
    SC.icons[m.path] = m.png ? 'data:image/png;base64,' + m.png : 'none';
    if (!m.png) return;
    for (const img of document.querySelectorAll('img[data-icon]')) if (img.dataset.icon === m.path) img.src = SC.icons[m.path];
    // (the grid's HTML is compared before it's set again: the new src in it)
    const g = $('pal-grid');
    if (g) g._h = null;
  },
  note: (m) => toast(esc(m.text), 3500),
  ask: async (m) => {
    const r = await ask(m.text, [], 'Yes');
    lab.engineSend({ cmd: 'confirm', yes: !!r });
  },
  console: (m) => {
    SC.conOut.push('› ' + m.line);
    if (m.text) for (const l of m.text.split('\n')) SC.conOut.push(l);
    if (SC.conOut.length > 2000) SC.conOut.splice(0, 500);
    // (also in the log, in order with the rest)
    S.engine.log.push('› ' + m.line);
    if (m.text) for (const l of m.text.split('\n')) S.engine.log.push(l);
    if (!S.consoleOpen || DK.tab !== 'console') {
      S.consoleOpen = true;
      DK.tab = 'console';
      requestAnimationFrame(sizeCanvas);
    }
    renderConsole();
  },
  step: (m) => onStepMsg(m),
  resumed: () => {
    DK.paused = null;
    DK.frame = null;
    if (S.tab === 'scene') {
      renderConsole();
      renderSteps(true);
      renderStageUi();
    }
  },
  // (a species check's progress: which species it's on)
  speciesCheckProgress: (m) => {
    SC.checkProgress = m;
    if (m.starting) toast(`Checking ${esc(m.species)} (${m.done + 1} of ${m.of})…`, 1500);
  },
  speciesCheck: (m) => {
    SC.checkProgress = null;
    if (SC.checkWait) SC.checkWait(m);
    else if (m.want === SC.checkWant) showCheck(m); // (after the wait gave up)
  },
  viewAnims: (m) => {
    SC.viewAnims = m;
    if (S.tab === 'scene') renderStageUi();
  },
  stepsDropped: (m) => {
    DK.dropped += m.n;
  },
  // (a time skip finished: whoever waits on it)
  skipDone: (m) => {
    if (SC.skipWait) SC.skipWait(m), (SC.skipWait = null);
  },
};
