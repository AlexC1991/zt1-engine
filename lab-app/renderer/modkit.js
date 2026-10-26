// The Mods tab: making mods properly and sharing them (main.js "Your mods";
// the guide is docs/MODDING.md). Three sections, switched at the top of the
// left column: Mods (your mods - this file), Game files (every file the game
// has, gamefiles.js) and Lab scripts (the small C++ test scripts that run in
// the Test Zoo, app.js renderMods).
//
// Mods: the mods in the game's mods folder down the left (filtered by a
// search, each switched on or off right there); the picked one in the
// middle - its name, what it is and its switch at the top, then three pages:
// Overview (its mod.json details), Contents (what it adds or changes: the
// game things in its .ztd files, its shaders, its HD art, its settings) and
// Publish (checked against the mod repository's rules, uploaded, and the
// mods you've uploaded, signed in with your CodeRook account).

const MK = {
  mods: [], folder: '', sel: null, check: null, log: [], mine: null, signedIn: false, busy: false, newOpen: false,
  page: 'overview', filter: '', contents: null,
  picture: null, // the shown mod's picture: {folder, url}
};

// The section switch at the top of the left column
function modkitTabs() {
  const tab = (k, label, what) =>
    `<div class="${S.modsLeft === k ? 'on' : ''}" data-act="modsLeft" data-arg="${k}" title="${esc(what)}">${label}</div>`;
  return `<div class="seg mini lefttabs mksections">${tab('library', 'Mods', 'Your mods: make, test and share them')}${tab(
    'game',
    'Game files',
    "Every file the game has: the paths a mod's .ztd uses, readable, and the animation maker",
  )}${tab('mods', 'Lab scripts', 'Small C++ scripts that set up the Test Zoo and check one rule of the original against our engine')}</div>`;
}

async function modkitLoad() {
  const r = await lab.gameModsList();
  MK.folder = r.folder;
  MK.mods = (r.mods || []).filter((m) => !m.folder.startsWith('.'));
  // (the one picked last, kept for a Lab started again)
  if (!MK.sel) MK.sel = store.get('mkSel', null);
  if (!MK.sel || !MK.mods.some((m) => m.folder === MK.sel)) MK.sel = MK.mods.length ? MK.mods[0].folder : null;
  store.set('mkSel', MK.sel);
  MK.signedIn = (await lab.modkitToken()).signedIn;
}

// What kind of mod it is, from its contents (once read) or its mod.json
function mkKinds(m, c) {
  const k = [];
  if (c) {
    if (c.ztds.length) k.push('Game files');
    if (c.shaders.length) k.push('Shaders');
    if (c.hd.files) k.push('HD art');
  }
  if (m && m.imported) k.push('Brought in');
  return k;
}
const mkSize = (b) => (b >= 1048576 ? (b / 1048576).toFixed(1) + ' MB' : b >= 1024 ? Math.round(b / 1024) + ' KB' : (b || 0) + ' B');

async function renderModKit() {
  // (only the newest draws: an older one still reading - a mod picked, then
  // another - would otherwise finish last and put the first one back)
  const gen = (MK.gen = (MK.gen || 0) + 1);
  // (the first time: something to see while the mods are read)
  if (!$('v-mods').firstElementChild) $('v-mods').innerHTML = '<div class="stagemsg"><b>Reading your mods…</b></div>';
  await modkitLoad();
  const m = MK.mods.find((x) => x.folder === MK.sel);
  if (m && (!MK.contents || MK.contents.folder !== m.folder)) {
    const c = await lab.modkitContents(m.folder);
    if (gen !== MK.gen) return;
    MK.contents = { folder: m.folder, ...c };
  }
  if (m && (!MK.picture || MK.picture.folder !== m.folder)) {
    // (no picture, or it can't be read: the page shows without one)
    const r = await lab.modkitPicture(m.folder, { op: 'get' }).catch(() => null);
    if (gen !== MK.gen) return;
    MK.picture = { folder: m.folder, url: (r && r.url) || '' };
  }
  if (gen !== MK.gen) return;
  const c = m && MK.contents && MK.contents.folder === m.folder && MK.contents.ok ? MK.contents : null;
  // (drawn as the studio: studio.js)
  await renderStudio(gen, m, c);
  iconButtons($('v-mods'));
}

function mkEmpty() {
  return `<div class="stagemsg"><b>No mods yet</b><span>A mod is a folder in the game's mods folder: a mod.json saying what it is, and its files - a .ztd of game files, shaders, or HD art.<br>Make one from a template, or bring in an old Zoo Tycoon download (dropping it here works too).</span><div style="display:flex;gap:10px;justify-content:center"><div class="btn primary" data-act="mkNew">New mod</div><div class="btn" data-act="mkImport">Bring one in</div></div></div>`;
}

function mkOverviewPage(m) {
  const field = (k, label, about, long) =>
    `<label class="mkf${long ? ' wide' : ''}"><span>${label}</span>${
      long
        ? `<textarea class="mkfield" data-k="${k}" rows="4">${esc(m[k] || '')}</textarea>`
        : `<input class="mkfield" data-k="${k}" value="${esc(m[k] || '')}"/>`
    }<small>${about}</small></label>`;
  const pic = MK.picture && MK.picture.folder === m.folder ? MK.picture.url : '';
  const busy = MK.busy ? ' disabled' : '';
  return `<div class="mkpicture">
      <div class="mkpicbox">${pic ? `<img src="${pic}" alt="Its picture">` : `<span class="muted">No picture yet</span>`}</div>
      <div class="mkpicside"><div class="label">Its picture</div>
        <div class="muted" style="font-size:12px">The Mod Menu shows it beside the mod (367 x 276; a picture of another shape is cut to fill it, from the middle). It's picture.png in the mod's folder.</div>
        <div class="mkpicbtns"><div class="btn small${busy}" data-act="mkPicture" data-arg="file">Choose a picture…</div><div class="btn small${busy}" data-act="mkPicture" data-arg="zoo" title="The Test Zoo's view as it is now">From the Test Zoo view</div>${
          pic ? `<div class="btn small ghost${busy}" data-act="mkPicture" data-arg="remove">Remove</div>` : ''
        }</div></div></div>
    <div class="mkform">
      ${field('name', 'Name', 'What the Mod Menu calls it.')}
      ${field('id', 'Id', 'Its id in the mod repository: lower-case letters, numbers, . _ - (made from the name if empty). Yours alone once uploaded.')}
      ${field('author', 'Author', "Who made it (and who it's credited to).")}
      ${field('version', 'Version', "e.g. 1.0 - raise it when you change it, so players see there's an update.")}
      ${field('about', 'About', 'What it does, in a sentence or two. Players read this in the Mod Menu.', true)}
    </div>
    <div class="mkactions"><div class="btn primary${MK.busy ? ' disabled' : ''}" data-act="mkSave">Save</div><span class="muted" id="mk-dirty">${MK.dirty ? 'Not saved' : ''}</span></div>
    <div class="mkguide"><div class="label">Making a mod</div>
      <div><b>Game files</b> (new or changed animals, objects, scenery) go in a .ztd: a zip with the game's own paths inside. <span class="blue clickable" data-act="modsLeft" data-arg="game">Game files</span> shows every path; its animation maker makes a recoloured animal as a mod.</div>
      <div><b>Shaders</b> go in a shaders/ folder (mods/zt1-toon is the example). <b>HD art</b> goes in hd/; the HD art lab script makes it.</div>
      <div><b>Only game data:</b> no programs or scripts - the mod repository turns those down. <b>Old downloads</b> work as they did: bring them in here, or in the game (Mod Menu › Add a mod...).</div>
      <div class="muted">The whole guide: <span class="mono">docs/MODDING.md</span></div></div>`;
}

function mkContentsPage(m, c) {
  if (!MK.contents || MK.contents.folder !== m.folder) return '<div class="muted">Reading it…</div>';
  if (!c) return `<div class="red">${esc(MK.contents.message || "Its files couldn't be read.")}</div>`;
  const sec = (title, inner, note) => `<div class="mksec"><div class="label">${title}${note ? ` <span class="dim">${note}</span>` : ''}</div>${inner}</div>`;
  const out = [];
  if (c.things.length) {
    const byKind = {};
    for (const t of c.things) (byKind[t.kind] = byKind[t.kind] || []).push(t);
    out.push(
      sec(
        'What it adds or changes',
        Object.entries(byKind)
          .map(
            ([k, ts]) => `<div class="mkgroup"><div class="mkgk">${esc(k)} <span class="dim">${ts.length}</span></div>${ts
              .map((t) => `<div class="mkthing mono" data-act="mkOpenThing" data-arg="${esc(t.ztd + '|' + (t.file || t.path))}" title="${esc(t.ztd + ': ' + (t.file || t.path))} - open it to change">${esc(t.path)}</div>`)
              .join('')}</div>`,
          )
          .join(''),
        `${c.things.length} in its .ztd files`,
      ),
    );
  }
  if (c.ztds.length)
    out.push(
      sec(
        'Game files',
        c.ztds
          .map((z) => `<div class="mkline"><span class="mono">${esc(z.file)}</span><span class="dim">${z.bad ? '<span class="red">not a .ztd the game can read</span>' : `${z.entries} files`} · ${mkSize(z.bytes)}</span></div>`)
          .join('') +
          `<div class="mkkinds">${Object.entries(c.kinds)
            .map(([k, n]) => `<span class="mkbadge">${esc(k)} ${n}</span>`)
            .join('')}</div>`,
      ),
    );
  if (c.shaders.length)
    out.push(
      sec(
        'Shaders',
        c.shaders
          .map((s) => `<div class="mkline"><span>${esc(s.name)} <span class="dim">${esc(s.stage === 'screen' ? 'whole screen' : 'map art')}${s.settings ? ` · ${s.settings} settings` : ''}</span></span><span class="dim">${esc(s.about)}</span></div>`)
          .join(''),
        'Game Options › Shaders',
      ),
    );
  if (c.hd.files) out.push(sec('HD art', `<div class="mkline"><span>${c.hd.files} frames</span><span class="dim">${mkSize(c.hd.bytes)}, shown only zoomed in</span></div>`));
  if (c.replaces.length)
    out.push(sec('It changes', c.replaces.map((r) => `<div class="mkthing mono">${esc(r)}</div>`).join(''), 'mod.json "replaces": the Mod Menu lists these'));
  if (c.settings.length)
    out.push(
      sec(
        'Its settings',
        c.settings.map((s) => `<div class="mkline"><span>${esc(s.label || s.name || '?')}</span><span class="dim">${esc(s.type || (s.min !== undefined ? `${s.min}–${s.max}` : ''))}</span></div>`).join(''),
        'shown in Game Options',
      ),
    );
  if (c.other.length)
    out.push(sec('Other files', c.other.map((o) => `<div class="mkline"><span class="mono">${esc(o.path)}</span><span class="dim">${mkSize(o.bytes)}</span></div>`).join('')));
  return out.length ? out.join('') : '<div class="muted">Nothing in it yet but its mod.json. Put its files in its folder (Open folder, above).</div>';
}

function mkPublishPage(m) {
  const check = MK.check && MK.check.folder === MK.sel ? MK.check : null;
  const up = MK.mine && MK.mine.ok !== false && (MK.mine.mods || []).find((u) => u.id === m.id || u.id === (m.id || '').toLowerCase());
  const busy = MK.busy ? ' disabled' : '';
  return `<div class="mksec"><div class="label">Theme pack</div>
      <div class="muted" style="font-size:12px">Combine two or more local mods into one pack, with its own name, picture and repository listing. It is a snapshot: edits to the originals need a new pack build. Turn off the separate originals when testing the pack.</div>
      <div><div class="btn small${busy}" data-act="mkThemePack">Build a theme pack…</div></div>
      ${m.themePack ? `<div class="muted">Includes ${(m.themePack.components || []).map(c => esc(c.name)).join(', ')}</div>` : ''}</div>
    <div class="mksec"><div class="label">Check it</div>
      <div class="muted" style="font-size:12px">The rules the mod repository's own checks use: what it may hold, its id and its details.</div>
      <div><div class="btn small${busy}" data-act="mkCheck">Check it</div></div>
      ${
        check
          ? `<div class="mkcheck">${check.ok ? '<div class="green">✓ It follows the rules</div>' : "<div class=\"red\">✗ It can't be uploaded yet</div>"}<div class="dim">${check.files || 0} files, ${mkSize(check.bytes || 0)}</div>${check.errors
              .map((e) => `<div><span class="red">✗</span> ${esc(e)}</div>`)
              .join('')}${check.warnings.map((w) => `<div><span class="amber">!</span> ${esc(w)}</div>`).join('')}</div>`
          : ''
      }</div>
    <div class="mksec"><div class="label">The mod repository</div>
      ${up ? `<div class="mkline"><span><b>${esc(up.name)}</b> <span class="dim">${esc(up.version)}</span></span><span class="${up.live ? 'green' : 'amber'}">${esc(up.status)}</span></div>${up.reason ? `<div class="red" style="font-size:12px">${esc(up.reason)}</div>` : ''}` : ''}
      <div class="muted" style="font-size:12px">Uploaded, it's checked there (the same rules, and a virus scan), then offered in every official game's Mod Menu. Uploading it again updates it.</div>
      <div style="display:flex;gap:8px"><div class="btn small primary${busy}${MK.signedIn ? '' : ' disabled'}" data-act="mkUpload">${up ? 'Upload this version' : 'Upload it'}</div>${
        up ? `<div class="btn small ghost" data-act="mkTakedown" data-arg="${esc(up.id)}">Take it down</div>` : ''
      }</div>
      <div class="code" id="mk-log" style="font-size:12px;white-space:pre-wrap;${MK.log.length ? '' : 'display:none'}">${esc(MK.log.join('\n'))}</div></div>
    <div class="mksec"><div class="label">Your uploads</div>${mkUploads()}</div>`;
}

function mkUploads() {
  if (!MK.signedIn)
    return `<div class="muted" style="font-size:12px">Uploading needs a CodeRook account (coderook.com). Sign in with <span class="mono">cbx sign-in</span>, or paste a personal access token:</div><div style="display:flex;gap:6px"><input id="mk-token" type="password" placeholder="CodeRook token" style="flex:1"/><div class="btn small primary" data-act="mkSignIn">Sign in</div></div>`;
  const head = `<div class="muted" style="font-size:12px">Signed in to CodeRook. <span class="blue clickable" data-act="mkSignOut">Sign out</span> · <span class="blue clickable" data-act="mkMine">Refresh</span></div>`;
  if (MK.mine === null) return head + '<div class="muted" style="font-size:12px">…</div>';
  if (MK.mine.ok === false) return head + `<div class="muted" style="font-size:12px">${esc(MK.mine.message)}</div>`;
  if (!MK.mine.mods.length) return head + '<div class="muted" style="font-size:12px">Nothing uploaded yet.</div>';
  return (
    head +
    MK.mine.mods
      .map(
        (u) => `<div class="mkline"><span><b>${esc(u.name || u.id)}</b> <span class="dim">${esc(u.version || '')}</span></span><span><span class="${u.live ? 'green' : 'amber'}">${esc(
          u.status,
        )}</span> · <span class="blue clickable" data-act="mkTakedown" data-arg="${esc(u.id)}">Take it down</span></span></div>${u.reason ? `<div class="red" style="font-size:12px">${esc(u.reason)}</div>` : ''}`,
      )
      .join('')
  );
}

Object.assign(ACTIONS, {
  mkThemePack: async () => {
    if (MK.busy) return;
    if (MK.dirty) await ACTIONS.mkSave();
    const choices = (MK.mods || []).filter(m => !m.themePack);
    if (choices.length < 2) return toast('Make or bring in at least two mods first.');
    const fields = [
      { key: 'name', label: 'Theme pack name' },
      { key: 'id', label: 'Repository id (optional; made from the name)' },
      { key: 'author', label: 'Made by', value: store.get('mkAuthor', '') },
      { key: 'version', label: 'Version', value: '1.0' },
      { key: 'about', label: 'About this pack' },
      ...choices.map((m,i) => ({ key: 'mod' + i, label: m.name || m.folder, value: m.folder === MK.sel ? 'yes' : 'no', options: [{value:'no',label:'Leave out'},{value:'yes',label:'Include in pack'}] })),
    ];
    const a = await ask('Build a theme pack', fields, 'Build pack');
    if (!a) return;
    const folders = choices.filter((m,i) => a['mod' + i] === 'yes').map(m => m.folder);
    MK.busy = true; renderModKit();
    let r;
    try { r = await lab.animalDo(MK.sel, { op: 'new', itemOp: true, themePack: true, folders, name:a.name, id:a.id, author:a.author, version:a.version, about:a.about }); }
    finally { MK.busy = false; }
    if (!r?.ok) { renderModKit(); return toast(esc(r?.error || 'The pack could not be built.'), 9000); }
    await modkitLoad(); MK.sel = r.folder; MK.picture = null; MK.contents = null;
    stInvalidate(); stSetNode('about'); await renderModKit();
    toast('Pack built. Set its own picture here, then use Publish to check and upload this single pack.', 6500);
  },
  mkPick: (folder) => {
    if (MK.dirty && !confirm('Leave this mod without saving its details?')) return;
    MK.dirty = false;
    MK.sel = folder;
    MK.node = null;
    MK.log = [];
    ST.picker = false;
    stInvalidate();
    renderModKit();
  },
  // (the old pages, as the studio's places: Overview, Contents, Animals, Art
  // and Publish)
  mkPage: (p) => {
    const first = (AN.list || [])[0];
    const node = { overview: 'about', contents: 'files', art: 'art', publish: 'publish', animals: first ? 'animal:' + first.type : 'start' }[p] || p;
    ACTIONS.stPick(node);
  },
  // (a thing it adds, open in Game files: the game's own reading of it -
  // so the mod has to be on)
  // (its own copy, open to change: saving puts it back in the mod)
  mkOpenThing: (arg) => {
    const [ztd, file] = String(arg).split('|');
    if (MK.sel && file) openModEntry(MK.sel, ztd, file);
  },
  // (Write Code, a file in a mod: back to the mod, or try it)
  mkShowMod: (folder) => {
    MK.sel = folder;
    stSetNode('files');
    MK.contents = null;
    stInvalidate();
    S.modsLeft = 'library';
    setTab('mods');
  },
  mkTryMod: async (folder) => {
    if (S.eng.file && !(await saveEngineFile(S.eng.file))) return;
    if (!MK.mods) await modkitLoad();
    const m = MK.mods.find((x) => x.folder === folder);
    if (m && !m.enabled) await lab.gameModsEnable(folder, true);
    lab.engineRestart();
    toast(`<b>${esc(folder)}</b> is on: the Test Zoo restarts with it.`, 3500);
    setTab('scene');
  },
  // A new animal from one of the game's (Write Code > Game files: an
  // animal's .ai), as its own mod: then its species file, open to change
  mkSpecies: async (file) => {
    await modkitLoad();
    const a = await ask(
      `A new species from ${file}`,
      [
        { key: 'name', label: 'Its name (what the game calls it)', hint: 'e.g. Quagga' },
        {
          key: 'folder',
          label: 'Into (a mod there is: a pack of animals, or this one made again - its species file kept)',
          value: '',
          options: [{ value: '', label: 'A new mod' }, ...MK.mods.map((m) => ({ value: m.folder, label: m.name || m.folder }))],
        },
        { key: 'tint', label: 'Its colour: the art tinted towards this (#rrggbb; empty: as it is)', hint: '#b08850' },
        { key: 'strength', label: 'How much (0 to 1; the dark markings change least)', value: '0.6' },
        { key: 'author', label: 'Made by', value: store.get('mkAuthor', '') },
      ],
      'Make it',
    );
    if (!a || !a.name) return;
    store.set('mkAuthor', a.author);
    toast(`Making the ${esc(a.name)} from your game's ${esc(file)}…`, 6000);
    const r = await lab.modkitSpecies({ from: file, name: a.name, folder: a.folder, tint: a.tint, strength: a.strength, author: a.author });
    if (!r.ok) return toast(esc(r.message || 'It couldn’t be made.'), 6000);
    toast(
      r.kept
        ? `<b>${esc(a.name)}</b>: its art made again (${r.files} files); your species file kept.`
        : `<b>${esc(a.name)}</b>: ${a.folder ? 'added to ' + esc(r.folder) : 'a new mod'}, ${r.files} files. Its species file is open.`,
      4000,
    );
    // (an open copy of its species file: read again from the mod)
    const key = `mod:${r.folder}|content.ztd|${r.species}`;
    if (CodeEditor.isOpen(key) && !CodeEditor.isDirty(key)) {
      const e = await lab.modkitEntry(r.folder, 'content.ztd', r.species);
      if (e && e.ok) CodeEditor.reload(key, e.text);
    }
    await modkitLoad();
    MK.sel = r.folder;
    openModEntry(r.folder, 'content.ztd', r.species);
  },
  // One of the game's files, copied into a mod to change it there
  mkChangeInMod: async (file) => {
    await modkitLoad();
    const a = await ask(
      `Change ${file} in a mod`,
      [
        {
          key: 'folder',
          label: 'Which mod (its copy takes the game’s place while the mod is on)',
          value: '',
          options: [{ value: '', label: 'A new mod…' }, ...MK.mods.map((m) => ({ value: m.folder, label: m.name || m.folder }))],
        },
        { key: 'name', label: 'A new mod’s name', hint: 'e.g. Bigger restaurants' },
      ],
      'Copy it in',
    );
    if (!a) return;
    let folder = a.folder;
    if (!folder) {
      const n = await lab.modkitNew({ name: a.name || 'My changes', template: 'empty' });
      if (!n.ok) return toast(esc(n.message || 'The mod couldn’t be made.'), 4000);
      folder = n.folder;
    }
    const r = await lab.modkitCopyIn(folder, file);
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be copied in.'), 5000);
    MK.sel = folder;
    await openModEntry(folder, r.ztd, r.entry, r);
    toast(r.had ? `It was already in <b>${esc(folder)}</b>: its copy is open.` : `Copied into <b>${esc(folder)}</b>: change it and save.`, 3500);
  },
  mkNew: () => {
    MK.newOpen = !MK.newOpen;
    // (its form is in the mod picker: opened with it)
    if (MK.newOpen) ST.picker = true;
    renderModKit().then(() => $('mk-newname') && $('mk-newname').focus());
  },
  mkCreate: async () => {
    const name = ($('mk-newname') || {}).value || '';
    const template = ($('mk-template') || {}).value || 'empty';
    if (template === 'starter') toast('Making the starter kit from your game...', 4000);
    const r = await lab.modkitNew({ name, template });
    MK.newOpen = false;
    if (!r.ok) toast(esc(r.message || "It couldn't be made."), 5000);
    if (r.ok) {
      MK.sel = r.folder;
      stSetNode('start');
      ST.picker = false;
      stInvalidate();
      MK.log = [];
    }
    renderModKit();
  },
  // (a path given - dropped on the list, or from the Lab's MCP - else the picker)
  mkImport: async (file) => {
    const r = await lab.modkitImport(typeof file === 'string' && file ? file : undefined);
    if (r.cancelled) return;
    toast(esc(r.message || (r.ok ? 'Brought in.' : 'Not brought in.')));
    if (r.ok) {
      MK.sel = r.folder;
      stSetNode('files');
      ST.picker = false;
      MK.contents = null;
      stInvalidate();
      MK.log = [];
    }
    renderModKit();
  },
  mkOpen: (folder) => lab.modkitOpen(folder || ''),
  mkSave: async () => {
    const fields = {};
    document.querySelectorAll('.mkfield').forEach((el) => (fields[el.dataset.k] = el.value));
    if (!Object.keys(fields).length) return;
    await lab.modkitSave(MK.sel, fields);
    MK.dirty = false;
    ST.problems = null;
    toast('Saved.');
    renderModKit();
  },
  // Its picture: an image you pick, the Test Zoo's view, or none
  mkPicture: async (how) => {
    if (!MK.sel) return;
    let req = { op: how };
    if (how === 'zoo') {
      const c = $('canvas');
      if (!c || !c.width || S.engine.status !== 'connected') return toast('Open the Test Zoo first: its view becomes the picture.', 4000);
      req = { op: 'png', png: c.toDataURL('image/png').split(',')[1] };
    }
    const r = await lab.modkitPicture(MK.sel, req);
    if (!r.ok) return r.cancelled || toast(esc(r.error || 'It couldn’t be set.'), 4000);
    MK.picture = { folder: MK.sel, url: r.url || '' };
    ST.problems = null;
    toast(r.url ? 'Picture set: the Mod Menu shows it.' : 'Picture removed.', 2500);
    renderModKit();
  },
  mkToggle: async () => {
    const m = MK.mods.find((x) => x.folder === MK.sel);
    if (m) await lab.gameModsEnable(m.folder, !m.enabled);
    renderModKit();
  },
  // (the switch in the list: on or off without opening it)
  mkQuickToggle: async (folder) => {
    const m = MK.mods.find((x) => x.folder === folder);
    if (m) await lab.gameModsEnable(m.folder, !m.enabled);
    renderModKit();
  },
  mkTryZoo: async () => {
    const m = MK.mods.find((x) => x.folder === MK.sel);
    if (!m) return;
    if (!m.enabled) await lab.gameModsEnable(m.folder, true);
    lab.engineRestart();
    toast(`${esc(m.name || m.folder)} is on: the Test Zoo restarts with it.`, 3500);
    setTab('scene');
  },
  mkPlay: async () => {
    const m = MK.mods.find((x) => x.folder === MK.sel);
    if (!m) return;
    if (!m.enabled) await lab.gameModsEnable(m.folder, true);
    setTab('play');
    if (S.play.running) toast('It is on. The game in Play has it once it restarts.', 3500);
    else ACTIONS.playStart();
  },
  mkCheck: async () => {
    if (MK.dirty) await ACTIONS.mkSave();
    MK.check = { folder: MK.sel, ...(await lab.modkitCheck(MK.sel)) };
    renderModKit();
  },
  mkUpload: async () => {
    if (MK.busy) return;
    if (MK.dirty) await ACTIONS.mkSave();
    MK.busy = true;
    stSetNode('publish');
    MK.log = ['Uploading...'];
    renderModKit();
    const r = await lab.modkitUpload(MK.sel);
    MK.busy = false;
    if (r.check) MK.check = { folder: MK.sel, ...r.check };
    MK.log.push(r.message || (r.ok ? 'Uploaded.' : 'Not uploaded.'));
    MK.mine = null;
    renderModKit();
  },
  mkSignIn: async () => {
    const t = ($('mk-token') || {}).value || '';
    if (!t.trim()) return toast('Paste your CodeRook token first.');
    await lab.modkitToken(t);
    MK.mine = null;
    renderModKit();
  },
  mkSignOut: async () => {
    await lab.modkitToken('');
    MK.mine = null;
    renderModKit();
  },
  mkMine: async () => {
    MK.mine = await lab.modkitMine();
    if (S.tab === 'mods' && S.modsLeft === 'library') renderModKit();
  },
  // ("<id>!": already confirmed - the Lab's MCP)
  mkTakedown: async (arg) => {
    const sure = String(arg).endsWith('!');
    const id = sure ? String(arg).slice(0, -1) : arg;
    if (!sure && !confirm(`Take ${id} down from the mod repository? Players won't be offered it any more.`)) return;
    const r = await lab.modkitTakedown(id);
    toast(esc(r.message));
    MK.mine = null;
    renderModKit();
  },
});

lab.onModkit((s) => {
  MK.log.push(s.line);
  const el = $('mk-log');
  if (el) {
    el.style.display = '';
    el.textContent = MK.log.join('\n');
  }
});
