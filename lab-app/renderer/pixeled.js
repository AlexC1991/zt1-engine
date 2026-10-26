// ZT1 Lab: the animal maker - Mods > a mod > Animals (re/lab/animalmaker.py
// reads and writes it, in the mod's content.ztd and .art folder):
//  - a new animal from scratch: a name, and an animal it behaves like (its
//    needs, biome and behaviour, all changeable) - no art of its own yet
//  - its animations, one row each (walk, run, eat, ...): LINKED to an
//    animation that exists - the one it behaves like, at first, so it works
//    in the game straight away - or DRAWN; any row can be linked to any
//    animation, the game's or one drawn here, at any time
//  - the editor, as Piskel's: transparent layers, frames, onion skin, a
//    guide (the linked animation, faint, under the drawing), one set of
//    frames for each way it faces (N, NE, E, SE, S: the game mirrors the
//    other three), and pencil, eraser, fill, line, box, oval, picker and
//    move. A layer marked shadow is the ground shadow. Colours are free:
//    saving makes the animal's 256-colour palette from everything drawn
// (loaded after modkit.js)

const AN = {
  folder: null, // the mod
  list: null, // its animals [{type, name, species}]
  type: null, // the one shown
  info: null, // its rows: {name, subs: {m: [{slot, value, path, status, dirs, frames, ms}]}}
  sub: 'm',
  busy: '',
  error: '',
  ed: null, // the editor, while drawing
};
const AN_DIRS = ['N', 'NE', 'E', 'SE', 'S'];
function anRequest(req) {
  return lab.animalDo(AN.folder, AN.item && ['load', 'save', 'slots', 'link'].includes(req.op) ? { ...req, itemOp: true, item: AN.item.key } : req);
}
const AN_SUBS = { m: 'Adult', f: 'Female', y: 'Young' };
const AN_TOOLS = [
  ['pencil', '✎', 'Pencil (B)'],
  ['eraser', '⌫', 'Eraser (E)'],
  ['fill', '▧', 'Fill (G)'],
  ['line', '╱', 'Line (L)'],
  ['rect', '▭', 'Box (R)'],
  ['oval', '◯', 'Oval (O)'],
  ['picker', '⊙', 'Pick a colour (I)'],
  ['move', '✥', 'Move the layer (M)'],
  ['anchor', '⊕', 'The ground point: where the animal stands (A)'],
  ['band', '⇕', 'A height band, to make taller or shorter in every direction - legs, a neck (H)'],
];

// ---------------------------------------------------------------- the page
async function animalsPage(m) {
  if (AN.folder !== m.folder) {
    if (AN.ed && AN.ed.dirty && !confirm('Leave this drawing without saving it?')) return '';
    Object.assign(AN, { folder: m.folder, list: null, type: null, info: null, ed: null, error: '' });
  }
  if (!AN.list) await anLoadList();
  if (AN.type && !AN.info) await anLoadInfo();
  return `<div class="an" id="an">${anHtml()}</div>`;
}
async function anLoadList() {
  const r = await lab.animalDo(AN.folder, { op: 'animals' });
  AN.list = r.ok ? r.animals : [];
  AN.error = r.ok ? '' : r.error || '';
  if (!AN.type || !AN.list.some((a) => a.type === AN.type)) AN.type = AN.list.length ? AN.list[0].type : null;
}
async function anLoadInfo() {
  if (!AN.type) return;
  const r = await anRequest({ op: 'slots', type: AN.type });
  AN.info = r.ok ? r : null;
  AN.error = r.ok ? '' : r.error || '';
  if (AN.info && !AN.info.subs[AN.sub]) AN.sub = AN.item ? Object.keys(AN.info.subs)[0] : 'm';
}
function anRender() {
  const el = $('an');
  if (!el) return;
  el.innerHTML = anHtml();
  anMounted();
}
function anMounted() {
  if (AN.ed) edMounted();
}

// (the studio's tree lists the animals: studio.js)
function anHtml() {
  if (AN.error && !AN.list) return `<div class="red" style="padding:20px">${esc(AN.error)}</div>`;
  return `<div class="anmain">${AN.ed ? edHtml() : anRowsHtml()}</div>`;
}

function anRowsHtml() {
  if (AN.busy) return `<div class="muted" style="padding:20px">${esc(AN.busy)}</div>`;
  const info = AN.info;
  if (!info) return `<div class="muted" style="padding:20px">${esc(AN.error || 'Reading it…')}</div>`;
  const subs = Object.keys(info.subs);
  const subName = s => ['fence', 'tankwall'].includes(AN.item?.kind) ? ({ f: 'Fence', g: 'Gate' }[s] || s) : s === 'base' ? 'Object' : AN_SUBS[s] || s;
  const tabs = subs.map((s) => `<div class="${s === AN.sub ? 'on' : ''}" data-act="anSub" data-arg="${s}">${subName(s)}</div>`).join('');
  const rows = info.subs[AN.sub] || [];
  const drawn = rows.filter((r) => r.status === 'drawn').length;
  const badge = (s) => `<span class="anbadge ${s}">${{ drawn: 'Drawn', linked: 'Linked', missing: 'Missing' }[s]}</span>`;
  const body = rows
    .map(
      (r) => `<tr><td class="mono">${esc(r.slot)}</td><td>${badge(r.status)}</td><td class="mono dim antrim" title="${esc(r.path)}">${esc(
        r.status === 'drawn' ? 'its own' + (r.path.split('/').pop() !== r.slot.toLowerCase() ? ': ' + r.path.split('/').pop() : '') : r.path || r.value,
      )}</td><td class="dim">${r.frames ? `${r.frames} frames · ${(r.dirs || []).length} ways` : ''}</td><td class="anbtns"><div class="btn small" data-act="anDraw" data-arg="${esc(r.slot)}">${
        r.status === 'drawn' ? 'Edit' : 'Draw'
      }</div><div class="btn small ghost" data-act="anDrawSeparate" data-arg="${esc(r.slot)}" title="Make an independent drawing for this slot; changes will not edit a shared animation">Own copy…</div><div class="btn small ghost" data-act="anLink" data-arg="${esc(r.slot)}">Link…</div></td></tr>`,
    )
    .join('');
  return `<div class="anhead"><div><div class="nm">${esc(info.name)}</div><div class="dim mono">${esc(AN.item ? AN.item.file : 'animals/' + info.type + '.uca')} · ${drawn} of ${rows.length} drawn</div></div>
      <div class="spacer"></div>
      <div class="btn small ghost" data-act="anSpecies" title="Its definition and properties">${AN.item ? 'Its definition file' : 'Its species file'}</div>
      <div class="btn small" data-act="mkTryMod" data-arg="${esc(AN.folder)}">▶ Try in the Test Zoo</div></div>
    <div class="seg mini" style="margin:10px 0">${tabs}${info.femaleOwn || AN.item ? '' : '<span class="dim" style="font-size:11.5px;padding:0 8px">The female uses the adult\'s animations.</span>'}</div>
    <div class="muted" style="font-size:12px;margin-bottom:8px">Each animation it plays. <b>Linked</b>: it uses one that exists (the game's, or one drawn here). <b>Drawn</b>: its own. Draw one, or link any of them to another, at any time.</div>
    <table class="antable"><thead><tr><th>Animation</th><th></th><th>Uses</th><th></th><th></th></tr></thead><tbody>${body}</tbody></table>
    <div style="display:flex;gap:8px;margin-top:10px">${AN.item ? '' : '<div class="btn small ghost" data-act="anNewAnim">+ A new animation</div>'}<div class="btn small ghost" data-act="anLinkPoses" title="For a prototype: reuse a drawn animation for selected poses">Link other poses…</div></div>`;
}

// ---------------------------------------------------------------- actions
Object.assign(ACTIONS, {
  anPick: (t) => ACTIONS.stPick('animal:' + t),
  anSub: (s) => {
    AN.sub = s;
    anRender();
  },
  anNew: async () => {
    AN.item = null;
    AN_DIRS.splice(0, AN_DIRS.length, 'N', 'NE', 'E', 'SE', 'S');
    const animals = (SC.catalog || []).filter((i) => toolKindOf(i) === 'animal' && /\.ai$/i.test(i.file));
    if (!animals.length && S.engine.status === 'connected') {
      lab.engineSend({ cmd: 'catalog' });
      for (let i = 0; i < 30 && !SC.catalog; i++) await new Promise((r) => setTimeout(r, 100));
    }
    // (its kind first - a zoo animal, a Marine Mania one, a dinosaur - then
    // an animal of that kind to start from: its behaviour, home and keeper
    // come with it)
    const kindOf = (i) => ((i.members || []).includes('dinosaur') ? 'dinosaur' : (i.members || []).includes('aqua') ? 'aqua' : 'zoo');
    const k = await ask(
      'A new animal: what kind',
      [
        {
          key: 'kind',
          label: 'Its kind (how it lives in the zoo)',
          value: 'zoo',
          options: [
            { value: 'zoo', label: 'Zoo animal: a fenced exhibit, a keeper' },
            { value: 'aqua', label: 'Marine animal (Marine Mania): a tank, a marine specialist' },
            { value: 'dinosaur', label: 'Dinosaur (Dinosaur Digs): eggs, a paleontologist' },
          ],
        },
      ],
      'Next',
    );
    if (!k) return;
    const list = (SC.catalog || [])
      .filter((i) => toolKindOf(i) === 'animal' && /\.ai$/i.test(i.file) && kindOf(i) === k.kind)
      .sort((a, b) => (a.name || '').localeCompare(b.name || ''));
    const first = { zoo: 'animals/zebra.ai', aqua: 'animals/dolphin.ai', dinosaur: 'animals/trex.ai' }[k.kind];
    const a = await ask(
      'A new animal',
      [
        { key: 'name', label: 'Its name (what the game calls it)', hint: 'e.g. Glyptodon' },
        {
          key: 'like',
          label: 'It behaves like (its behaviour, needs, home and sounds to start from - change any of them)',
          value: list.some((i) => i.file === first) ? first : list[0] && list[0].file,
          options: list.length ? list.map((i) => ({ value: i.file, label: i.name || i.file })) : [{ value: 'animals/zebra.ai', label: 'Plains Zebra' }],
        },
      ],
      'Make it',
    );
    if (!a || !a.name) return;
    AN.busy = 'Making it…';
    const r = await lab.animalDo(AN.folder, { op: 'new', name: a.name, like: a.like });
    AN.busy = '';
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be made.'), 5000);
    toast(`<b>${esc(a.name)}</b> made: its animations are linked to the ${esc(r.like)}'s. Draw its own, one at a time.`, 5000);
    AN.type = r.type;
    AN.ed = null;
    AN.info = null;
    await anLoadList();
    anRender();
    await anLoadInfo();
    anRender();
  },
  anSpecies: () => {
    if (AN.info) openModEntry(AN.folder, 'content.ztd', AN.item ? AN.item.file : `animals/${AN.info.type}.uca`);
  },
  // (a row pointed at another animation: one drawn here, its own, or any of
  // the game's - by its path, which the list helps find)
  anLink: async (slot) => {
    if (!AN.info) return;
    const row = (AN.info.subs[AN.sub] || []).find((r) => r.slot === slot);
    const own = [];
    for (const rows of Object.values(AN.info.subs)) for (const r of rows) if (r.status === 'drawn' && !own.includes(r.path)) own.push(r.path);
    const a = await ask(
      `Link ${slot} to an animation`,
      [
        {
          key: 'target',
          label: 'The animation it plays: one drawn here, or any animation\'s path (animals/zebra/m/walk/walk - Write Code › Game files lists them, type:ani)',
          value: row ? row.path : '',
        },
        ...(own.length ? [{ key: 'own', label: 'Or one drawn for this animal', value: '', options: [{ value: '', label: '(the path above)' }, ...own.map((p) => ({ value: p, label: p }))] }] : []),
      ],
      'Link it',
    );
    if (!a) return;
    const target = (a.own || a.target || '').trim();
    if (!target) return;
    const r = await anRequest({ op: 'link', type: AN.info.type, sub: AN.sub, slot, target });
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be linked.'), 5000);
    toast(`${esc(slot)} now plays ${esc(target)}.`, 3000);
    ST.problems = null;
    ST.insp = null;
    await anLoadInfo();
    anRender();
  },
  anNewAnim: async () => {
    const a = await ask('A new animation', [{ key: 'name', label: 'Its name (lower-case letters, numbers and _: dig, roll_over)', hint: 'e.g. dig' }], 'Draw it');
    if (!a || !a.name) return;
    const slot = a.name.trim().toLowerCase();
    if (!/^[a-z0-9_]{1,24}$/.test(slot)) return toast('Lower-case letters, numbers and _ only.', 3000);
    toast('Its behaviour sets play it by this name: add it to one in its species file (fPlay(' + esc(slot) + ')).', 6000);
    await edOpen(slot, slot, null);
  },
  anLinkPoses: async () => {
    if (!AN.info) return;
    const rows = AN.info.subs[AN.sub] || [], own = [...new Set(rows.filter(r => r.status === 'drawn').map(r => r.path))];
    if (!own.length) return toast('Draw or import an animation and Save it first.', 4000);
    const a = await ask('Link other poses', [
      { key: 'target', label: 'Reuse this drawn animation (this does not create new movement)', value: own[0], options: own.map(p => ({ value: p, label: p })) },
      { key: 'slots', label: 'Pose names, separated by commas (crate and egg poses are kept)', value: rows.filter(r => !/^(box_|egg_)/.test(r.slot)).map(r => r.slot).join(',') },
    ], 'Link these poses');
    if (!a) return;
    const slots = [...new Set(a.slots.split(',').map(s => s.trim()).filter(Boolean))];
    if (!slots.length || slots.some(s => !rows.some(r => r.slot === s) || /^(box_|egg_)/.test(s))) return toast('Choose existing pose names; crate and egg poses are kept.', 5000);
    const folder = AN.folder, type = AN.info.type, sub = AN.sub;
    let failed = '';
    AN.busy = 'Linking poses…'; anRender();
    try {
      for (const slot of slots) {
        const r = await anRequest({ op: 'link', type, sub, slot, target: a.target });
        if (!r.ok) throw Error(r.error || 'A pose could not be linked.');
      }
    } catch (e) { failed = e.message || String(e); } finally { AN.busy = ''; }
    ST.problems = null; ST.insp = null; await anLoadInfo(); renderModKit(); toast(failed ? esc(failed) : `Linked ${slots.length} poses.`, failed ? 6000 : 4000);
  },
  anDraw: async (slot) => {
    if (!AN.info) return;
    const row = (AN.info.subs[AN.sub] || []).find((r) => r.slot === slot);
    // (its own drawn art's folder name, else the row's name)
    const anim = AN.item?.kind === 'ground' ? 'texture' : row && row.status === 'drawn' ? row.path.split('/').pop() : slot.toLowerCase().replace(/[^a-z0-9_]/g, '_').slice(0, 24);
    await edOpen(slot, anim, row && row.path ? row.path : null);
  },
  anDrawSeparate: async (slot) => {
    if (!AN.info) return;
    const row = (AN.info.subs[AN.sub] || []).find(r => r.slot === slot);
    if (!row) return;
    const anim = slot.toLowerCase().replace(/[^a-z0-9_]/g, '_').slice(0, 24);
    await edOpen(slot, anim, row.path || null);
  },
});

// ---------------------------------------------------------------- editor
// A frame's cells (one per layer) are ImageData, or null where nothing's drawn
function edBlank(w, h) {
  return new ImageData(w, h);
}
async function edDecode(b64, w, h) {
  if (!b64) return null;
  const img = new Image();
  img.src = 'data:image/png;base64,' + b64;
  await img.decode();
  const c = document.createElement('canvas');
  c.width = w;
  c.height = h;
  const g = c.getContext('2d');
  g.drawImage(img, 0, 0);
  return g.getImageData(0, 0, w, h);
}
function edEncode(cell) {
  if (!cell || !cell.data.some((v, i) => i % 4 === 3 && v)) return null;
  const c = document.createElement('canvas');
  c.width = cell.width;
  c.height = cell.height;
  c.getContext('2d').putImageData(cell, 0, 0);
  return c.toDataURL('image/png').split(',')[1];
}

// (pic: one of its pictures - {key, w, h} - one frame facing N, at the
// game's size for it, saved with a palette of its own)
async function edOpen(slot, anim, refPath, pic) {
  AN.busy = 'Opening it…';
  const r = await anRequest({ op: 'load', type: AN.info.type, sub: pic ? 'pics' : AN.sub, anim, reference: refPath });
  AN.busy = '';
  if (!r.ok) return toast(esc(r.error || 'It couldn’t be opened.'), 5000);
  let ref = r.reference;
  let p = r.project;
  let startCopy = false;
  let how = 'blank';
  if (p && p.stencil && (!ref || p.stencil !== ref.from)) {
    // (the stencil it was drawn with, back under it)
    const st = await edReadAnim(p.stencil);
    if (st) ref = st;
  }
  if (!p && pic) {
    // (a picture: its current one copied in to change, as a stencil, or none)
    const a = await ask(
      `Draw its ${slot.toLowerCase()}`,
      [
        {
          key: 'how',
          label: `Start from (${pic.w} x ${pic.h})`,
          value: ref ? 'remake' : 'blank',
          options: (ref
            ? [
                { value: 'remake', label: 'Remake: its current picture copied in, to change' },
                { value: 'stencil', label: 'Stencil: its current picture’s outline, to draw your own in' },
              ]
            : []
          ).concat([{ value: 'blank', label: 'Blank' }]),
        },
      ],
      'Start',
    );
    if (!a) return;
    how = a.how;
    if (how === 'blank') ref = null;
    p = { version: 1, w: pic.w, h: pic.h, ax: pic.w >> 1, ay: pic.h >> 1, ms: 100, layers: [{ name: 'Picture', visible: true }], dirs: {} };
  }
  if (!p) {
    // (new: blank with the linked animation as a guide, a copy of it, or a
    // stencil - any animal's animation, its shape in every direction and
    // frame, to reshape and recolour - as big as it, its ground point where
    // that one's is)
    // (the stencil browser, on what the slot plays now)
    const pick = await edStencilBrowser({ slot, start: true, linked: ref ? ref.from : '' });
    if (!pick) return;
    how = pick.how;
    if (how === 'blank') ref = null;
    else {
      if (!ref || pick.path !== ref.from) ref = await edReadAnim(pick.path);
      if (!ref) return;
      startCopy = how === 'remake';
    }
    const w = ref ? ref.w : AN.item?.kind === 'path' ? 80 : 96,
      h = ref ? ref.h : AN.item?.kind === 'path' ? 64 : 96;
    p = { version: 1, w, h, ax: ref ? ref.ax : w / 2, ay: ref ? ref.ay : AN.item?.kind === 'path' ? 32 : 80, ms: ref ? ref.ms : 100, layers: [{ name: 'Shadow', visible: true, shadow: true }, { name: 'Body', visible: true }], dirs: {} };
  }
  const ed = {
    slot, anim, sub: pic ? 'pics' : AN.sub, pic: pic || null, w: p.w, h: p.h, ax: p.ax, ay: p.ay, ms: p.ms || 100,
    layers: p.layers, dirs: {}, dir: pic || !AN_DIRS.includes('E') ? 'N' : 'E', fi: 0, li: Math.max(0, p.layers.length - 1),
    tool: 'pencil', color: store.get('edColor', '#c08040'), size: 1, zoom: 0, onion: true, mirror: false,
    // (the stencil: shown for a drawing started on one, its inside as faint
    // as the slider says, its outline, and paint kept inside it or not)
    showRef: p && p.version && r.project ? !!r.project.stencilShown : how === 'stencil',
    stFill: +store.get('edStFill', 20), stLine: store.get('edStLine', true), stInside: false,
    recent: store.get('edRecent', []), playing: false, undo: [], redo: [], dirty: false, ref: null,
  };
  for (const d of AN_DIRS) {
    const frames = (p.dirs || {})[d] || [];
    ed.dirs[d] = [];
    for (const f of frames) ed.dirs[d].push({ cells: await Promise.all(p.layers.map((_, i) => edDecode((f.cells || [])[i], p.w, p.h))) });
  }
  // The guide: the linked animation, placed by its ground point
  if (ref) {
    ed.ref = { from: ref.from, dx: p.ax - ref.ax, dy: p.ay - ref.ay, dirs: {} };
    for (const [d, frames] of Object.entries(ref.dirs)) ed.ref.dirs[d] = await Promise.all(frames.map((b) => edDecode(b, ref.w, ref.h)));
  }
  if (startCopy && ed.ref) {
    // (the copy: its art on Body, its shadow pixels - see-through in the
    // guide - on Shadow)
    for (const d of AN_DIRS) {
      ed.dirs[d] = (ed.ref.dirs[d] || []).map((src) => {
        const body = edBlank(p.w, p.h),
          sh = edBlank(p.w, p.h);
        for (let y = 0; y < src.height; y++)
          for (let x = 0; x < src.width; x++) {
            const X = x + ed.ref.dx,
              Y = y + ed.ref.dy;
            if (X < 0 || Y < 0 || X >= p.w || Y >= p.h) continue;
            const i = (y * src.width + x) * 4,
              o = (Y * p.w + X) * 4,
              a = src.data[i + 3];
            if (!a) continue;
            const t = a === 255 ? body : sh;
            t.data[o] = a === 255 ? src.data[i] : 0;
            t.data[o + 1] = a === 255 ? src.data[i + 1] : 0;
            t.data[o + 2] = a === 255 ? src.data[i + 2] : 0;
            t.data[o + 3] = 255;
          }
        return { cells: [sh, body] };
      });
    }
    ed.dirty = true;
  }
  // (on a stencil: as many blank frames as it has, each drawn over its own)
  if (how === 'stencil' && ed.ref)
    for (const d of AN_DIRS) {
      const n = (ed.ref.dirs[d] || []).length;
      while (ed.dirs[d].length < n) ed.dirs[d].push({ cells: ed.layers.map(() => null) });
    }
  const populated = AN_DIRS.filter(d => ed.dirs[d].some(f => f.cells.some(Boolean)));
  if (populated.length && !populated.includes(ed.dir)) ed.dir = populated[0];
  for (const d of AN_DIRS) if (!ed.dirs[d].length) ed.dirs[d].push({ cells: ed.layers.map(() => null) });
  AN.ed = ed;
  // (a picture remade: its current one copied onto its layer)
  if (pic && how === 'remake' && ed.ref) {
    edCopyGuide();
    ed.dirty = true;
  }
  // (the studio drawn again: the editor takes the Inspector's room)
  renderModKit();
}

function edFrames() {
  return AN.ed.dirs[AN.ed.dir];
}
function edFrame() {
  const fr = edFrames();
  AN.ed.fi = Math.max(0, Math.min(AN.ed.fi, fr.length - 1));
  return fr[AN.ed.fi];
}
function edCell(make) {
  const f = edFrame();
  const ed = AN.ed;
  if (!f.cells[ed.li] && make) f.cells[ed.li] = edBlank(ed.w, ed.h);
  return f.cells[ed.li];
}

// (an icon button: its icon, what it does in its tooltip)
const edIb = (act, icon, tip, arg, on) =>
  `<div class="edib${on ? ' on' : ''}" data-act="${act}"${arg !== undefined ? ` data-arg="${esc(String(arg))}"` : ''} title="${esc(tip)}">${ic(icon)}</div>`;

function edHtml() {
  const ed = AN.ed;
  const tools = AN_TOOLS.map(([k, ic, tip]) => `<div class="edtool${ed.tool === k ? ' on' : ''}" data-act="edTool" data-arg="${k}" title="${esc(tip)}">${ic}</div>`).join('');
  // (the direction ring: the five drawn ways, and the three the game mirrors
  // from them, this frame in each)
  const dirs = (AN.item ? AN_DIRS.map(d => [d, d]) : ED_RING).map(([d, from]) => {
    const n = ed.dirs[from].filter((f) => f.cells.some(Boolean)).length;
    return `<div class="edringd${d === ed.dir ? ' on' : ''}${from !== d ? ' mir' : ''}" data-act="edDir" data-arg="${from}" title="${
      from !== d ? `${d}: the game mirrors ${from} - drawing ${from} draws it` : n ? `${d}: ${n} frames drawn` : `${d}: nothing drawn yet`
    }"><canvas id="ed-ring-${d}" width="${ed.w}" height="${ed.h}"></canvas><span>${d}${n ? '' : ' ·'}</span></div>`;
  }).join('');
  const layers = ed.layers
    .map((l, i) => ({ l, i }))
    .reverse()
    .map(
      ({ l, i }) => `<div class="edlayer${i === ed.li ? ' sel' : ''}" data-act="edLayer" data-arg="${i}" title="${esc(l.name)}${l.shadow ? ': the ground shadow (the game draws it see-through)' : ''}"><span class="edeye${l.visible ? '' : ' off'}" data-act="edEye" data-arg="${i}" title="${
        l.visible ? 'Hide it' : 'Show it'
      }">${ic(l.visible ? 'eye' : 'eyeOff')}</span><span class="nm">${esc(l.name)}</span>${l.shadow ? `<span class="edlshadow">${ic('shadow')}</span>` : ''}</div>`,
    )
    .join('');
  const frames = edFrames()
    .map((_, i) => `<div class="edthumb${i === ed.fi ? ' sel' : ''}" data-act="edFrame" data-arg="${i}" title="Frame ${i + 1}"><canvas id="ed-th-${i}" width="${ed.w}" height="${ed.h}"></canvas><span>${i + 1}</span></div>`)
    .join('');
  const recent = ed.recent.map((c) => `<div class="edsw" style="background:${c}" data-act="edColor" data-arg="${c}" title="${c}"></div>`).join('');
  const guide = ed.ref ? ed.ref.from.split('/').filter(Boolean) : null;
  return `<div class="edtop${ed.pic ? ' pic' : ''}"><div class="nm">${esc(AN.info.name)} › <span class="mono">${esc(ed.slot)}</span> <span class="dim">(${ed.pic ? `${ed.pic.w} x ${ed.pic.h}` : AN.item ? esc(ed.sub === 'base' ? 'Object' : ed.sub) : AN_SUBS[ed.sub]})</span></div>
      <div class="spacer"></div>
      ${AN.busy ? `<span class="green">${esc(AN.busy)}</span>` : ''}
      <div class="btn small primary${ed.dirty ? '' : ' disabled'}" data-act="edSave" title="Made into the game's art and linked to ${esc(ed.slot)}">Save</div>
      <div class="btn small ghost" data-act="edClose">Close</div></div>
    ${ed.pic ? '' : `<div class="edring">${dirs}</div>`}
    <div class="edbody">
      <div class="edtools">${tools}
        <div class="edsep"></div>
        <input type="color" id="ed-color" value="${ed.color}" title="The pen colour">
        <div class="edsizes">${[1, 2, 3, 4].map((n) => `<div class="${ed.size === n ? 'on' : ''}" data-act="edSize" data-arg="${n}" title="Brush ${n} px ([ and ])">${n}</div>`).join('')}</div>
        <div class="edrecent">${recent}</div></div>
      <div class="edstagewrap">
        <div class="edstage" id="ed-stage"><canvas id="ed-canvas"></canvas></div>
        <div class="edzoombar">${edIb('edZoom', 'zoomOut', 'Zoom out (Ctrl + scroll down)', -1)}<span id="ed-zoom" title="How many screen pixels each pixel takes">${ed.zoom}×</span>${edIb(
          'edZoom',
          'zoomIn',
          'Zoom in (Ctrl + scroll up)',
          1,
        )}${edIb('edZoomFit', 'fit', 'Fit it in the view')}</div>
      </div>
      <div class="edside">
        <div class="edcard">
          <div class="edcardh"><span>Stencil</span><span class="spacer"></span>${
            ed.ref
              ? edIb('edGuide', ed.showRef ? 'eye' : 'eyeOff', ed.showRef ? 'Hide the stencil' : 'Show the stencil', undefined, ed.showRef) +
                edIb('edStLine', 'outline', 'Its outline, over the drawing', undefined, ed.stLine) +
                edIb('edStInside', 'inside', 'Paint only inside the outline', undefined, ed.stInside) +
                edIb('edStFillAll', 'bucket', 'Fill the stencil with the pen colour on this layer, in every frame and direction')
              : ''
          }${edIb('edStencil', 'stencil', 'Choose a stencil: the outline of any animal, person, building, tree, rock or fence')}</div>
          <div class="edstencil" data-act="edStencil" title="${ed.ref ? esc(ed.ref.from) + ' - click to choose another' : 'Choose a stencil'}">${
            ed.ref
              ? `<canvas id="ed-guide" width="${ed.w}" height="${ed.h}"></canvas><div><div class="nm">${esc(guide[1] || '')}</div><div class="dim mono">${esc(guide.slice(-1)[0] || '')}</div></div>`
              : `<span class="dim">None: draw freely, or choose one</span>`
          }</div>
          ${
            ed.ref
              ? `<label class="edslider" title="How much of the stencil shows inside its outline: none, just the outline, up to all of it"><span>Inside</span><input type="range" id="ed-stfill" min="0" max="100" step="5" value="${ed.stFill}"><b id="ed-stfillv">${ed.stFill}%</b></label>`
              : ''
          }
        </div>
        <div class="edcard">
          <div class="edcardh"><span>Layers</span><span class="spacer"></span>${edIb('edLayerAdd', 'plus', 'A new layer')}${edIb('edLayerMove', 'up', 'Move it up', 1)}${edIb('edLayerMove', 'down', 'Move it down', -1)}${edIb(
            'edLayerRename',
            'pencil',
            'Rename it',
          )}${edIb('edLayerShadow', 'shadow', 'Make it the ground shadow (or not)')}${edIb('edLayerDel', 'trash', 'Delete it')}</div>
          <div class="edlayers">${layers}</div>
        </div>
        <div class="edcard every">
          <div class="edcardh"><span>Every direction</span><span class="dim" title="These change every frame of every direction at once; Ctrl+Z undoes each">all frames</span></div>
          <div class="edgrid edpairs">
            <span class="edpair" title="Size">${edIb('edAllSize', 'shrink', 'Smaller: a tenth, about the ground point', -1)}${edIb('edAllSize', 'grow', 'Bigger: a tenth, about the ground point', 1)}</span>
            <span class="edpair" title="Body length">${edIb('edBody', 'bodyShort', 'Body shorter, along the way it faces', -1)}${edIb('edBody', 'bodyLong', 'Body longer, along the way it faces', 1)}</span>
            <span class="edpair" title="Height band">${edIb('edBand', 'bandShort', 'Height band shorter (pick the band with ⇕ first)', -1)}${edIb('edBand', 'bandTall', 'Height band taller: a longer neck or legs (pick the band with ⇕ first)', 1)}</span>
            <span class="edpair" title="Colour">${edIb('edRecolour', 'palette', 'Recolour a colour and its shades everywhere')}</span></div>
        </div>
        <div class="edcard">
          <div class="edcardh"><span>This direction</span><span class="dim">${ed.dir}</span></div>
          <div class="edgrid">${edIb('edCopyDir', 'copy', 'Copy its frames from another direction')}${edIb('edFlip', 'flip', 'Flip this frame left to right')}${edIb(
            'edImport',
            'upload',
            'Import external art: PNG picture, frame strip, directional sheet or OBJ model',
          )}${edIb('edExport', 'download', 'Export its frames as one PNG strip')}</div>
        </div>
        <div class="edcard">
          <div class="edcardh"><span>View</span></div>
          <div class="edgrid">${edIb('edView', 'onion', 'Onion skin: the frames either side, faint', 'onion', ed.onion)}${edIb('edView', 'mirror', 'Mirror the pen across the ground point', 'mirror', ed.mirror)}</div>
        </div>
      </div></div>
    <div class="edtl">
      <div class="edtlg">${edIb('edPlay', ed.playing ? 'pause' : 'play', ed.playing ? 'Pause (Space)' : 'Play (Space)')}${edIb('edStep', 'prev', 'The frame before (←)', -1)}${edIb('edStep', 'next', 'The next frame (→)', 1)}
        <label class="edms" title="How long each frame shows"><input type="number" id="ed-ms" value="${ed.ms}" min="20" max="2000" step="10"><span>ms</span></label></div>
      <div class="edstrip">${frames}<div class="edthumb add" data-act="edFrameAdd" title="A blank frame after this one">${ic('plus')}</div></div>
      <div class="edtlg">${edIb('edFrameDup', 'copy', 'Duplicate this frame')}${edIb('edFrameMove', 'moveLeft', 'Move it earlier', -1)}${edIb('edFrameMove', 'moveRight', 'Move it later', 1)}${edIb(
        'edFrameDel',
        'trash',
        'Delete this frame',
      )}</div>
    </div>`;
}

function edMounted() {
  const ed = AN.ed;
  const st = $('ed-stage');
  if (!st) return;
  if (!ed.zoom) ed.zoom = Math.max(2, Math.min(16, Math.floor(Math.min((st.clientWidth - 24) / ed.w, (st.clientHeight - 24) / ed.h))));
  const zl = $('ed-zoom');
  if (zl) zl.textContent = ed.zoom + '×';
  const c = $('ed-canvas');
  c.width = ed.w * ed.zoom;
  c.height = ed.h * ed.zoom;
  c.onpointerdown = edDown;
  c.onpointermove = edMove;
  c.onpointerup = c.onpointercancel = edUp;
  c.oncontextmenu = (e) => e.preventDefault();
  const col = $('ed-color');
  if (col) col.oninput = () => edSetColor(col.value, false);
  if (col) col.onchange = () => edSetColor(col.value, true);
  // (Ctrl + scroll: zoom in or out about the pixel under the mouse)
  st.onwheel = (e) => {
    if (!e.ctrlKey) return;
    e.preventDefault();
    edSetZoom(AN.ed.zoom + (e.deltaY < 0 ? 1 : -1) * Math.max(1, Math.round(AN.ed.zoom / 6)), e.clientX, e.clientY);
  };
  edGuideThumb();
  // (the stencil's inside: how much of it shows, 0 just its outline)
  const sf = $('ed-stfill');
  if (sf)
    sf.oninput = () => {
      ed.stFill = +sf.value;
      store.set('edStFill', ed.stFill);
      const v = $('ed-stfillv');
      if (v) v.textContent = ed.stFill + '%';
      edDraw();
    };
  const ms = $('ed-ms');
  if (ms)
    ms.onchange = () => {
      ed.ms = Math.max(20, Math.min(2000, +ms.value || 100));
      ed.dirty = true;
      if (ed.playing) edPlay(true);
    };
  edDraw();
  edThumbs();
  edRing();
}

function edSetColor(c, remember) {
  const ed = AN.ed;
  ed.color = c;
  store.set('edColor', c);
  if (remember) {
    ed.recent = [c, ...ed.recent.filter((x) => x !== c)].slice(0, 16);
    store.set('edRecent', ed.recent);
    const r = document.querySelector('.edrecent');
    if (r) r.innerHTML = ed.recent.map((x) => `<div class="edsw" style="background:${x}" data-act="edColor" data-arg="${x}" title="${x}"></div>`).join('');
  }
}

// A frame's layers as one picture (the shadow as see-through black)
function edComposite(frame, alpha = 1) {
  const ed = AN.ed;
  const out = edBlank(ed.w, ed.h);
  ed.layers.forEach((l, i) => {
    const c = frame.cells[i];
    if (!c || !l.visible) return;
    for (let p = 0; p < c.data.length; p += 4) {
      const a = c.data[p + 3];
      if (a < 128) continue;
      if (l.shadow) {
        if (!out.data[p + 3]) {
          out.data[p] = out.data[p + 1] = out.data[p + 2] = 0;
          out.data[p + 3] = 110 * alpha;
        }
      } else {
        out.data[p] = c.data[p];
        out.data[p + 1] = c.data[p + 1];
        out.data[p + 2] = c.data[p + 2];
        out.data[p + 3] = 255 * alpha;
      }
    }
  });
  return out;
}

function edDraw(preview) {
  const ed = AN.ed;
  const c = $('ed-canvas');
  if (!c) return;
  const g = c.getContext('2d');
  const z = ed.zoom;
  g.imageSmoothingEnabled = false;
  // (the checks: see-through)
  for (let y = 0; y < ed.h; y += 4)
    for (let x = 0; x < ed.w; x += 4) {
      g.fillStyle = ((x + y) / 4) % 2 ? '#2a3036' : '#22272c';
      g.fillRect(x * z, y * z, 4 * z, 4 * z);
    }
  const put = (img, a, dx = 0, dy = 0) => {
    if (!img) return;
    const t = document.createElement('canvas');
    t.width = img.width;
    t.height = img.height;
    t.getContext('2d').putImageData(img, 0, 0);
    g.globalAlpha = a;
    g.drawImage(t, dx * z, dy * z, img.width * z, img.height * z);
    g.globalAlpha = 1;
  };
  const fr = edFrames();
  const sref = edStencilImg();
  if (sref && ed.stFill > 0) put(sref, ed.stFill / 100, ed.ref.dx, ed.ref.dy);
  if (ed.onion && !ed.playing && fr.length > 1) {
    if (ed.fi > 0) put(edComposite(fr[ed.fi - 1]), 0.25);
    if (ed.fi < fr.length - 1) put(edComposite(fr[ed.fi + 1]), 0.15);
  }
  const comp = edComposite(edFrame());
  put(comp, 1);
  if (preview) put(preview, 1);
  // (the stencil's outline where nothing's painted yet: your own paint -
  // an outline of your own traced on it - covers it pixel by pixel)
  if (sref && ed.stLine) put(edOutlineUnder(sref, comp, preview), 1, ed.ref.dx, ed.ref.dy);
  // (the height band, picked to stretch)
  if (ed.band) {
    g.fillStyle = 'rgba(106,168,239,.16)';
    g.fillRect(0, Math.min(ed.band.y0, ed.band.y1) * z, ed.w * z, (Math.abs(ed.band.y1 - ed.band.y0) || 1) * z);
    g.strokeStyle = 'rgba(106,168,239,.7)';
    g.strokeRect(0.5, Math.min(ed.band.y0, ed.band.y1) * z + 0.5, ed.w * z - 1, (Math.abs(ed.band.y1 - ed.band.y0) || 1) * z - 1);
  }
  // (the ground point)
  g.strokeStyle = 'rgba(255,90,90,.9)';
  g.lineWidth = 1;
  g.beginPath();
  g.moveTo((ed.ax - 3) * z, (ed.ay + 0.5) * z);
  g.lineTo((ed.ax + 4) * z, (ed.ay + 0.5) * z);
  g.moveTo((ed.ax + 0.5) * z, (ed.ay - 3) * z);
  g.lineTo((ed.ax + 0.5) * z, (ed.ay + 4) * z);
  g.stroke();
  if (z >= 8) {
    g.strokeStyle = 'rgba(255,255,255,.05)';
    g.beginPath();
    for (let x = 0; x <= ed.w; x++) (g.moveTo(x * z + 0.5, 0), g.lineTo(x * z + 0.5, ed.h * z));
    for (let y = 0; y <= ed.h; y++) (g.moveTo(0, y * z + 0.5), g.lineTo(ed.w * z, y * z + 0.5));
    g.stroke();
  }
}
function edThumbs() {
  const ed = AN.ed;
  edFrames().forEach((f, i) => {
    const c = $('ed-th-' + i);
    if (c) c.getContext('2d').putImageData(edComposite(f), 0, 0);
  });
}
function edThumb() {
  const c = $('ed-th-' + AN.ed.fi);
  if (c) c.getContext('2d').putImageData(edComposite(edFrame()), 0, 0);
}

// ------------------------------------------------------------ drawing
const edHex = (h) => [parseInt(h.slice(1, 3), 16), parseInt(h.slice(3, 5), 16), parseInt(h.slice(5, 7), 16)];
let edXY = function (e) {
  const c = $('ed-canvas');
  const r = c.getBoundingClientRect();
  return [Math.floor(((e.clientX - r.left) * (c.width / r.width)) / AN.ed.zoom), Math.floor(((e.clientY - r.top) * (c.height / r.height)) / AN.ed.zoom)];
};
function edPlot(img, x, y, rgba) {
  const ed = AN.ed;
  const pts = [[x, y]];
  if (ed.mirror) pts.push([2 * ed.ax - x, y]);
  const s = ed.size,
    o = Math.floor((s - 1) / 2);
  for (const [px, py] of pts)
    for (let dy = 0; dy < s; dy++)
      for (let dx = 0; dx < s; dx++) {
        const X = px - o + dx,
          Y = py - o + dy;
        if (X < 0 || Y < 0 || X >= img.width || Y >= img.height) continue;
        if (rgba[3] && !edInside(X, Y)) continue;
        const i = (Y * img.width + X) * 4;
        img.data[i] = rgba[0];
        img.data[i + 1] = rgba[1];
        img.data[i + 2] = rgba[2];
        img.data[i + 3] = rgba[3];
      }
}
function edLinePts(x0, y0, x1, y1) {
  const pts = [];
  const dx = Math.abs(x1 - x0),
    dy = -Math.abs(y1 - y0),
    sx = x0 < x1 ? 1 : -1,
    sy = y0 < y1 ? 1 : -1;
  let err = dx + dy;
  for (;;) {
    pts.push([x0, y0]);
    if (x0 === x1 && y0 === y1) break;
    const e2 = 2 * err;
    if (e2 >= dy) (err += dy), (x0 += sx);
    if (e2 <= dx) (err += dx), (y0 += sy);
  }
  return pts;
}
function edShapePts(tool, x0, y0, x1, y1) {
  if (tool === 'line') return edLinePts(x0, y0, x1, y1);
  const l = Math.min(x0, x1),
    r = Math.max(x0, x1),
    t = Math.min(y0, y1),
    b = Math.max(y0, y1);
  const pts = [];
  if (tool === 'rect') {
    for (let x = l; x <= r; x++) pts.push([x, t], [x, b]);
    for (let y = t; y <= b; y++) pts.push([l, y], [r, y]);
    return pts;
  }
  // (an oval in the box)
  const cx = (l + r) / 2,
    cy = (t + b) / 2,
    rx = Math.max(0.5, (r - l) / 2),
    ry = Math.max(0.5, (b - t) / 2);
  const n = Math.max(16, Math.ceil(2 * Math.PI * Math.max(rx, ry) * 2));
  let last = null;
  for (let k = 0; k <= n; k++) {
    const a = (k / n) * 2 * Math.PI;
    const p = [Math.round(cx + rx * Math.cos(a)), Math.round(cy + ry * Math.sin(a))];
    if (last) pts.push(...edLinePts(last[0], last[1], p[0], p[1]));
    last = p;
  }
  return pts;
}
function edPush() {
  const ed = AN.ed;
  const cell = edCell(false);
  ed.undo.push({ dir: ed.dir, fi: ed.fi, li: ed.li, cell: cell ? new ImageData(new Uint8ClampedArray(cell.data), cell.width, cell.height) : null });
  if (ed.undo.length > 100) ed.undo.shift();
  ed.redo = [];
}
function edFill(img, x, y, rgba) {
  const w = img.width,
    h = img.height,
    d = img.data;
  const at = (X, Y) => (Y * w + X) * 4;
  const i0 = at(x, y);
  const src = [d[i0], d[i0 + 1], d[i0 + 2], d[i0 + 3]];
  const same = (i) => (src[3] < 128 ? d[i + 3] < 128 : d[i] === src[0] && d[i + 1] === src[1] && d[i + 2] === src[2] && d[i + 3] >= 128);
  if (src[3] >= 128 && src[0] === rgba[0] && src[1] === rgba[1] && src[2] === rgba[2] && rgba[3]) return;
  const st = [[x, y]];
  const seen = new Uint8Array(w * h);
  while (st.length) {
    const [X, Y] = st.pop();
    if (X < 0 || Y < 0 || X >= w || Y >= h || seen[Y * w + X]) continue;
    const i = at(X, Y);
    if (!same(i) || (rgba[3] && !edInside(X, Y))) continue;
    seen[Y * w + X] = 1;
    d[i] = rgba[0];
    d[i + 1] = rgba[1];
    d[i + 2] = rgba[2];
    d[i + 3] = rgba[3];
    st.push([X + 1, Y], [X - 1, Y], [X, Y + 1], [X, Y - 1]);
  }
}
function edDown(e) {
  const ed = AN.ed;
  if (ed.playing) edPlay(false);
  const [x, y] = edXY(e);
  e.target.setPointerCapture(e.pointerId);
  const rgba = e.button === 2 || ed.tool === 'eraser' ? [0, 0, 0, 0] : [...edHex(ed.color), 255];
  ed.stroke = { x0: x, y0: y, last: [x, y], rgba };
  if (ed.tool === 'anchor') {
    ed.ax = x;
    ed.ay = y;
    ed.dirty = true;
    ed.stroke = null;
    edDraw();
    return edTop();
  }
  if (ed.tool === 'band') {
    ed.band = { y0: y, y1: y };
    ed.stroke.band = true;
    return edDraw();
  }
  if (ed.tool === 'picker') {
    const p = edComposite(edFrame()).data;
    const i = (y * ed.w + x) * 4;
    if (p[i + 3]) edSetColor('#' + [p[i], p[i + 1], p[i + 2]].map((v) => v.toString(16).padStart(2, '0')).join(''), true);
    const col = $('ed-color');
    if (col) col.value = ed.color;
    ed.stroke = null;
    return;
  }
  if (!ed.layers[ed.li].visible) {
    ed.stroke = null;
    return toast('That layer is hidden: show it to draw on it.', 2500);
  }
  edPush();
  const cell = edCell(true);
  if (ed.tool === 'fill') {
    if (x >= 0 && y >= 0 && x < ed.w && y < ed.h) edFill(cell, x, y, rgba);
    ed.stroke = null;
    return edChanged();
  }
  if (ed.tool === 'move') {
    ed.stroke.orig = new ImageData(new Uint8ClampedArray(cell.data), cell.width, cell.height);
    return;
  }
  if (ed.tool === 'pencil' || ed.tool === 'eraser') {
    edPlot(cell, x, y, rgba);
    edChanged(true);
  }
}
function edMove(e) {
  const ed = AN.ed;
  const s = ed && ed.stroke;
  if (!s) return;
  const [x, y] = edXY(e);
  if (s.band) {
    ed.band.y1 = Math.max(0, Math.min(ed.h, y));
    return edDraw();
  }
  const cell = edCell(true);
  if (ed.tool === 'pencil' || ed.tool === 'eraser') {
    for (const [px, py] of edLinePts(s.last[0], s.last[1], x, y)) edPlot(cell, px, py, s.rgba);
    s.last = [x, y];
    return edChanged(true);
  }
  if (ed.tool === 'move') {
    const dx = x - s.x0,
      dy = y - s.y0,
      o = s.orig,
      w = ed.w;
    cell.data.fill(0);
    for (let Y = 0; Y < ed.h; Y++)
      for (let X = 0; X < w; X++) {
        const sx = X - dx,
          sy = Y - dy;
        if (sx < 0 || sy < 0 || sx >= w || sy >= ed.h) continue;
        const i = (Y * w + X) * 4,
          j = (sy * w + sx) * 4;
        cell.data[i] = o.data[j];
        cell.data[i + 1] = o.data[j + 1];
        cell.data[i + 2] = o.data[j + 2];
        cell.data[i + 3] = o.data[j + 3];
      }
    return edChanged(true);
  }
  if (['line', 'rect', 'oval'].includes(ed.tool)) {
    const pv = edBlank(ed.w, ed.h);
    for (const [px, py] of edShapePts(ed.tool, s.x0, s.y0, x, y)) edPlot(pv, px, py, s.rgba[3] ? s.rgba : [255, 80, 80, 200]);
    s.last = [x, y];
    edDraw(pv);
  }
}
function edUp() {
  const ed = AN.ed;
  const s = ed && ed.stroke;
  if (!s) return;
  ed.stroke = null;
  if (s.band) {
    const a = Math.min(ed.band.y0, ed.band.y1),
      b = Math.max(ed.band.y0, ed.band.y1) + 1;
    ed.band = b - a >= 2 ? { y0: a, y1: Math.min(b, ed.h) } : null;
    return renderModKit();
  }
  if (['line', 'rect', 'oval'].includes(ed.tool)) {
    const cell = edCell(true);
    for (const [px, py] of edShapePts(ed.tool, s.x0, s.y0, s.last[0], s.last[1])) edPlot(cell, px, py, s.rgba);
  }
  edChanged();
}
function edChanged(fast) {
  const ed = AN.ed;
  ed.dirty = true;
  edDraw();
  edThumb();
  edRingSoon();
  if (!fast) edTop();
}
// (the Save button's state, without drawing the whole page again)
function edTop() {
  const b = document.querySelector('[data-act="edSave"]');
  if (b) b.classList.toggle('disabled', !AN.ed.dirty);
  // (and the studio's top bar)
  const s = document.querySelector('.stsaved');
  if (s) {
    s.textContent = AN.ed.dirty ? 'Not saved' : 'All saved';
    s.classList.toggle('no', AN.ed.dirty);
  }
}

let edTimer = null;
function edPlay(on) {
  const ed = AN.ed;
  clearInterval(edTimer);
  ed.playing = on;
  if (on) {
    edTimer = setInterval(() => {
      if (!AN.ed || !$('ed-canvas')) return clearInterval(edTimer);
      AN.ed.fi = (AN.ed.fi + 1) % edFrames().length;
      edDraw();
      document.querySelectorAll('.edthumb').forEach((t, i) => t.classList.toggle('sel', i === AN.ed.fi));
    }, ed.ms);
  }
  const b = document.querySelector('.edib[data-act="edPlay"]');
  if (b) {
    b.innerHTML = ic(on ? 'pause' : 'play');
    b.title = on ? 'Pause (Space)' : 'Play (Space)';
  }
}

Object.assign(ACTIONS, {
  // (a stroke with the tool in hand through "x,y;x,y;..." - as the mouse
  // would draw it: for the Lab's MCP, an agent drawing)
  edStroke: (arg) => {
    const ed = AN.ed;
    if (!ed) return;
    const pts = String(arg || '').split(';').map((p) => p.split(',').map(Number)).filter((p) => p.length === 2 && p.every(Number.isFinite));
    if (!pts.length) return;
    const ev = (x, y) => ({ button: 0, pointerId: 1, target: { setPointerCapture() {} }, _xy: [x, y] });
    const saved = edXY;
    edXY = (e) => e._xy;
    try {
      edDown(ev(...pts[0]));
      for (const p of pts.slice(1)) edMove(ev(...p));
      edUp(ev(...pts[pts.length - 1]));
    } finally {
      edXY = saved;
    }
  },
  edTool: (t) => {
    AN.ed.tool = t;
    document.querySelectorAll('.edtool').forEach((el) => el.classList.toggle('on', el.dataset.arg === t));
  },
  edSize: (n) => {
    AN.ed.size = +n;
    document.querySelectorAll('.edsizes > div').forEach((el) => el.classList.toggle('on', +el.dataset.arg === +n));
  },
  edColor: (c) => {
    edSetColor(c, false);
    const col = $('ed-color');
    if (col) col.value = c;
  },
  edDir: (d) => {
    AN.ed.dir = d;
    AN.ed.fi = 0;
    anRender();
  },
  edFrame: (i) => {
    if (AN.ed.playing) edPlay(false);
    AN.ed.fi = +i;
    edDraw();
    edRing();
    document.querySelectorAll('.edthumb').forEach((t, k) => t.classList.toggle('sel', k === AN.ed.fi));
  },
  edPlay: () => edPlay(!AN.ed.playing),
  edZoom: (d) => edSetZoom(AN.ed.zoom + +d * Math.max(1, Math.round(AN.ed.zoom / 6))),
  edZoomFit: () => edSetZoom(edFitZoom()),
  edView: (k) => {
    AN.ed[k] = !AN.ed[k];
    const b = document.querySelector(`.edib[data-act="edView"][data-arg="${k}"]`);
    if (b) b.classList.toggle('on', AN.ed[k]);
    edDraw();
  },
  edGuide: () => {
    AN.ed.showRef = !AN.ed.showRef;
    anRender();
  },
  edStep: (d) => ACTIONS.edFrame((AN.ed.fi + +d + edFrames().length) % edFrames().length),
  edLayer: (i) => {
    AN.ed.li = +i;
    document.querySelectorAll('.edlayer').forEach((el) => el.classList.toggle('sel', +el.dataset.arg === +i));
  },
  edEye: (i) => {
    const l = AN.ed.layers[+i];
    l.visible = !l.visible;
    AN.ed.dirty = true;
    anRender();
  },
  edLayerAdd: async () => {
    const a = await ask('A new layer', [{ key: 'name', label: 'Its name', value: 'Layer ' + (AN.ed.layers.length + 1) }], 'Add it');
    if (!a) return;
    const ed = AN.ed;
    // (above the one picked)
    const at = ed.li + 1;
    ed.layers.splice(at, 0, { name: a.name || 'Layer', visible: true });
    for (const d of AN_DIRS) for (const f of ed.dirs[d]) f.cells.splice(at, 0, null);
    ed.li = at;
    ed.dirty = true;
    anRender();
  },
  edLayerDel: () => {
    const ed = AN.ed;
    if (ed.layers.length < 2) return toast('It needs a layer.', 2000);
    if (!confirm(`Delete the layer ${ed.layers[ed.li].name}, in every frame?`)) return;
    ed.layers.splice(ed.li, 1);
    for (const d of AN_DIRS) for (const f of ed.dirs[d]) f.cells.splice(ed.li, 1);
    ed.li = Math.max(0, ed.li - 1);
    ed.dirty = true;
    anRender();
  },
  edLayerMove: (d) => {
    const ed = AN.ed;
    const j = ed.li + +d;
    if (j < 0 || j >= ed.layers.length) return;
    [ed.layers[ed.li], ed.layers[j]] = [ed.layers[j], ed.layers[ed.li]];
    for (const dd of AN_DIRS) for (const f of ed.dirs[dd]) [f.cells[ed.li], f.cells[j]] = [f.cells[j], f.cells[ed.li]];
    ed.li = j;
    ed.dirty = true;
    anRender();
  },
  edLayerRename: async () => {
    const l = AN.ed.layers[AN.ed.li];
    const a = await ask('Rename the layer', [{ key: 'name', label: 'Its name', value: l.name }], 'Rename');
    if (!a || !a.name) return;
    l.name = a.name;
    AN.ed.dirty = true;
    anRender();
  },
  edLayerShadow: () => {
    const l = AN.ed.layers[AN.ed.li];
    l.shadow = !l.shadow;
    AN.ed.dirty = true;
    toast(l.shadow ? 'Its pixels are the ground shadow now (any colour: the game draws them as its shadow).' : 'An ordinary layer again.', 3000);
    anRender();
  },
  edFrameAdd: () => {
    const ed = AN.ed;
    edFrames().splice(ed.fi + 1, 0, { cells: ed.layers.map(() => null) });
    ed.fi++;
    ed.dirty = true;
    anRender();
  },
  edFrameDup: () => {
    const ed = AN.ed;
    const f = edFrame();
    edFrames().splice(ed.fi + 1, 0, { cells: f.cells.map((c) => (c ? new ImageData(new Uint8ClampedArray(c.data), c.width, c.height) : null)) });
    ed.fi++;
    ed.dirty = true;
    anRender();
  },
  edFrameDel: () => {
    const ed = AN.ed;
    const fr = edFrames();
    if (fr.length < 2) return toast('A direction keeps one frame at least.', 2000);
    fr.splice(ed.fi, 1);
    ed.fi = Math.max(0, ed.fi - 1);
    ed.dirty = true;
    anRender();
  },
  edFrameMove: (d) => {
    const ed = AN.ed;
    const fr = edFrames();
    const j = ed.fi + +d;
    if (j < 0 || j >= fr.length) return;
    [fr[ed.fi], fr[j]] = [fr[j], fr[ed.fi]];
    ed.fi = j;
    ed.dirty = true;
    anRender();
  },
  edFlip: () => {
    const ed = AN.ed;
    edPush();
    for (let i = 0; i < ed.layers.length; i++) {
      const c = edFrame().cells[i];
      if (!c) continue;
      const o = new Uint8ClampedArray(c.data);
      for (let y = 0; y < ed.h; y++)
        for (let x = 0; x < ed.w; x++) {
          // (across the ground point, so it stays where it stands)
          const X = 2 * ed.ax - x;
          const i2 = (y * ed.w + x) * 4;
          if (X < 0 || X >= ed.w) {
            c.data[i2 + 3] = 0;
            continue;
          }
          const j = (y * ed.w + X) * 4;
          c.data[i2] = o[j];
          c.data[i2 + 1] = o[j + 1];
          c.data[i2 + 2] = o[j + 2];
          c.data[i2 + 3] = o[j + 3];
        }
    }
    edChanged();
  },
  edCopyDir: async () => {
    const ed = AN.ed;
    const others = AN_DIRS.filter((d) => d !== ed.dir);
    const a = await ask(`Copy frames into ${ed.dir}`, [{ key: 'from', label: 'From the direction', value: others[0], options: others.map((d) => ({ value: d, label: `${d} (${ed.dirs[d].length} frames)` })) }], 'Copy them');
    if (!a) return;
    edPushAll();
    ed.dirs[ed.dir] = ed.dirs[a.from].map((f) => ({ cells: f.cells.map((c) => (c ? new ImageData(new Uint8ClampedArray(c.data), c.width, c.height) : null)) }));
    ed.fi = 0;
    ed.dirty = true;
    anRender();
  },
  edImport: () => {
    AssetImport.choose();
  },
  edExport: () => {
    const ed = AN.ed;
    const fr = edFrames();
    const c = document.createElement('canvas');
    c.width = ed.w * fr.length;
    c.height = ed.h;
    const g = c.getContext('2d');
    fr.forEach((f, i) => {
      const t = document.createElement('canvas');
      t.width = ed.w;
      t.height = ed.h;
      t.getContext('2d').putImageData(edComposite(f), 0, 0);
      g.drawImage(t, i * ed.w, 0);
    });
    const a = document.createElement('a');
    a.href = c.toDataURL('image/png');
    a.download = `${AN.info.type}-${ed.anim}-${ed.dir}.png`;
    a.click();
  },
  edSave: async () => {
    const ed = AN.ed;
    if (!ed.dirty) return;
    const empty = AN_DIRS.filter((d) => !ed.dirs[d].some((f) => f.cells.some(Boolean)));
    if (empty.length === AN_DIRS.length) return toast('Nothing drawn yet.', 2500);
    const project = { version: 1, w: ed.w, h: ed.h, ax: ed.ax, ay: ed.ay, ms: ed.ms, layers: ed.layers, dirs: {}, stencil: ed.ref ? ed.ref.from : '', stencilShown: !!ed.showRef };
    for (const d of AN_DIRS) {
      if (!ed.dirs[d].some((f) => f.cells.some(Boolean))) continue;
      project.dirs[d] = ed.dirs[d].map((f) => ({ cells: f.cells.map(edEncode) }));
    }
    // (a picture: made the game's art with its own palette, the species file
    // pointed at it)
    if (ed.pic) {
      AN.busy = 'Making it the game\'s art…';
      edTop();
      const r = await lab.animalDo(AN.folder, { op: 'savepic', type: AN.info.type, key: ed.anim, project });
      AN.busy = '';
      if (!r.ok) return toast(esc(r.error || 'It couldn’t be saved.'), 6000), anRender();
      ed.dirty = false;
      AN.pics = null;
      // (its species file points at it now: the Inspector's copy read again)
      ST.insp = null;
      toast(`Saved: the game shows your ${esc(ed.slot.toLowerCase())} now (${r.colours} colours${r.merged ? `, ${r.merged} merged: a picture holds 255` : ''}).`, 5000);
      return renderModKit();
    }
    AN.busy = 'Making it the game\'s art…';
    edTop();
    const r = await anRequest({ op: 'save', type: AN.info.type, sub: ed.sub, anim: ed.anim, slot: ed.slot, project });
    AN.busy = '';
    if (!r.ok) {
      anRender();
      return toast(esc(r.error || 'It couldn’t be saved.'), 6000);
    }
    ed.dirty = false;
    toast(
      `Saved: ${esc(ed.slot)} plays what you drew. ${r.colours} colours${r.merged ? ` (${r.merged} merged into others: the palette holds 254)` : ''}.${
        empty.length ? ` Nothing drawn facing ${empty.join(', ')} yet: it shows nothing there.` : ''
      }`,
      7000,
    );
    await anLoadInfo();
    ST.problems = null;
    ST.insp = null;
    renderModKit();
  },
  edClose: () => {
    if (!AN.ed) return;
    if (AN.ed.dirty && !confirm('Close it without saving?')) return;
    edPlay(false);
    AN.ed = null;
    renderModKit();
  },
});

// (keys, while the editor's open and nothing's being typed)
document.addEventListener('keydown', (e) => {
  if (!AN.ed || !$('ed-canvas') || /INPUT|TEXTAREA|SELECT/.test((e.target || {}).tagName || '')) return;
  const ed = AN.ed;
  const k = e.key.toLowerCase();
  if ((e.ctrlKey || e.metaKey) && (k === 'z' || k === 'y')) {
    e.preventDefault();
    const from = k === 'z' && !e.shiftKey ? ed.undo : ed.redo,
      to = from === ed.undo ? ed.redo : ed.undo;
    const u = from.pop();
    if (!u) return;
    if (u.all) {
      to.push(edSnapshot());
      edRestore(u);
      ed.dirty = true;
      return renderModKit();
    }
    const f = ed.dirs[u.dir][u.fi];
    if (!f) return;
    to.push({ dir: u.dir, fi: u.fi, li: u.li, cell: f.cells[u.li] ? new ImageData(new Uint8ClampedArray(f.cells[u.li].data), ed.w, ed.h) : null });
    f.cells[u.li] = u.cell;
    ed.dir = u.dir;
    ed.fi = u.fi;
    ed.dirty = true;
    return anRender();
  }
  if (e.ctrlKey || e.metaKey || e.altKey) return;
  const tool = { b: 'pencil', e: 'eraser', g: 'fill', l: 'line', r: 'rect', o: 'oval', i: 'picker', m: 'move', a: 'anchor', h: 'band' }[k];
  if (tool) return ACTIONS.edTool(tool);
  if (k === '[' || k === ']') return ACTIONS.edSize(Math.max(1, Math.min(4, ed.size + (k === ']' ? 1 : -1))));
  if (k === 'arrowleft' || k === 'arrowright') {
    e.preventDefault();
    return ACTIONS.edFrame((ed.fi + (k === 'arrowright' ? 1 : -1) + edFrames().length) % edFrames().length);
  }
  if (k === ' ') {
    e.preventDefault();
    edPlay(!ed.playing);
  }
});

// ------------------------------------------------- every direction at once
// The ring: each way the animal faces, and where its art comes from - the
// game mirrors SW, W and NW from SE, E and NE (about the ground point)
const ED_RING = [['N', 'N'], ['NE', 'NE'], ['E', 'E'], ['SE', 'SE'], ['S', 'S'], ['SW', 'SE'], ['W', 'E'], ['NW', 'NE']];
function edRing() {
  const ed = AN.ed;
  if (!ed) return;
  edGuideThumb();
  for (const [d, from] of AN.item ? AN_DIRS.map(d => [d, d]) : ED_RING) {
    const c = $('ed-ring-' + d);
    if (!c) continue;
    const fr = ed.dirs[from];
    const f = fr[Math.min(ed.fi, fr.length - 1)];
    const g = c.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, c.width, c.height);
    if (!f) continue;
    const t = document.createElement('canvas');
    t.width = ed.w;
    t.height = ed.h;
    t.getContext('2d').putImageData(edComposite(f), 0, 0);
    if (from !== d) g.setTransform(-1, 0, 0, 1, 2 * ed.ax + 1, 0);
    g.drawImage(t, 0, 0);
  }
}
let edRingTimer = null;
function edRingSoon() {
  if (edRingTimer) return;
  edRingTimer = requestAnimationFrame(() => {
    edRingTimer = null;
    edRing();
  });
}

// (a change to every frame: the whole drawing kept for undo)
function edSnapshot() {
  const ed = AN.ed;
  const dirs = {};
  for (const d of AN_DIRS) dirs[d] = ed.dirs[d].map((f) => ({ cells: f.cells.map((c) => (c ? new ImageData(new Uint8ClampedArray(c.data), c.width, c.height) : null)) }));
  return { all: true, w: ed.w, h: ed.h, ax: ed.ax, ay: ed.ay, ms: ed.ms, fi: ed.fi, dir: ed.dir, showRef: ed.showRef, dirs, ref: ed.ref, rdx: ed.ref ? ed.ref.dx : 0, rdy: ed.ref ? ed.ref.dy : 0 };
}
function edRestore(u) {
  const ed = AN.ed;
  Object.assign(ed, { w: u.w, h: u.h, ax: u.ax, ay: u.ay, ms: u.ms, fi: u.fi, dir: u.dir, showRef: u.showRef, dirs: u.dirs, zoom: 0, ref: u.ref });
  if (ed.ref) Object.assign(ed.ref, { dx: u.rdx, dy: u.rdy });
}
function edPushAll() {
  const ed = AN.ed;
  ed.undo.push(edSnapshot());
  if (ed.undo.length > 100) ed.undo.shift();
  ed.redo = [];
}
// Every cell of every frame and direction drawn again on a canvas of the new
// size: each new pixel from the old one map(d)(x, y) gives (nearest, so the
// pixels stay pixels)
function edRemap(map, w, h, ax, ay) {
  const ed = AN.ed;
  edPushAll();
  for (const d of AN_DIRS) {
    const at = map(d);
    for (const f of ed.dirs[d])
      f.cells = f.cells.map((c) => {
        if (!c) return null;
        const out = new ImageData(w, h);
        for (let y = 0; y < h; y++)
          for (let x = 0; x < w; x++) {
            const [sx, sy] = at(x + 0.5, y + 0.5);
            const X = Math.floor(sx),
              Y = Math.floor(sy);
            if (X < 0 || Y < 0 || X >= c.width || Y >= c.height) continue;
            const i = (Y * c.width + X) * 4,
              o = (y * w + x) * 4;
            if (!c.data[i + 3]) continue;
            out.data[o] = c.data[i];
            out.data[o + 1] = c.data[i + 1];
            out.data[o + 2] = c.data[i + 2];
            out.data[o + 3] = c.data[i + 3];
          }
        return out;
      });
  }
  if (ed.ref) {
    ed.ref.dx += ax - ed.ax;
    ed.ref.dy += ay - ed.ay;
  }
  if (ed.band) {
    ed.band.y0 += ay - ed.ay;
    ed.band.y1 += ay - ed.ay;
  }
  Object.assign(ed, { w, h, ax, ay, zoom: 0, dirty: true });
  renderModKit();
}
// (the way each direction faces, on screen: its body runs along this)
const ED_AXIS = { N: [0, -1], NE: [0.894, -0.447], E: [1, 0], SE: [0.894, 0.447], S: [0, 1] };
// (the middle of what's drawn facing d, over all its frames: the body's
// middle, which a longer body grows out from)
function edMiddle(d) {
  const ed = AN.ed;
  let sx = 0,
    sy = 0,
    n = 0;
  for (const f of ed.dirs[d])
    f.cells.forEach((c, li) => {
      if (!c || ed.layers[li].shadow) return;
      for (let y = 0; y < c.height; y++)
        for (let x = 0; x < c.width; x++)
          if (c.data[(y * c.width + x) * 4 + 3]) {
            sx += x;
            sy += y;
            n++;
          }
    });
  return n ? [sx / n + 0.5, sy / n + 0.5] : [ed.ax + 0.5, ed.ay + 0.5];
}

Object.assign(ACTIONS, {
  // Bigger or smaller, about the ground point
  edAllSize: (dir) => {
    const ed = AN.ed;
    const s = +dir > 0 ? 1.1 : 1 / 1.1;
    const l = ed.ax * s,
      t = ed.ay * s;
    const w = Math.ceil(l + (ed.w - ed.ax) * s),
      h = Math.ceil(t + (ed.h - ed.ay) * s);
    const ax = Math.round(l),
      ay = Math.round(t);
    if (w > 512 || h > 512) return toast('That would be more than 512 pixels across.', 3000);
    edRemap(() => (x, y) => [ed.ax + (x - ax) / s, ed.ay + (y - ay) / s], w, h, ax, ay);
  },
  // Longer or shorter along the way it faces, in each direction (N and S
  // along the screen's up and down, as the game's art is drawn)
  edBody: (dir) => {
    const ed = AN.ed;
    const k = +dir > 0 ? 1.15 : 1 / 1.15;
    const pad = k > 1 ? Math.ceil(((k - 1) * Math.max(ed.w, ed.h)) / 2) + 2 : 0;
    const mids = {};
    for (const d of AN_DIRS) mids[d] = edMiddle(d);
    edRemap(
      (d) => {
        const [vx, vy] = ED_AXIS[d],
          [mx, my] = mids[d];
        return (x, y) => {
          const dx = x - pad - mx,
            dy = y - pad - my;
          const p = (dx * vx + dy * vy) * (1 - 1 / k);
          return [mx + dx - p * vx, my + dy - p * vy];
        };
      },
      ed.w + 2 * pad,
      ed.h + 2 * pad,
      ed.ax + pad,
      ed.ay + pad,
    );
  },
  // The height band taller or shorter: what's above it moves up or down,
  // the ground stays where it is - the same heights in every direction
  edBand: (dir) => {
    const ed = AN.ed;
    if (!ed.band) return toast('Pick a height band first: the ⇕ tool, dragged up or down the drawing.', 3500);
    const k = +dir > 0 ? 1.15 : 1 / 1.15;
    const a = ed.band.y0,
      b = ed.band.y1,
      H = b - a;
    const nh = Math.max(1, Math.round(H * k)),
      delta = nh - H,
      off = Math.max(0, delta);
    edRemap(
      () => (x, y) => {
        if (y >= b + off) return [x, y - off];
        if (y >= b + off - nh) return [x, b - (b + off - y) * (H / nh)];
        return [x, y - off + delta];
      },
      ed.w,
      ed.h + off,
      ed.ax,
      ed.ay + off,
    );
    // (the band, its new height)
    AN.ed.band = { y0: b + off - nh, y1: b + off };
  },
  // One colour and its shades turned into another, everywhere
  edRecolour: async () => {
    const ed = AN.ed;
    if (!ed) return;
    const a = await ask(
      'Recolour a colour everywhere',
      [
        { key: 'from', label: 'This colour (the pen colour: pick one off the drawing with ⊙ first)', value: ed.color },
        { key: 'to', label: 'Into (#rrggbb): its lighter and darker shades follow', hint: '#6b4a2a' },
        { key: 'near', label: 'How close a colour counts (0 just that one, 100 a lot)', value: '25' },
        { key: 'where', label: 'On', value: 'all', options: [{ value: 'all', label: 'Every layer' }, { value: 'this', label: `This layer (${ed.layers[ed.li].name})` }] },
      ],
      'Recolour',
    );
    if (!a) return;
    const hex = (v) => /^#?[0-9a-f]{6}$/i.test((v || '').trim());
    if (!hex(a.from) || !hex(a.to)) return toast('Colours as #rrggbb.', 3000);
    const F = edHex('#' + a.from.trim().replace('#', '')),
      T = edHex('#' + a.to.trim().replace('#', ''));
    const lum = (r, g, b) => 0.299 * r + 0.587 * g + 0.114 * b;
    const lf = Math.max(1, lum(...F));
    const near = Math.max(0, Math.min(100, +a.near || 0)) * 2.2;
    edPushAll();
    let n = 0;
    for (const d of AN_DIRS)
      for (const f of ed.dirs[d])
        f.cells.forEach((c, li) => {
          if (!c || ed.layers[li].shadow || (a.where === 'this' && li !== ed.li)) return;
          const p = c.data;
          for (let i = 0; i < p.length; i += 4) {
            if (!p[i + 3]) continue;
            // (its shade kept: as much lighter or darker than the picked
            // colour as it was)
            if (Math.hypot(p[i] - F[0], p[i + 1] - F[1], p[i + 2] - F[2]) > near) continue;
            const r = lum(p[i], p[i + 1], p[i + 2]) / lf;
            p[i] = Math.min(255, Math.round(T[0] * r));
            p[i + 1] = Math.min(255, Math.round(T[1] * r));
            p[i + 2] = Math.min(255, Math.round(T[2] * r));
            n++;
          }
        });
    ed.dirty = true;
    toast(n ? `${n.toLocaleString()} pixels recoloured, in every frame and direction.` : 'Nothing that colour was drawn.', 3500);
    renderModKit();
  },
});

// ------------------------------------------------------------------ zoom
// (the most that fits the view)
function edFitZoom() {
  const ed = AN.ed;
  const st = $('ed-stage');
  if (!st) return ed.zoom || 4;
  return Math.max(1, Math.min(40, Math.floor(Math.min((st.clientWidth - 24) / ed.w, (st.clientHeight - 24) / ed.h))));
}
// Zoomed to z, the pixel under (cx, cy) on the screen - the mouse, or the
// view's middle - kept where it is
function edSetZoom(z, cx, cy) {
  const ed = AN.ed;
  const st = $('ed-stage'),
    c = $('ed-canvas');
  if (!ed || !st || !c) return;
  z = Math.max(1, Math.min(40, Math.round(z)));
  if (z === ed.zoom) return;
  const sr = st.getBoundingClientRect();
  if (cx === undefined) {
    cx = sr.left + sr.width / 2;
    cy = sr.top + sr.height / 2;
  }
  const r = c.getBoundingClientRect();
  const px = (cx - r.left) / ed.zoom,
    py = (cy - r.top) / ed.zoom;
  ed.zoom = z;
  c.width = ed.w * z;
  c.height = ed.h * z;
  edDraw();
  const r2 = c.getBoundingClientRect();
  st.scrollLeft += r2.left + px * z - cx;
  st.scrollTop += r2.top + py * z - cy;
  const zl = $('ed-zoom');
  if (zl) zl.textContent = z + '×';
}

// ------------------------------------------------------------- stencils
// The stencil card's picture: the stencil's frame, as it lies under this one
function edGuideThumb() {
  const ed = AN.ed;
  const c = $('ed-guide');
  if (!c || !ed.ref) return;
  const g = c.getContext('2d');
  g.clearRect(0, 0, c.width, c.height);
  const rs = ed.ref.dirs[ed.dir] || [];
  const img = rs[ed.fi % Math.max(1, rs.length)];
  if (!img) return;
  const t = document.createElement('canvas');
  t.width = img.width;
  t.height = img.height;
  t.getContext('2d').putImageData(img, 0, 0);
  g.drawImage(t, ed.ref.dx, ed.ref.dy);
}
// A rendered animation (animalmaker.py's) made the guide: decoded, placed by
// its ground point under the drawing's
async function edGuideFrom(ref) {
  const ed = AN.ed;
  const g = { from: ref.from, dx: ed.ax - ref.ax, dy: ed.ay - ref.ay, dirs: {} };
  for (const [d, frames] of Object.entries(ref.dirs)) g.dirs[d] = await Promise.all(frames.map((b) => edDecode(b, ref.w, ref.h)));
  return g;
}
// The guide copied in as the drawing: its art on the first layer that isn't
// the shadow, its shadow pixels (see-through in it) on the shadow layer
function edCopyGuide() {
  const ed = AN.ed;
  const shadowAt = ed.layers.findIndex((l) => l.shadow);
  let bodyAt = ed.layers.findIndex((l, i) => !l.shadow && i === ed.li);
  if (bodyAt < 0) bodyAt = ed.layers.findIndex((l) => !l.shadow);
  for (const d of AN_DIRS) {
    const srcs = ed.ref.dirs[d] || [];
    ed.dirs[d] = srcs.map((src) => {
      const cells = ed.layers.map(() => null);
      const body = edBlank(ed.w, ed.h),
        sh = edBlank(ed.w, ed.h);
      for (let y = 0; y < src.height; y++)
        for (let x = 0; x < src.width; x++) {
          const X = x + ed.ref.dx,
            Y = y + ed.ref.dy;
          if (X < 0 || Y < 0 || X >= ed.w || Y >= ed.h) continue;
          const i = (y * src.width + x) * 4,
            o = (Y * ed.w + X) * 4,
            a = src.data[i + 3];
          if (!a) continue;
          const t = a === 255 || shadowAt < 0 ? body : sh;
          t.data[o] = t === body ? src.data[i] : 0;
          t.data[o + 1] = t === body ? src.data[i + 1] : 0;
          t.data[o + 2] = t === body ? src.data[i + 2] : 0;
          t.data[o + 3] = 255;
        }
      if (bodyAt >= 0) cells[bodyAt] = body;
      if (shadowAt >= 0) cells[shadowAt] = sh;
      return { cells };
    });
    if (!ed.dirs[d].length) ed.dirs[d].push({ cells: ed.layers.map(() => null) });
  }
  ed.fi = 0;
}
// (an animation read for drawing: every frame in every direction, ~seconds)
async function edReadAnim(path) {
  AN.busy = 'Reading it…';
  toast(`Reading ${esc(path)}: every frame in every direction…`, 6000);
  const r = await anRequest({ op: 'load', type: AN.info.type, sub: AN.sub, anim: '__stencil__', reference: path });
  AN.busy = '';
  if (!r.ok || !r.reference) {
    toast(esc(r.error || 'It couldn’t be read.'), 5000);
    return null;
  }
  return r.reference;
}

// The stencil browser: every animal (the game's and the mods' on), its
// animations, and the picked one playing in all eight directions - to see
// it's the one before drawing over it or starting from it.
//   opts: {slot, start (a new drawing: Blank is offered too), linked (the
//   path the slot plays now)}  ->  {how: 'blank' | 'guide' | 'copy', path}
const SB = { q: '', animal: null, path: null, rows: {}, prev: null, fi: 0, timer: null, done: null, opts: null, cat: [] };
async function edStencilBrowser(opts) {
  SB.opts = opts;
  SB.cat = (await stCatalog())
    .filter((i) => /\.(ai|uca|ucs|ucb)$/i.test(i.file) && sbKindOf(i))
    .concat(SB_GUESTS)
    .sort((x, y) => itemName(x).localeCompare(itemName(y)));
  if (SB.cat.length <= SB_GUESTS.length) {
    toast('The Test Zoo’s engine lists the game’s items: start it first (Test Zoo).', 5000);
    return null;
  }
  // (the animation the slot plays now, to start with: its animal picked)
  const linked = (opts.linked || '').toLowerCase();
  const m = /^animals\/([^/]+)\//.exec(linked);
  const guess = AN.item ? SB.cat.find(i => i.file.toLowerCase() === (AN.item.kind === 'guest' ? 'guests/' + AN.item.guestKind : AN.item.source)) : m && SB.cat.find((i) => i.file.toLowerCase() === `animals/${m[1]}.ai` || i.file.toLowerCase() === `animals/${m[1]}.uca`);
  SB.animal = guess ? guess.file : SB.animal || (SB.cat.find((i) => sbKindOf(i) === 'animals') || SB.cat[0]).file;
  SB.kind = sbKindOf(SB.cat.find((i) => i.file === SB.animal));
  SB.path = linked || null;
  SB.prev = null;
  SB.scrolled = false;
  return new Promise((resolve) => {
    SB.done = resolve;
    let el = $('stb');
    if (!el) {
      el = document.createElement('div');
      el.id = 'stb';
      document.body.appendChild(el);
    }
    sbRender();
    sbPickAnimal(SB.animal, SB.path);
  });
}
function sbClose(result) {
  clearInterval(SB.timer);
  const el = $('stb');
  if (el) el.remove();
  const d = SB.done;
  SB.done = null;
  if (d) d(result);
}
function sbRender() {
  const el = $('stb');
  if (!el) return;
  const q = SB.q.trim().toLowerCase();
  // (by kind - or, searching, every kind)
  const list = SB.cat
    .filter((i) => (q ? `${itemName(i)} ${i.file}`.toLowerCase().includes(q) : sbKindOf(i) === SB.kind))
    .map(
      (i) => `<div class="sbanimal${i.file === SB.animal ? ' sel' : ''}" data-sb="animal" data-arg="${esc(i.file)}" title="${esc(i.file)}">${iconImg(i.icon, 'sbicon')}<span>${esc(itemName(i))}</span>${
        q ? `<i>${esc((SB_KINDS.find((k) => k[0] === sbKindOf(i)) || ['', ''])[1])}</i>` : ''
      }</div>`,
    )
    .join('');
  const kinds = SB_KINDS.map(([k, label]) => `<div class="${k === SB.kind && !q ? 'on' : ''}" data-sb="kind" data-arg="${k}">${label}</div>`).join('');
  const rows = SB.rows[SB.animal];
  const chips = !rows
    ? '<span class="dim">Reading its animations…</span>'
    : rows.length
      ? rows.map((r) => `<div class="sbchip${r.path === SB.path ? ' sel' : ''}" data-sb="anim" data-arg="${esc(r.path)}" title="${esc(r.path)}">${esc(r.slot)}<span>${r.frames}</span></div>`).join('')
      : '<span class="dim">It has no animations of its own.</span>';
  const p = SB.prev && SB.prev.path === SB.path ? SB.prev : null;
  const ring = SB.ring && SB.prev && SB.prev.path === SB.path ? SB.ring.map((d) => [d, d]) : ED_RING;
  const tiles = ring.map(([d, from]) => `<div class="sbtile${from !== d ? ' mir' : ''}"><canvas id="sb-${d}" width="150" height="120"></canvas><span>${d}</span></div>`).join('');
  const a = SB.cat.find((i) => i.file === SB.animal);
  const start = SB.opts.start;
  el.innerHTML = `<div class="sbwin">
      <div class="sbhead"><div class="sbtitle">${ic('stencil')} Choose a stencil${SB.opts.slot ? ` <span class="dim">for ${esc(SB.opts.slot)}</span>` : ''}</div>
        <input id="sb-q" placeholder="Find anything: lion, tree, keeper, fence…" value="${esc(SB.q)}"><div class="edib" data-sb="close" title="Close (Esc)">${ic('x')}</div></div>
      <div class="sbbody">
        <div class="sbleft"><div class="sbkinds">${kinds}</div><div class="sblist">${list || '<div class="dim" style="padding:10px">Nothing matches.</div>'}</div></div>
        <div class="sbmain">
          <div class="sbanimh">${a ? `${iconImg(a.icon, 'sbicon')}<b>${esc(itemName(a))}</b> <span class="dim mono">${esc(a.file)}</span>` : ''}</div>
          <div class="sbchips">${chips}</div>
          <div class="sbtiles">${tiles}</div>
          <div class="sbinfo dim">${p ? `${esc(SB.path)} · ${p.frames} frames · ${p.ms} ms a frame · the dashed three are mirrored by the game` : SB.path ? 'Reading it…' : 'Pick one of its animations.'}</div>
        </div>
      </div>
      <div class="sbfoot">
        <div class="btn small ghost" data-sb="close">Cancel</div><div class="spacer"></div>
        ${start ? '<div class="btn small" data-sb="blank" title="An empty canvas: nothing over it">Blank</div>' : ''}
        <div class="btn small${SB.path ? '' : ' disabled'}" data-sb="stencil" title="Its outline over your canvas, and its inside as faint as you like: draw your own in it">Stencil</div>
        <div class="btn small primary${SB.path ? '' : ' disabled'}" data-sb="remake" title="${start ? 'Its art copied in, every frame and direction, to change and recolour' : 'Your drawing replaced by a copy of its art (Ctrl+Z undoes it)'}">Remake</div>
      </div></div>`;
  const qi = $('sb-q');
  qi.oninput = () => {
    SB.q = qi.value;
    clearTimeout(SB.qt);
    SB.qt = setTimeout(() => {
      sbRender();
      const q2 = $('sb-q');
      q2.focus();
      q2.setSelectionRange(q2.value.length, q2.value.length);
    }, 120);
  };
  el.onclick = (e) => {
    const t = e.target.closest('[data-sb]');
    if (t) return sbAct(t.dataset.sb, t.dataset.arg);
    if (e.target === el) sbClose(null);
  };
  el.onkeydown = (e) => e.key === 'Escape' && sbClose(null);
  const sel = el.querySelector('.sbanimal.sel');
  if (sel && !SB.scrolled) {
    sel.scrollIntoView({ block: 'center' });
    SB.scrolled = true;
  }
  sbPaint();
}
function sbAct(what, arg) {
  if (what === 'close') return sbClose(null);
  if (what === 'animal') return sbPickAnimal(arg, null);
  if (what === 'anim') return sbPickAnim(arg);
  if (what === 'kind') {
    SB.kind = arg;
    SB.q = '';
    SB.scrolled = false;
    return sbRender();
  }
  if (what === 'blank') return sbClose({ how: 'blank' });
  if ((what === 'stencil' || what === 'remake') && SB.path) return sbClose({ how: what, path: SB.path });
}
async function sbPickAnimal(file, path) {
  SB.animal = file;
  SB.path = path;
  sbRender();
  if (!SB.rows[file]) {
    const r = await lab.animalDo(AN.folder, { op: 'stencils', like: file });
    SB.rows[file] = r.ok ? (r.subs.m || []).concat((r.subs.y || []).map((x) => ({ ...x, slot: 'young ' + x.slot }))) : [];
    if (SB.animal !== file) return;
  }
  const rows = SB.rows[file];
  if (!SB.path && rows.length) {
    const slot = (SB.opts.slot || '').toLowerCase();
    SB.path = (rows.find((x) => x.slot.toLowerCase() === slot) || rows.find((x) => x.slot === 'walk') || rows[0]).path;
  }
  sbRender();
  if (SB.path) sbPickAnim(SB.path);
}
// Its frames in each of the five drawn directions, from the engine (fast):
// shown playing, the other three mirrored as the game shows them
async function sbPickAnim(path) {
  SB.path = path;
  SB.prev = null;
  SB.fi = 0;
  sbRender();
  const dirs = {};
  let ms = 100,
    frames = 0;
  if ((AN.item?.kind === 'path' && path.startsWith('paths/')) ||
      (AN.item?.assetRoot && path.startsWith(AN.item.assetRoot + '/')) ||
      (!AN.item && path.startsWith('animals/' + AN.type + '/'))) {
    const r = await anRequest({ op: 'load', type: AN.type, sub: AN.sub, anim: '__preview__', reference: path });
    if (SB.path !== path || !$('stb')) return;
    const ref = r.reference;
    if (!ref) return;
    SB.ring = Object.keys(ref.dirs);
    for (const [d, pictures] of Object.entries(ref.dirs)) dirs[d] = {w:ref.w,h:ref.h,ax:ref.ax,ay:ref.ay,imgs:await Promise.all(pictures.map(sbImage))};
    SB.prev = {path,dirs,ms:ref.ms,frames:Math.max(...Object.values(ref.dirs).map(f => f.length))};
    sbRender(); clearInterval(SB.timer); SB.timer = setInterval(() => {if (!$('stb')) return clearInterval(SB.timer); SB.fi++; sbPaint();}, Math.max(40,ref.ms));
    return;
  }
  const first = await gfView(path + '.ani');
  if (SB.path !== path || !$('stb')) return;
  const has = (first && first.dirs) || AN_DIRS;
  SB.ring = has.includes('E') || has.includes('e') ? null : has;
  for (const d of SB.ring || AN_DIRS) {
    const v = await gfView(path + '.ani', d);
    if (SB.path !== path || !$('stb')) return;
    if (v && v.kind === 'anim' && Array.isArray(v.png)) {
      dirs[d] = { w: v.w, h: v.h, ax: v.anchorX, ay: v.anchorY, imgs: await Promise.all(v.png.map((b) => sbImage(b))) };
      ms = v.frameMs || ms;
      frames = Math.max(frames, v.png.length);
    }
  }
  if (SB.path !== path) return;
  SB.prev = { path, dirs, ms, frames };
  sbRender();
  clearInterval(SB.timer);
  SB.timer = setInterval(() => {
    if (!$('stb')) return clearInterval(SB.timer);
    SB.fi++;
    sbPaint();
  }, Math.max(40, ms));
}
function sbImage(b64) {
  return new Promise((res) => {
    const i = new Image();
    i.onload = () => res(i);
    i.onerror = () => res(null);
    i.src = 'data:image/png;base64,' + b64;
  });
}
function sbPaint() {
  const p = SB.prev && SB.prev.path === SB.path ? SB.prev : null;
  if (!p) return;
  // (one scale for every direction, so they compare)
  let s = 3;
  for (const v of Object.values(p.dirs)) s = Math.min(s, 140 / v.w, 110 / v.h);
  s = s >= 1 ? Math.floor(s) : s;
  for (const [d, from] of SB.ring ? SB.ring.map((x) => [x, x]) : ED_RING) {
    const c = $('sb-' + d);
    const v = p.dirs[from];
    if (!c || !v) continue;
    const g = c.getContext('2d');
    g.setTransform(1, 0, 0, 1, 0, 0);
    g.clearRect(0, 0, c.width, c.height);
    const img = v.imgs[SB.fi % v.imgs.length];
    if (!img) continue;
    g.imageSmoothingEnabled = false;
    // (the ground point at the tile's middle, low down)
    const ox = c.width / 2 - v.ax * s,
      oy = c.height * 0.72 - v.ay * s;
    if (from !== d) g.setTransform(-1, 0, 0, 1, c.width, 0);
    g.drawImage(img, ox, oy, v.w * s, v.h * s);
  }
}

Object.assign(ACTIONS, {
  // (the editor's Stencil card: another one to draw over, or to start again from)
  edStencil: async () => {
    const ed = AN.ed;
    const pick = await edStencilBrowser({ slot: ed.slot, linked: ed.ref ? ed.ref.from : '' });
    if (!pick || !AN.ed) return;
    const ref = await edReadAnim(pick.path);
    if (!ref || AN.ed !== ed) return;
    if (pick.how === 'stencil') {
      ed.ref = await edGuideFrom(ref);
      ed.showRef = true;
      ed.dirty = true;
      toast(`${esc(pick.path)} is your stencil: its outline over the drawing, its inside as faint as the slider says.`, 4000);
    } else {
      edPushAll();
      Object.assign(ed, { w: ref.w, h: ref.h, ax: ref.ax, ay: ref.ay, zoom: 0, band: null });
      ed.ref = await edGuideFrom(ref);
      edCopyGuide();
      ed.dirty = true;
      toast('Your drawing is a copy of it now: Ctrl+Z goes back.', 4000);
    }
    renderModKit();
  },
});

// ------------------------------------------------- the stencil overlay
// (the browser's kinds of thing: the game's items by where they live, and
// the guests, who have no file of their own)
const SB_KINDS = [['animals', 'Animals'], ['people', 'People'], ['buildings', 'Buildings'], ['plants', 'Trees and plants'], ['scenery', 'Scenery'], ['fences', 'Fences'], ['paths', 'Paths']];
const SB_GUESTS = [['man', 'Man', 'lsmguest'], ['woman', 'Woman', 'lsfguest'], ['boy', 'Boy', 'lsbguest'], ['girl', 'Girl', 'lsgguest']].map(([k, n, ic]) => ({
  file: 'guests/' + k,
  name: n + ' (guest)',
  icon: `guests/${ic}/${ic}`,
  guest: true,
}));
function sbKindOf(i) {
  if (!i) return 'animals';
  if (i.guest) return 'people';
  const k = toolKindOf(i);
  if (k === 'animal') return 'animals';
  if (k === 'staff') return 'people';
  if (k === 'fence') return 'fences';
  if (k === 'path') return 'paths';
  if (/^scenery\/building\//i.test(i.file)) return 'buildings';
  if (/^scenery\/foliage\//i.test(i.file)) return 'plants';
  return 'scenery';
}
// The stencil's picture under this frame (none: no stencil, or hidden)
function edStencilImg() {
  const ed = AN.ed;
  if (!ed || !ed.showRef || !ed.ref) return null;
  const rs = ed.ref.dirs[ed.dir] || ed.ref.dirs[ed.dir.toLowerCase()];
  return rs && rs.length ? rs[ed.fi % rs.length] : null;
}
// (its body: the art itself - the see-through shadow isn't the shape)
const edSolid = (img, x, y) => x >= 0 && y >= 0 && x < img.width && y < img.height && img.data[(y * img.width + x) * 4 + 3] >= 200;
// Its outline: the body's edge pixels, in a colour that shows on anything
const edOutlines = new WeakMap();
function edOutline(img) {
  let o = edOutlines.get(img);
  if (o) return o;
  o = new ImageData(img.width, img.height);
  for (let y = 0; y < img.height; y++)
    for (let x = 0; x < img.width; x++) {
      if (!edSolid(img, x, y)) continue;
      if (edSolid(img, x - 1, y) && edSolid(img, x + 1, y) && edSolid(img, x, y - 1) && edSolid(img, x, y + 1)) continue;
      const i = (y * img.width + x) * 4;
      o.data[i] = 90;
      o.data[i + 1] = 214;
      o.data[i + 2] = 255;
      o.data[i + 3] = 235;
    }
  edOutlines.set(img, o);
  return o;
}
// The outline less the pixels painted in this frame (or in the shape being
// dragged out): the see-through shadow doesn't count as paint
function edOutlineUnder(img, comp, preview) {
  const ed = AN.ed;
  const o = edOutline(img);
  const out = new ImageData(new Uint8ClampedArray(o.data), o.width, o.height);
  const painted = (X, Y) =>
    X >= 0 && Y >= 0 && X < comp.width && Y < comp.height && (comp.data[(Y * comp.width + X) * 4 + 3] >= 128 || (preview && preview.data[(Y * comp.width + X) * 4 + 3]));
  for (let y = 0; y < o.height; y++)
    for (let x = 0; x < o.width; x++) {
      const i = (y * o.width + x) * 4;
      if (out.data[i + 3] && painted(x + ed.ref.dx, y + ed.ref.dy)) out.data[i + 3] = 0;
    }
  return out;
}
// (paint kept inside: is this pixel of the drawing inside the stencil?)
function edInside(x, y) {
  const ed = AN.ed;
  // (a shadow falls outside the body: never held in)
  if (!ed.stInside || (ed.layers[ed.li] && ed.layers[ed.li].shadow)) return true;
  const img = edStencilImg();
  return !img || edSolid(img, x - ed.ref.dx, y - ed.ref.dy);
}

Object.assign(ACTIONS, {
  edStLine: () => {
    AN.ed.stLine = !AN.ed.stLine;
    store.set('edStLine', AN.ed.stLine);
    anRender();
  },
  edStInside: () => {
    const ed = AN.ed;
    ed.stInside = !ed.stInside;
    if (ed.stInside && !ed.showRef) ed.showRef = true;
    toast(ed.stInside ? 'Paint stays inside the stencil’s outline now (the eraser works anywhere).' : 'Paint goes anywhere again.', 3000);
    anRender();
  },
  // The stencil's shape filled with the pen colour, on this layer, in every
  // frame of every direction it has: a solid body to shade and detail
  edStFillAll: () => {
    const ed = AN.ed;
    if (!ed.ref) return;
    if (ed.layers[ed.li].shadow) return toast('That’s the shadow layer: pick the layer to paint on first.', 3500);
    const rgba = [...edHex(ed.color), 255];
    edPushAll();
    let n = 0;
    for (const d of AN_DIRS) {
      const rs = ed.ref.dirs[d] || [];
      if (!rs.length) continue;
      // (as many frames as the stencil has, at least)
      while (ed.dirs[d].length < rs.length) ed.dirs[d].push({ cells: ed.layers.map(() => null) });
      ed.dirs[d].forEach((f, fi) => {
        const img = rs[fi % rs.length];
        if (!f.cells[ed.li]) f.cells[ed.li] = edBlank(ed.w, ed.h);
        const c = f.cells[ed.li];
        for (let y = 0; y < ed.h; y++)
          for (let x = 0; x < ed.w; x++) {
            if (!edSolid(img, x - ed.ref.dx, y - ed.ref.dy)) continue;
            const i = (y * ed.w + x) * 4;
            c.data[i] = rgba[0];
            c.data[i + 1] = rgba[1];
            c.data[i + 2] = rgba[2];
            c.data[i + 3] = 255;
            n++;
          }
      });
    }
    ed.dirty = true;
    toast(n ? 'The stencil filled in, every frame and direction: shade and detail it from here (Ctrl+Z undoes it).' : 'The stencil has nothing facing these ways.', 4500);
    renderModKit();
  },
});

// --------------------------------------------------------- its pictures
// The pictures the game shows of it (animalmaker.py pictures): the buy
// panel's icon, the list picture and the plaque - the animal it was made
// from's at first, until it has its own
async function picsLoad() {
  if (AN.pics && AN.pics.type === AN.type) return;
  const r = await lab.animalDo(AN.folder, { op: 'pictures', type: AN.type });
  AN.pics = { type: AN.type, list: r.ok ? r.pictures : [], error: r.ok ? '' : r.error || 'Its pictures couldn’t be read.', shown: {} };
}
function picsHtml() {
  const P = AN.pics;
  if (!P || P.type !== AN.type) return '<div class="muted" style="padding:20px">Reading its pictures…</div>';
  if (P.error) return `<div class="red" style="padding:20px">${esc(P.error)}</div>`;
  const where = { icon: 'The buy panel, where it’s adopted', list: 'Lists of animals: Zoo Status, the exhibit’s', plaque: 'Its info panel, and the exhibit sign' };
  const cards = P.list
    .map((p) => {
      const from = p.path.split('/')[1] || '';
      return `<div class="edcard piccard">
        <div class="edcardh"><span>${esc(p.label)}</span><span class="dim">${p.w} x ${p.h}</span></div>
        <div class="picbox" style="aspect-ratio:${p.w} / ${p.h}"><canvas id="pic-${p.key}" width="${p.w}" height="${p.h}"></canvas></div>
        <div class="muted" style="font-size:11.5px">${esc(where[p.key])}</div>
        <div class="picrow">${p.own ? '<span class="anbadge drawn">Its own</span>' : `<span class="anbadge linked" title="${esc(p.path)}">The ${esc(from)}'s</span>`}<span class="spacer"></span>
          <div class="btn small" data-act="picDraw" data-arg="${p.key}">${p.drawn ? 'Edit' : 'Draw'}</div></div>
      </div>`;
    })
    .join('');
  return `<div class="needs"><div class="nehead"><div><div class="ststartt">Its pictures</div><div class="muted">What the game shows of it in its panels. Each is drawn here like an animation - one frame, at the game's size, with a palette of its own. Until it has its own, it shows the animal it was made from's.</div></div></div>
    <div class="btn small" data-act="picFromFrame" style="margin-bottom:12px">Use an animation frame…</div><div class="picgrid">${cards}</div></div>`;
}
// (each shown as the game draws it: the engine's own reading of it, else
// read here)
async function picsShow() {
  const P = AN.pics;
  if (!P || !P.list) return;
  for (const p of P.list) {
    const c = $('pic-' + p.key);
    if (!c || !p.path) continue;
    let png = P.shown[p.path];
    if (!png) {
      const v = await gfView(p.path + '.ani');
      png = v && v.kind === 'anim' && Array.isArray(v.png) ? v.png[0] : null;
      if (!png) {
        const r = await lab.animalDo(AN.folder, { op: 'load', type: AN.type, sub: 'pics', anim: '__preview__', reference: p.path });
        const d = r.ok && r.reference && r.reference.dirs && (r.reference.dirs.N || Object.values(r.reference.dirs)[0]);
        png = d && d[0];
      }
      if (png) P.shown[p.path] = png;
    }
    const c2 = $('pic-' + p.key);
    if (!png || !c2) continue;
    const img = await sbImage(png);
    if (!img) continue;
    const g = c2.getContext('2d');
    g.imageSmoothingEnabled = false;
    g.clearRect(0, 0, c2.width, c2.height);
    // (centred: the game's are drawn from the middle)
    g.drawImage(img, Math.round((c2.width - img.width) / 2), Math.round((c2.height - img.height) / 2));
  }
}

Object.assign(ACTIONS, {
  picFromFrame: async () => {
    const own = [...new Set(Object.values(AN.info?.subs || {}).flat().filter(r => r.status === 'drawn').map(r => r.path))];
    if (!own.length) return toast('Import or draw an animation and save it first.', 4000);
    const a = await ask('Pictures from an animation frame', [
      { key: 'source', label: 'Drawn animation', value: own[0], options: own.map(p => ({ value: p, label: p })) },
      { key: 'direction', label: 'Facing', value: 'SE', options: AN_DIRS.map(d => ({ value: d, label: d })) },
      { key: 'frame', label: 'Frame number (starting at 1)', value: '1' },
      { key: 'pictures', label: 'Pictures to make', value: 'all', options: [{ value: 'all', label: 'Icon, list picture and plaque' }, ...(AN.pics?.list || []).map(p => ({ value: p.key, label: p.label }))] },
    ], 'Make the pictures');
    if (!a) return;
    try {
      const r = await lab.animalDo(AN.folder, { op: 'load', type: AN.type, sub: 'pics', anim: '__picture_source__', reference: a.source });
      const frames = r.reference?.dirs?.[a.direction] || [], frame = AssetImport.integer(a.frame, 1, frames.length, 'Frame');
      for (const p of AN.pics.list.filter(p => a.pictures === 'all' || p.key === a.pictures)) {
        const project = await AssetImport.pictureProject(frames[frame - 1], p.w, p.h);
        const saved = await lab.animalDo(AN.folder, { op: 'savepic', type: AN.type, key: p.key, project });
        if (!saved.ok) throw Error(saved.error || 'A picture could not be saved.');
      }
      AN.pics = null; ST.insp = null; ST.problems = null; renderModKit(); toast('Pictures made from your frame. You can edit each one.', 5000);
    } catch (e) { toast(esc(e.message || String(e)), 6000); }
  },
  picDraw: async (key) => {
    const p = AN.pics && AN.pics.list.find((x) => x.key === key);
    if (!p) return;
    await edOpen(p.label, key, p.exists ? p.path : null, { key, w: p.w, h: p.h });
  },
});
