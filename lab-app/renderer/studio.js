// ZT1 Lab: the mod studio - Mods > a mod, as one workspace:
//  - the top bar: which mod (the picker: every mod, its switch, New mod,
//    Bring one in), its switch, saved or not, Test in the zoo, Play, Publish
//  - the tree (left): everything the mod holds, by kind - its animals,
//    buildings, scenery, fences and paths, staff, maps, the game's files it
//    changes - read by re/lab/studio.py; + Make something; About; Art; Files;
//    Publish
//  - the middle: the editor for what's picked (an animal's animations and the
//    animation editor - pixeled.js; the art maker - artmaker.js; the About,
//    Files and Publish pages - modkit.js)
//  - the Inspector (right): what's picked, as controls - an animal's or a
//    building's values from its file, saved into the mod as they change
//  - the dock (bottom): Problems (what's missing or wrong, each one a link to
//    where it's fixed) and the mod's log
// (loaded after modkit.js, artmaker.js and pixeled.js)

const ST = {
  tree: null, // {folder, items, maps, changed} (studio.py)
  insp: null, // the Inspector's file: {key, folder, ztd, entry, text, crlf}
  inspNote: '',
  picker: false,
  dock: store.get('stDock', true),
  dockTab: 'problems',
  problems: null, // {folder, list: [{text, node, level}]}
  showAll: false, // the Inspector's other values, open
};

// What's picked, per mod (kept for a Lab started again)
function stNode() {
  return MK.node || store.get('stNode:' + MK.sel, 'start');
}
function stSetNode(n) {
  MK.node = n;
  store.set('stNode:' + MK.sel, n);
}

// The kinds the tree groups items by (studio.py's kind_of)
const ST_KINDS = [
  ['animal', 'Animals'],
  ['building', 'Buildings'],
  ['scenery', 'Scenery'],
  ['foliage', 'Foliage'],
  ['fence', 'Fences'],
  ['path', 'Paths'],
  ['staff', 'Staff'],
  ['guest', 'Guests'],
  ['item', 'Items'],
  ['other', 'Other'],
];

// ------------------------------------------------------------- the values
// An entity's values the Inspector shows as controls, by kind: [key, label,
// what it is (num, money, float, flag)]; the rest are under "All its values"
const ST_FIELDS = {
  animal: [
    ['Price', [['cPurchaseCost', 'Price to adopt', 'money']]],
    ['Food and rest', [['cHungerThreshold', 'Gets hungry at'], ['cHungerIncrement', 'Hunger grows by'], ['cNeededFood', 'Food it needs'], ['cEnergyThreshold', 'Gets tired at'], ['cEnergyIncrement', 'Tiredness grows by'], ['cMaxEnergy', 'Most energy']]],
    ['Group and space', [['cSocial', 'Lives in a group', 'flag'], ['cNumberAnimalsMin', 'Fewest in an exhibit'], ['cNumberAnimalsMax', 'Most in an exhibit'], ['cHabitatSize', 'Space it needs'], ['cAnimalDensity', 'Space each one more needs'], ['cCrowd', 'Guests it can stand']]],
    ['Babies', [['cReproductionChance', 'Chance of a baby'], ['cReproductionInterval', 'Time between babies'], ['cBabyToAdult', 'Time to grow up'], ['cOffspring', 'Babies at a time']]],
    ['What it can do', [['cIsJumper', 'Jumps low fences', 'flag'], ['cIsClimber', 'Climbs fences', 'flag'], ['cClimbsCliffs', 'Climbs cliffs', 'flag'], ['cSwims', 'Swims', 'flag'], ['cFlies', 'Flies', 'flag'], ['cNeedShelter', 'Needs a shelter', 'flag'], ['cBashStrength', 'Breaks fences with']]],
    ['Its habitat', [['cTreePref', 'Trees it wants'], ['cRockPref', 'Rocks it wants'], ['cSpacePref', 'Open space it wants'], ['cElevationPref', 'Hills it wants'], ['cHabitatPreference', 'How much the habitat counts']]],
    ['Guests and health', [['cAttractiveness', 'How much guests like it'], ['cInitialHappiness', 'Happiness when it arrives'], ['cSickChance', 'Chance of falling sick'], ['cKeeperFrequency', 'Keeper visits it needs'], ['cTimeDeath', 'Lifespan'], ['cDeathChance', 'Chance of dying of old age']]],
  ],
  building: [
    ['Price and size', [['cPurchaseCost', 'Price to build', 'money'], ['cUpkeep', 'Upkeep', 'float'], ['cFootprintX', 'Footprint across'], ['cFootprintY', 'Footprint deep'], ['cHeight', 'Height']]],
    ['Guests', [['cCapacity', 'Guests inside at once'], ['cTimeInside', 'Time a guest stays'], ['cDefaultCost', 'What guests pay', 'float'], ['cLowCost', 'Low price', 'float'], ['cMedCost', 'Medium price', 'float'], ['cHighCost', 'High price', 'float']]],
    ['What it does for guests', [['cAdultChange', 'Adult happiness'], ['cChildChange', 'Child happiness'], ['cHungerChange', 'Hunger'], ['cThirstChange', 'Thirst'], ['cBathroomChange', 'Restroom need'], ['cEnergyChange', 'Energy']]],
  ],
  scenery: [['Price and size', [['cPurchaseCost', 'Price', 'money'], ['cFootprintX', 'Footprint across'], ['cFootprintY', 'Footprint deep'], ['cHeight', 'Height'], ['cSelectable', 'Can be picked up', 'flag'], ['cDeletable', 'Can be removed', 'flag']]]],
};
ST_FIELDS.foliage = ST_FIELDS.scenery;
// (an animal's kind adds its own: a marine animal's water - or any animal's
// that swims under it - and a dinosaur's egg and fierceness)
const ST_KIND_FIELDS = {
  water: [
    'Water',
    [
      ['cUnderwater', 'Lives under water', 'flag'],
      ['cOnlyUnderwater', 'Never leaves the water', 'flag'],
      ['cDepth', 'Water deep enough for it'],
      ['cDepthMin', 'Best depth: from'],
      ['cDepthMax', 'Best depth: to'],
      ['cAvoidEdges', 'Keeps off the tank walls'],
      ['cMurkyWaterChange', 'Murky water: happiness'],
      ['cVeryMurkyWaterChange', 'Very murky: happiness'],
      ['cExtremelyMurkyWaterChange', 'Filthy water: happiness'],
      ['cSalinityChange', 'Wrong salt: happiness'],
      ['cSalinityHealthChange', 'Wrong salt: health'],
    ],
  ],
  dinosaur: [
    'Dinosaur',
    [
      ['cLaysEggs', 'Comes as an egg', 'flag'],
      ['cTimeToHatch', 'Time to hatch'],
      ['cEatsEggs', 'Eats eggs', 'flag'],
      ['cIsManEater', 'Eats guests', 'flag'],
      ['cCrushesFences', 'Breaks fences down'],
      ['cTall', 'Tall', 'flag'],
      ['cBabiesAttack', 'Its babies attack too', 'flag'],
    ],
  ],
};
ST_FIELDS.fence = [['Price and strength', [['cPurchaseCost', 'Price a piece', 'money'], ['cStrength', 'Strength'], ['cLife', 'Lasts'], ['cDecayedLife', 'Lasts once worn'], ['cHeight', 'Height']]]];
ST_FIELDS.path = [['Price', [['cPurchaseCost', 'Price a tile', 'money']]]];
ST_FIELDS.staff = [['Pay', [['cPurchaseCost', 'Hiring cost', 'money'], ['cSalary', 'Pay']]]];

// An .ai's sections: [{name (lower case), start, end}] by line
function stSections(lines) {
  const out = [];
  lines.forEach((ln, i) => {
    const m = /^\s*\[([^\]]+)\]/.exec(ln);
    if (m) {
      if (out.length) out[out.length - 1].end = i;
      out.push({ name: m[1].trim().toLowerCase().replace(/\\/g, '/'), start: i, end: lines.length });
    }
  });
  return out;
}
function stKeyAt(ln) {
  const m = /^\s*([A-Za-z_][\w]*)\s*=\s*([^;\r]*)/.exec(ln);
  return m ? { key: m[1], value: m[2].trim() } : null;
}
// The values of the sections that hold an entity's characteristics, in the
// order the game reads them: the adult's (m/) first for an animal
function stValues(text, kind) {
  const lines = text.split('\n');
  const secs = stSections(lines);
  const prefix = kind === 'fence' || kind === 'tankwall' ? 'f' : 'm';
  const pick = (secs.some((s) => s.name.startsWith(prefix + '/characteristics')) ? [prefix + '/characteristics/integers', prefix + '/characteristics/floats', prefix + '/characteristics/strings'] : []).concat([
    'characteristics/integers',
    'characteristics/floats',
    'characteristics/strings',
  ]);
  const vals = {};
  for (const name of pick) {
    const s = secs.find((x) => x.name === name);
    if (!s) continue;
    for (let i = s.start + 1; i < s.end; i++) {
      const kv = stKeyAt(lines[i]);
      if (kv && !vals[kv.key.toLowerCase()]) vals[kv.key.toLowerCase()] = { key: kv.key, value: kv.value, sec: s.name, line: i };
    }
  }
  return vals;
}
// key = value set in the text: on its line where it has one, else added to
// the section the kind keeps it in
function stSetValue(text, kind, key, value, isFloat) {
  const nl = text.includes('\r\n') ? '\r\n' : '\n';
  const lines = text.split(/\r?\n/);
  const v = stValues(lines.join('\n'), kind)[key.toLowerCase()];
  if (v) {
    lines[v.line] = lines[v.line].replace(/=\s*[^;\r]*/, '= ' + value + (/;/.test(lines[v.line]) ? ' ' : ''));
    return lines.join(nl);
  }
  const secs = stSections(lines);
  const want = (kind === 'animal' && secs.some((s) => s.name.startsWith('m/')) ? 'm/' : '') + (isFloat ? 'characteristics/floats' : 'characteristics/integers');
  const s = secs.find((x) => x.name === want);
  if (s) lines.splice(s.start + 1, 0, `${key} = ${value}`);
  else lines.push('', `[${want.replace(/(^|\/)(\w)/g, (_, a, b) => a + b.toUpperCase())}]`, `${key} = ${value}`);
  return lines.join(nl);
}

// --------------------------------------------------------------- loading
async function stLoadTree(folder) {
  if (ST.tree && ST.tree.folder === folder) return;
  const r = await lab.studioTree(folder);
  ST.tree = { folder, ...(r.ok ? r : { items: [], maps: [], changed: [], error: r.error }) };
  ST.problems = null;
}
// (the mod's own animals, for the tree: the animal maker's list)
async function stLoadAnimals(folder) {
  if (AN.folder !== folder) Object.assign(AN, { folder, list: null, type: null, info: null, ed: null, error: '' });
  if (!AN.list) await anLoadList();
}
// The game's item list (names and icons), from the Test Zoo's engine
async function stCatalog() {
  if (SC.catalog || S.engine.status !== 'connected') return SC.catalog || [];
  SC.catalogAsked = true;
  lab.engineSend({ cmd: 'catalog' });
  for (let i = 0; i < 30 && !SC.catalog; i++) await new Promise((r) => setTimeout(r, 100));
  return SC.catalog || [];
}
function stItemName(it) {
  const c = itemOf(it.file.toLowerCase());
  return it.name || (c && c.name) || it.file.split('/').pop().replace(/\.\w+$/, '');
}

// What's missing or wrong, each with where it's put right
async function stProblems(m) {
  if (ST.problems && ST.problems.folder === m.folder) return ST.problems.list;
  const list = [];
  if (!(MK.picture && MK.picture.folder === m.folder && MK.picture.url)) list.push({ level: 'hint', text: 'No picture: the Mod Menu shows an empty box beside it.', node: 'about' });
  if (!(m.about || '').trim()) list.push({ level: 'hint', text: 'No About: players read it in the Mod Menu.', node: 'about' });
  const c = MK.contents && MK.contents.folder === m.folder ? MK.contents : null;
  if (c && c.ztds) for (const z of c.ztds) if (z.bad) list.push({ level: 'bad', text: `${z.file} isn't a .ztd the game can read.`, node: 'files' });
  for (const a of AN.list || []) {
    const r = await lab.animalDo(m.folder, { op: 'slots', type: a.type });
    if (!r.ok) {
      list.push({ level: 'bad', text: `${a.name}: ${r.error || "its species file doesn't read"}`, node: 'animal:' + a.type });
      continue;
    }
    const miss = (r.subs.m || []).filter((s) => s.status === 'missing').map((s) => s.slot);
    if (miss.length) list.push({ level: 'bad', text: `${a.name}: ${miss.length} animation${miss.length > 1 ? 's' : ''} play nothing (${miss.slice(0, 4).join(', ')}${miss.length > 4 ? '…' : ''}). Draw or link them.`, node: 'animal:' + a.type });
    // (what its kind needs)
    if (r.kind === 'dinosaur' && !r.egg)
      list.push({ level: 'bad', text: `${a.name}: a dinosaur hatches from an egg - it needs egg_idle, egg_hatch and egg_break animations (draw its own egg, or link a dinosaur's).`, node: 'animal:' + a.type });
    if (r.kind === 'aqua' && !r.underwater)
      list.push({ level: 'bad', text: `${a.name}: a marine animal, but its behaviour has nothing for under water - start it from a marine animal (its swimming and tank behaviour come with it).`, node: 'animal:' + a.type });
    const drawn = (r.subs.m || []).filter((s) => s.status === 'drawn').length;
    if (!drawn) list.push({ level: 'hint', text: `${a.name}: every animation is still linked to another animal's. Draw its own, starting with walk.`, node: 'animal:' + a.type });
  }
  for (const mp of (ST.tree && ST.tree.maps) || []) if (!mp.about) list.push({ level: 'hint', text: `${mp.title}: no description for the map list.`, node: 'map:' + mp.ztd + '|' + mp.file });
  ST.problems = { folder: m.folder, list };
  return list;
}

// ------------------------------------------------------------- rendering
async function renderStudio(gen, m, c) {
  if (m) {
    // (a mod not read yet: its frame shown straight away, filled in once
    // its contents are read)
    if (!(ST.tree && ST.tree.folder === m.folder) || AN.folder !== m.folder || !AN.list)
      $('v-mods').innerHTML = `<div class="st">${stTopHtml(m)}<div class="stbody"><div class="sttree">${modkitTabs()}<div class="muted" style="padding:10px 8px">Reading it…</div></div><div class="stcenter"></div></div></div>`;
    await Promise.all([stLoadTree(m.folder), stLoadAnimals(m.folder), ibLoad(m.folder)]);
    if (gen !== MK.gen) return;
  }
  let node = m ? stNode() : 'start';
  // (a node that isn't there - an item removed, or the mod still being read
  // when it was picked - shows the start, the pick kept for when it is)
  if (m && !stNodeExists(node)) node = 'start';
  if (node.startsWith('animal:') && (AN.item || AN.type !== node.slice(7))) {
    AN.item = null;
    AN_DIRS.splice(0, AN_DIRS.length, 'N', 'NE', 'E', 'SE', 'S');
    AN.type = node.slice(7);
    AN.info = null;
    AN.ed = null;
  }
  if (node.startsWith('animal:') && !AN.info) await anLoadInfo();
  if (node.startsWith('built:')) await ibSelect(node.slice(6));
  if (gen !== MK.gen) return;
  const editing = (node.startsWith('animal:') || node.startsWith('built:')) && AN.ed;
  $('v-mods').innerHTML = `<div class="st${editing ? ' noinsp' : ''}">
      ${stTopHtml(m)}
      ${ST.picker ? stPickerHtml() : ''}
      <div class="stbody">
        <div class="sttree">${modkitTabs()}${m ? stTreeHtml(m, node) : ''}</div>
        <div class="stcenter">
          <div class="stmain" id="st-main">${m ? stMainHtml(m, c, node) : mkEmpty()}</div>
          ${m ? stDockHtml() : ''}
        </div>
        ${m && !editing ? `<div class="stinsp" id="st-insp">${stInspHtml(m, c, node)}</div>` : ''}
      </div></div>`;
  stMounted(m, c, node, gen);
}

function stNodeExists(node) {
  if (['start', 'about', 'art', 'files', 'publish'].includes(node)) return true;
  if (node.startsWith('animal:')) return (AN.list || []).some((a) => a.type === node.slice(7));
  if (node.startsWith('built:')) return IB.items.some(i => i.key === node.slice(6));
  if (node.startsWith('item:')) return ((ST.tree && ST.tree.items) || []).some((i) => 'item:' + i.ztd + '|' + i.file === node);
  if (node.startsWith('map:')) return ((ST.tree && ST.tree.maps) || []).some((i) => 'map:' + i.ztd + '|' + i.file === node);
  return false;
}

function stTopHtml(m) {
  const pic = m && MK.picture && MK.picture.folder === m.folder && MK.picture.url;
  const dirty = MK.dirty || (AN.ed && AN.ed.dirty);
  const busy = MK.busy ? ' disabled' : '';
  return `<div class="sttop">
      <div class="stmod" data-act="stPicker" title="Pick another mod, or make one">${
        m ? (pic ? `<img class="sticon" src="${pic}" alt="">` : `<div class="sticon">${esc((m.name || m.folder).trim().charAt(0).toUpperCase() || '?')}</div>`) : '<div class="sticon">+</div>'
      }<div class="stmodtx"><div class="nm">${esc(m ? m.name || m.folder : 'No mod picked')}</div><div class="meta">${esc(m ? [m.version && 'v' + m.version, m.author && 'by ' + m.author].filter(Boolean).join(' · ') || m.folder : 'Pick or make one')}</div></div><span class="stcaret">▾</span></div>
      ${
        m
          ? `<div class="mkonoff${m.enabled ? ' on' : ''}" data-act="mkToggle" title="Switched on, it's in the Test Zoo and in Play when they next start"><span class="mktoggle${m.enabled ? ' on' : ''}"></span>${m.enabled ? 'On' : 'Off'}</div>
      <span class="stsaved${dirty ? ' no' : ''}">${dirty ? 'Not saved' : 'All saved'}</span>
      <div class="spacer"></div>
      <div class="btn small${busy}" data-act="mkTryZoo" title="Switch it on and restart the Test Zoo with it">▶ Test in the zoo</div>
      <div class="btn small${busy}" data-act="mkPlay" title="Switch it on and start the game with it in Play">Play with it</div>
      <div class="btn small primary" data-act="stPick" data-arg="publish">Publish</div>
      <div class="btn small ghost" data-act="mkOpen" data-arg="${esc(m.folder)}" title="Its folder">Folder</div>`
          : '<div class="spacer"></div>'
      }
    </div>`;
}

function stPickerHtml() {
  const q = MK.filter.trim().toLowerCase();
  const shown = MK.mods.filter((x) => !q || `${x.name || ''} ${x.folder} ${x.author || ''}`.toLowerCase().includes(q));
  const list = shown
    .map(
      (x) => `<div class="item mkitem${x.folder === MK.sel ? ' sel' : ''}" data-act="mkPick" data-arg="${esc(x.folder)}" title="${esc((x.name || x.folder) + (x.about ? '\n' + x.about : ''))}"><div class="mkrow">${
        x.pictureThumb ? `<img class="mkthumb" src="${x.pictureThumb}" alt="">` : ''
      }<div class="name">${esc(x.name || x.folder)}</div><span class="mktoggle${x.enabled ? ' on' : ''}" data-act="mkQuickToggle" data-arg="${esc(x.folder)}" title="${
        x.enabled ? 'On. Click to switch off.' : 'Off. Click to switch on.'
      }"></span></div><div class="sub">${esc([x.version && 'v' + x.version, x.author, x.imported && 'brought in'].filter(Boolean).join(' · ') || x.folder)}</div></div>`,
    )
    .join('');
  return `<div class="stpicker">
      <input id="mk-filter" placeholder="Find a mod" value="${esc(MK.filter)}"/>
      <div class="mklist">${list || `<div class="muted" style="padding:8px 10px;font-size:12px">${MK.mods.length ? 'No mod matches.' : 'No mods yet.'}</div>`}</div>
      ${
        MK.newOpen
          ? `<div class="mknew"><input id="mk-newname" placeholder="Its name"/><select id="mk-template"><option value="empty">Empty: build it in the studio</option><option value="starter">Starter kit (one of everything)</option><option value="ztd">Game files (a .ztd)</option><option value="shader">A shader (a look for the art)</option></select><div><div class="btn small primary" data-act="mkCreate">Make it</div> <div class="btn small ghost" data-act="mkNew">Cancel</div></div></div>`
          : ''
      }
      <div class="mkrailfoot"><div class="btn small primary" data-act="mkNew">New mod</div><div class="btn small" data-act="mkImport" title="An old Zoo Tycoon download (.ztd), a .zip, or a mod's folder. Dropping one on the page works too.">Bring one in</div><div class="btn small ghost" data-act="mkOpen" data-arg="" title="${esc(
        MK.folder,
      )}">Mods folder</div><div class="spacer"></div><div class="btn small ghost" data-act="stPicker">Close</div></div>
    </div>`;
}

function stTreeHtml(m, node) {
  const row = (n, label, extra = '', cls = '') =>
    `<div class="stnode${n === node ? ' sel' : ''}${cls ? ' ' + cls : ''}" data-act="stPick" data-arg="${esc(n)}" title="${esc(label)}"><span class="stlabel">${label}</span>${extra}</div>`;
  const tag = (t, cls) => `<span class="sttag ${cls || ''}">${t}</span>`;
  const head = (label, n, add) =>
    `<div class="sthead"><span>${label}</span>${n ? `<span class="dim">${n}</span>` : ''}<span class="spacer"></span>${add ? `<span class="stadd" data-act="${add}" title="Add one">+</span>` : ''}</div>`;
  const items = ((ST.tree && ST.tree.items) || []).filter(i => !IB.items.some(b => b.file.toLowerCase() === i.file.toLowerCase()));
  let h = `<div class="stscroll">${row('start', '＋ Make something', '', 'stmake')}${row('about', 'About this mod')}`;
  // Animals: its own (the animal maker's) and the game's it changes
  const own = AN.list || [];
  const ownFiles = new Set(own.map((a) => a.species.toLowerCase()));
  const gameAnimals = items.filter((i) => i.kind === 'animal' && !ownFiles.has(i.file.toLowerCase()));
  h += head('Animals', own.length + gameAnimals.length || '', 'stNewAnimal');
  h += own.map((a) => row('animal:' + a.type, esc(a.name), tag('new', 'new'))).join('');
  h += gameAnimals.map((i) => row('item:' + i.ztd + '|' + i.file, esc(stItemName(i)), tag('changed'))).join('');
  if (IB.items.length) {
    h += head('Created items', IB.items.length, 'ibNew');
    h += IB.items.map(i => row('built:' + i.key, esc(i.name), tag(i.mode === 'change' || i.kind === 'guest' ? 'changed' : 'new', i.mode === 'new' && i.kind !== 'guest' ? 'new' : ''))).join('');
  }
  for (const [k, label] of ST_KINDS.slice(1)) {
    const these = items.filter((i) => i.kind === k);
    if (!these.length && k !== 'building' && k !== 'scenery') continue;
    h += head(label, these.length || '', k === 'building' || k === 'scenery' ? 'stChangeItem' : '');
    h += these.map((i) => row('item:' + i.ztd + '|' + i.file, esc(stItemName(i)), i.changes ? tag('changed') : tag('new', 'new'))).join('');
  }
  const maps = (ST.tree && ST.tree.maps) || [];
  h += head('Maps', maps.length || '', 'stNewMap');
  h += maps.map((mp) => row('map:' + mp.ztd + '|' + mp.file, esc(mp.title), mp.blank ? tag('blank') : '')).join('');
  h += head('Tools');
  h += row('art', 'Paint and recolour art') + row('files', 'Its files', ST.tree && ST.tree.changed.length ? `<span class="dim">${ST.tree.changed.length} changed</span>` : '') + row('publish', 'Publish');
  return h + '</div>';
}

function stMainHtml(m, c, node) {
  if (node === 'start') return stStartHtml(m);
  if (node === 'about') return `<div class="mkbody">${mkOverviewPage(m)}</div>`;
  if (node === 'files') return `<div class="mkbody">${mkContentsPage(m, c)}</div>`;
  if (node === 'publish') return `<div class="mkbody">${mkPublishPage(m)}</div>`;
  if (node === 'art') return `<div class="mkbody amhost"><div class="am" id="am"><div class="muted" style="padding:20px">Reading its art…</div></div></div>`;
  if (node.startsWith('built:')) return `<div class="mkbody anhost"><div class="an" id="an">${anHtml()}</div></div>`;
  if (node.startsWith('animal:') || (node.startsWith('item:') && (stItemOf(node) || {}).kind === 'animal')) return stAnimalHtml(node);
  if (node.startsWith('item:')) return stItemHtml(node);
  if (node.startsWith('map:')) return stMapHtml(node);
  return '';
}

function stStartHtml(m) {
  const card = (act, icon, title, about) =>
    `<div class="stcard" data-act="${act}"><div class="stcardic">${icon}</div><div class="stcardtt">${title}</div><div class="stcardab">${about}</div></div>`;
  const own = (AN.list || []).length + ((ST.tree && ST.tree.items.length) || 0) + ((ST.tree && ST.tree.maps.length) || 0);
  return `<div class="ststart">
      <div class="ststartt">Make something for ${esc(m.name || m.folder)}</div>
      <div class="muted">Pick what to make. Each one starts from something that already works in the game, so it plays from the first step, and you change it from there.</div>
      <div class="stcards">
        ${card('stNewAnimal', ic('paw'), 'New animal', 'A new species: a name and an animal it behaves like, then its own look, one animation at a time.')}
        ${card('ibNew', ic('building'), 'Build an item', 'Buildings, scenery, filters, fences, staff and guest appearances. Create a separate item or customize an existing one; draw, import or remake its art.')}
        ${card('stNewMap', ic('map'), 'New map', 'A freeform map from blank grass: its size, the ground, paths, the zoo wall and where the entrance goes.')}
        ${card('stChangeItem', ic('pencil'), 'Change a game item', "Any of the game's animals, buildings, scenery or fences, copied in to change its price, values, name or look.")}
      </div>
      ${own ? '' : '<div class="muted" style="font-size:12px">Everything you make shows in the tree on the left. Pick one there to edit it; the Inspector on the right holds its values.</div>'}
    </div>`;
}

// An animal: its animations (its own: the animal maker) and its needs
function stAnimalHtml(node) {
  const own = node.startsWith('animal:');
  const tab = own ? ST.animalTab || 'anims' : 'needs';
  const t = (k, label) => `<div class="${tab === k ? 'on' : ''}" data-act="stAnimalTab" data-arg="${k}">${label}</div>`;
  const tabs = `<div class="sttabs">${own ? t('anims', 'Animations') : ''}${t('needs', 'Needs')}${own ? t('pics', 'Pictures') : ''}${
    own ? '' : `<span class="spacer"></span><span class="dim" style="font-size:12px">A game animal this mod changes: its animations are the game's (Paint and recolour art changes them)</span>`
  }</div>`;
  // (drawing - an animation or a picture: the editor has the room)
  if (own && AN.ed) return `<div class="mkbody anhost"><div class="an" id="an">${anHtml()}</div></div>`;
  if (tab === 'anims') return tabs + `<div class="mkbody anhost"><div class="an" id="an">${anHtml()}</div></div>`;
  if (tab === 'pics') return tabs + `<div class="mkbody"><div id="pics">${picsHtml()}</div></div>`;
  return tabs + `<div class="mkbody"><div id="ne">${needsHtml()}</div></div>`;
}

function stItemOf(node) {
  return ((ST.tree && ST.tree.items) || []).find((i) => 'item:' + i.ztd + '|' + i.file === node);
}
function stItemHtml(node) {
  const it = stItemOf(node);
  if (!it) return '';
  const cat = itemOf(it.file.toLowerCase());
  const kindName = (ST_KINDS.find((k) => k[0] === it.kind) || [0, 'Item'])[1].replace(/s$/, '');
  return `<div class="stitem">
      <div class="stitemhead"><div class="stitempic">${cat && cat.icon ? iconImg(cat.icon, 'stbigicon') : '<span class="noicon"></span>'}</div>
        <div><div class="ststartt">${esc(stItemName(it))}</div><div class="dim mono">${esc(it.file)} · ${esc(it.ztd)}</div>
        <div class="muted" style="margin-top:6px">${kindName}. ${
          it.changes ? 'Your copy of one of the game’s: while the mod is on, the game uses it in place of its own.' : 'New: the game has nothing at this path.'
        }</div></div></div>
      <div class="strow"><div class="btn small" data-act="stOpenFile" data-arg="${esc(it.ztd + '|' + it.file)}" title="The whole file, in Write Code">Open its file</div><div class="btn small ghost" data-act="stPick" data-arg="art" title="Its palette and frames, if the mod has its art">Paint its art</div></div>
      <div class="muted" style="font-size:12px">Its values are in the Inspector on the right: change one and it's saved into the mod. Test in the zoo restarts the Test Zoo with the change.</div>
    </div>`;
}
function stMapOf(node) {
  return ((ST.tree && ST.tree.maps) || []).find((i) => 'map:' + i.ztd + '|' + i.file === node);
}
function stMapHtml(node) {
  const mp = stMapOf(node);
  if (!mp) return '';
  return `<div class="stitem">
      <div class="stitemhead"><div class="stmappic" id="st-mappic"><span class="muted">…</span></div>
        <div><div class="ststartt">${esc(mp.title)}</div><div class="dim mono">${esc(mp.file)}${mp.size ? ' · ' + esc(mp.size) : ''}${mp.blank ? ' · from a blank map, ' + esc(mp.blank.replace(' ', ' x ')) : ''}</div>
        <div class="muted" style="margin-top:6px;max-width:60ch">${esc(mp.about || 'No description yet.')}</div></div></div>
      <div class="strow"><div class="btn small primary" data-act="stEditMap" data-arg="${esc(mp.file)}" title="Load it in the Test Zoo, editing on (the mod has to be on)">Edit it in the Test Zoo</div><div class="btn small" data-act="stOpenFile" data-arg="${esc(mp.ztd + '|' + mp.file)}">Open its .scn</div></div>
    </div>`;
}

function stDockHtml() {
  const probs = ST.problems && ST.problems.folder === MK.sel ? ST.problems.list : null;
  const bad = probs ? probs.filter((p) => p.level === 'bad').length : 0;
  const tab = (k, label) => `<div class="${ST.dockTab === k ? 'on' : ''}" data-act="stDockTab" data-arg="${k}">${label}</div>`;
  const body =
    ST.dockTab === 'log'
      ? `<div class="code" id="mk-log" style="font-size:12px;white-space:pre-wrap">${esc(MK.log.join('\n') || 'Nothing yet.')}</div>`
      : probs === null
        ? '<div class="muted">Checking it…</div>'
        : probs.length
          ? probs.map((p) => `<div class="stprob ${p.level === 'bad' ? 'bad' : 'hint'}" data-act="stPick" data-arg="${esc(p.node)}"><span class="stdot"></span>${esc(p.text)}</div>`).join('')
          : '<div class="green">Nothing missing.</div>';
  return `<div class="stdock${ST.dock ? '' : ' shut'}">
      <div class="stdockbar"><div class="seg mini">${tab('problems', `Problems${probs ? ` <span class="${bad ? 'red' : 'dim'}">${probs.length}</span>` : ''}`)}${tab('log', 'Log')}</div><div class="spacer"></div><div class="btn small ghost" data-act="stDock">${ST.dock ? '▾' : '▴'}</div></div>
      ${ST.dock ? `<div class="stdockbody" id="st-dock">${body}</div>` : ''}
    </div>`;
}

// The Inspector: the mod's own details, or what's picked as controls
function stInspHtml(m, c, node) {
  const file = stInspFile(node);
  if (!file) {
    const kv = (k, v) => `<div class="stkv"><span>${k}</span><span>${v}</span></div>`;
    return `<div class="stinsph">This mod</div>
      ${kv('Name', esc(m.name || m.folder))}${kv('Version', esc(m.version || '-'))}${kv('Author', esc(m.author || '-'))}${kv('Id', `<span class="mono">${esc(m.id || '-')}</span>`)}
      ${c ? kv('Files', `${c.files} · ${mkSize(c.bytes)}`) : ''}
      ${kv('Animals', (AN.list || []).length + ((ST.tree && ST.tree.items.filter((i) => i.kind === 'animal' && i.changes).length) || 0))}
      ${kv('Other items', (ST.tree && ST.tree.items.filter((i) => i.kind !== 'animal').length) || 0)}
      ${kv('Maps', (ST.tree && ST.tree.maps.length) || 0)}
      <div class="muted" style="font-size:12px;margin-top:10px">Pick an animal, building or map in the tree to see its values here.</div>`;
  }
  if (!ST.insp || ST.insp.key !== file.key) return '<div class="stinsph">Inspector</div><div class="muted">Reading it…</div>';
  if (ST.insp.error) return `<div class="stinsph">Inspector</div><div class="red">${esc(ST.insp.error)}</div>`;
  const vals = stValues(ST.insp.text, file.kind);
  let groups = ST_FIELDS[file.kind] || [];
  if (file.kind === 'animal') {
    const mem = ((/\[member\]([\s\S]*?)(\n\s*\[|$)/i.exec(ST.insp.text) || [])[1] || '').toLowerCase();
    if (/^\s*aqua\s*$/m.test(mem) || vals.cunderwater) groups = groups.concat([ST_KIND_FIELDS.water]);
    if (/^\s*dinosaur\s*$/m.test(mem)) groups = groups.concat([ST_KIND_FIELDS.dinosaur]);
  }
  const shown = new Set();
  const ctl = ([key, label, how]) => {
    const v = vals[key.toLowerCase()];
    shown.add(key.toLowerCase());
    const val = v ? v.value : '';
    const id = 'st-f-' + key;
    if (how === 'flag')
      return `<label class="stfield flag" title="${esc(key)}"><span>${label}</span><input type="checkbox" class="stval" id="${id}" data-k="${esc(key)}" data-how="flag"${+val ? ' checked' : ''}></label>`;
    return `<label class="stfield" title="${esc(key)}${v ? '' : ' (not in its file: the game uses its default)'}"><span>${label}</span><span class="stin">${how === 'money' ? '<i>$</i>' : ''}<input class="stval${v ? '' : ' unset'}" id="${id}" data-k="${esc(
      key,
    )}" data-how="${how || 'num'}" value="${esc(val)}" placeholder="default" inputmode="decimal"></span></label>`;
  };
  let h = `<div class="stinsph">${esc(file.title)} <span class="dim" id="st-inspnote">${esc(ST.inspNote)}</span></div>`;
  for (const [g, fields] of groups) h += `<div class="stgroup"><div class="stgl">${g}</div>${fields.map(ctl).join('')}</div>`;
  const rest = Object.values(vals).filter((v) => !shown.has(v.key.toLowerCase()));
  if (rest.length)
    h += `<div class="stgroup"><div class="stgl clickable" data-act="stShowAll">${ST.showAll ? '▾' : '▸'} All its values <span class="dim">${rest.length}</span></div>${
      ST.showAll
        ? rest.map((v) => `<label class="stfield" title="${esc(v.sec)}"><span class="mono">${esc(v.key)}</span><span class="stin"><input class="stval" data-k="${esc(v.key)}" data-how="raw" value="${esc(v.value)}"></span></label>`).join('')
        : ''
    }</div>`;
  return h + `<div class="muted" style="font-size:11.5px;margin-top:8px">Saved into the mod as you change them. <span class="blue clickable" data-act="stOpenFile" data-arg="${esc(file.ztd + '|' + file.entry)}">Its whole file ›</span></div>`;
}
// The file the Inspector edits for a node
function stInspFile(node) {
  if (node.startsWith('built:')) {
    const i = IB.items.find(i => i.key === node.slice(6));
    return i && !['guest', 'ground'].includes(i.kind) && { key: 'content.ztd|' + i.file, ztd: 'content.ztd', entry: i.file, kind: i.kind, title: i.name };
  }
  if (node.startsWith('animal:')) {
    const a = (AN.list || []).find((x) => x.type === node.slice(7));
    return a && { key: 'content.ztd|' + a.species, ztd: 'content.ztd', entry: a.species, kind: 'animal', title: a.name };
  }
  if (node.startsWith('item:')) {
    const it = stItemOf(node);
    return it && { key: it.ztd + '|' + it.file, ztd: it.ztd, entry: it.file, kind: it.kind, title: stItemName(it) };
  }
  return null;
}

function stMounted(m, c, node, gen) {
  const f = $('mk-filter');
  if (f) {
    f.oninput = () => {
      MK.filter = f.value;
      clearTimeout(MK.filterT);
      MK.filterT = setTimeout(
        () =>
          renderModKit().then(() => {
            const g = $('mk-filter');
            if (g) {
              g.focus();
              g.setSelectionRange(g.value.length, g.value.length);
            }
          }),
        150,
      );
    };
  }
  document.querySelectorAll('.mkfield').forEach(
    (el) =>
      (el.oninput = () => {
        MK.dirty = true;
        const s = $('mk-dirty');
        if (s) s.textContent = 'Not saved';
      }),
  );
  const page = $('v-mods');
  page.ondragover = (ev) => ev.preventDefault();
  page.ondrop = (ev) => {
    ev.preventDefault();
    const file = ev.dataTransfer && ev.dataTransfer.files && ev.dataTransfer.files[0];
    const p = file && (lab.pathForFile ? lab.pathForFile(file) : file.path);
    if (p) ACTIONS.mkImport(p);
  };
  if (!m) return;
  // (the game's item list - names, icons - asked for once; the page drawn
  // again with it)
  if (!SC.catalog && S.engine.status === 'connected' && !ST.askedCatalog) {
    ST.askedCatalog = true;
    stCatalog().then((cat) => cat.length && gen === MK.gen && renderModKit());
  }
  if (node === 'publish' && MK.signedIn && MK.mine === null) ACTIONS.mkMine();
  if (node.startsWith('animal:') && ((ST.animalTab || 'anims') === 'anims' || AN.ed)) anMounted();
  if (node.startsWith('built:')) anMounted();
  // (its pictures: read, then each drawn as the game shows it)
  if (node.startsWith('animal:') && ST.animalTab === 'pics' && !AN.ed)
    picsLoad().then(() => {
      const el = $('pics');
      if (el && AN.pics && AN.pics.type === AN.type) {
        el.innerHTML = picsHtml();
        iconButtons(el);
        picsShow();
      }
    });
  // (an animal's needs: read, then shown)
  if ((node.startsWith('animal:') && ST.animalTab === 'needs') || (node.startsWith('item:') && (stItemOf(node) || {}).kind === 'animal')) {
    if (NE.data && NE.key === m.folder + '|' + node) needsMounted();
    else neLoad(m, node).then(() => NE.key === m.folder + '|' + node && neRender());
  }
  if (node === 'art' && $('am'))
    artPage(m).then((html) => {
      const el = $('am');
      if (el && html && gen === MK.gen) {
        el.outerHTML = html;
        artMounted();
      }
    });
  if (node.startsWith('map:')) stMapPicture(node, gen);
  // (the Inspector's file, read once; its controls saved as they change)
  const file = stInspFile(node);
  if (file && (!ST.insp || ST.insp.key !== file.key || ST.insp.folder !== m.folder))
    lab.modkitEntry(m.folder, file.ztd, file.entry).then((r) => {
      if (gen !== MK.gen) return;
      ST.insp = { key: file.key, folder: m.folder, ztd: file.ztd, entry: file.entry, text: r && r.ok ? r.text : '', crlf: r && r.crlf, error: r && r.ok ? '' : (r && r.error) || "It couldn't be read." };
      ST.inspNote = '';
      stInspRender(m, c, node);
    });
  stInspWire();
  // (the problems: worked out after the page shows - an animal each)
  if (!ST.problems || ST.problems.folder !== m.folder)
    stProblems(m).then(() => {
      if (gen !== MK.gen || ST.dockTab !== 'problems') return;
      const d = document.querySelector('.stdock');
      if (d) d.outerHTML = stDockHtml();
    });
}
function stInspRender(m, c, node) {
  const el = $('st-insp');
  if (!el) return;
  el.innerHTML = stInspHtml(m, c, node);
  stInspWire();
}
function stInspWire() {
  document.querySelectorAll('.stval').forEach((el) => (el.onchange = () => stInspSet(el)));
}
async function stInspSet(el) {
  if (AN.busy) return toast('Wait for the animal’s current operation to finish.', 3000);
  const I = ST.insp;
  if (!I || I.error) return;
  const node = stNode();
  const file = stInspFile(node);
  if (!file || file.key !== I.key) return;
  const how = el.dataset.how;
  let v = how === 'flag' ? (el.checked ? '1' : '0') : el.value.trim();
  if (how !== 'raw' && how !== 'flag') {
    if (v === '') return;
    if (!/^-?\d+(\.\d+)?$/.test(v)) {
      toast('A number, please.', 2000);
      return stInspRender(MK.mods.find((x) => x.folder === MK.sel), MK.contents, node);
    }
    if (how !== 'float') v = String(Math.round(+v));
    else if (!v.includes('.')) v += '.00';
  }
  const text = stSetValue(I.text, file.kind, el.dataset.k, v, how === 'float');
  const key = `mod:${I.folder}|${I.ztd}|${I.entry}`;
  if (CodeEditor.isOpen(key) && CodeEditor.isDirty(key)) return toast('It’s open in Write Code with changes not saved: save or close it there first.', 5000);
  const r = await lab.modkitEntrySave(I.folder, I.ztd, I.entry, text, I.crlf);
  if (r && r.ok === false) return toast(esc(r.error || 'It couldn’t be saved.'), 4000);
  I.text = text;
  if (CodeEditor.isOpen(key)) CodeEditor.reload(key, text);
  ST.inspNote = 'Saved';
  const n = $('st-inspnote');
  if (n) n.textContent = 'Saved';
  el.classList.remove('unset');
  clearTimeout(ST.noteT);
  ST.noteT = setTimeout(() => {
    ST.inspNote = '';
    const n2 = $('st-inspnote');
    if (n2) n2.textContent = '';
  }, 1800);
}

// A map's preview picture, as the map list shows it (the engine reads it:
// the mod has to be on)
async function stMapPicture(node, gen) {
  const mp = stMapOf(node);
  const el = $('st-mappic');
  if (!mp || !el) return;
  const stem = mp.file.split('/').pop().replace(/\.scn$/i, '');
  const v = await gfView(`freeform/${stem}/${stem}.ani`);
  if (gen !== MK.gen) return;
  const box = $('st-mappic');
  if (!box) return;
  const png = v && v.png ? (Array.isArray(v.png) ? v.png[0] : v.png) : null;
  box.innerHTML = png ? `<img src="data:image/png;base64,${png}" alt="">` : '<span class="muted" style="font-size:11.5px;padding:8px">No preview (or the mod is off)</span>';
}

// ---------------------------------------------------------------- actions
Object.assign(ACTIONS, {
  stPick: (n) => {
    if (AN.busy) return toast('Wait for the animal’s current operation to finish.', 3000);
    if (AN.ed && AN.ed.dirty && !n.startsWith('animal:') && !confirm('Leave this drawing without saving it?')) return;
    if (AN.ed && AN.ed.dirty && n !== 'animal:' + AN.type && !confirm('Leave this drawing without saving it?')) return;
    if (MK.dirty && stNode() === 'about' && n !== 'about' && !confirm('Leave without saving its details?')) return;
    if (n !== 'about') MK.dirty = false;
    if (n.startsWith('animal:') && n !== 'animal:' + AN.type) AN.ed = null;
    stSetNode(n);
    ST.showAll = false;
    // (picked from elsewhere - the Lab's MCP: the studio shown)
    if (S.tab !== 'mods' || S.modsLeft !== 'library') {
      S.modsLeft = 'library';
      return setTab('mods');
    }
    renderModKit();
  },
  stPicker: () => {
    ST.picker = !ST.picker;
    MK.newOpen = false;
    renderModKit().then(() => ST.picker && $('mk-filter') && $('mk-filter').focus());
  },
  stDock: () => {
    ST.dock = !ST.dock;
    store.set('stDock', ST.dock);
    renderModKit();
  },
  stDockTab: (t) => {
    ST.dockTab = t;
    ST.dock = true;
    renderModKit();
  },
  stAnimalTab: (t) => {
    if (AN.busy) return toast('Wait for the animal’s current operation to finish.', 3000);
    if (AN.ed && AN.ed.dirty && !confirm('Leave this drawing without saving it?')) return;
    AN.ed = null;
    ST.animalTab = t;
    renderModKit();
  },
  stShowAll: () => {
    ST.showAll = !ST.showAll;
    stInspRender(MK.mods.find((x) => x.folder === MK.sel), MK.contents, stNode());
  },
  stOpenFile: (arg) => {
    const [ztd, entry] = String(arg).split('|');
    if (MK.sel && entry) openModEntry(MK.sel, ztd, entry);
  },
  // (B's cards: each a short series of questions, then the studio on it)
  stNewAnimal: async () => {
    if (AN.busy) return toast('Wait for the animal’s current operation to finish.', 3000);
    if (!MK.sel) return;
    await stCatalog();
    await ACTIONS.anNew();
    if (AN.type && (AN.list || []).some((a) => a.type === AN.type)) {
      ST.tree = null;
      stSetNode('animal:' + AN.type);
      renderModKit();
    }
  },
  stNewMap: async () => {
    if (!MK.sel) return;
    const a = await ask(
      'A new map',
      [
        {
          key: 'size',
          label: 'Its size (the game’s maps are Small, Medium or Large)',
          value: '75x75',
          options: [
            { value: '75x75', label: 'Small (75 x 75)' },
            { value: '125x125', label: 'Medium (125 x 125)' },
            { value: '150x150', label: 'Large (150 x 150)' },
            { value: 'custom', label: 'My own size…' },
          ],
        },
        { key: 'w', label: 'Own size: across (16 to 512)', value: '100' },
        { key: 'h', label: 'Own size: down (16 to 512)', value: '100' },
      ],
      'Start it',
    );
    if (!a) return;
    let size = a.size;
    if (size === 'custom') {
      const w = Math.round(+a.w),
        h = Math.round(+a.h);
      if (!(w >= 16 && w <= 512 && h >= 16 && h <= 512)) return toast('Each side 16 to 512 tiles.', 3000);
      size = `${w}x${h}`;
    }
    // (made in the Test Zoo for now: draw it there, then Save as a map into
    // this mod)
    store.set('mapTargetMod', MK.sel);
    setTab('scene');
    const r = await CONTROL.sceneLoad({ map: 'blank:' + size });
    toast(
      r && r.ok
        ? 'A blank map, editing on: paint the ground, draw the zoo wall with the Zoo Fence, place an entrance, then <b>Save as a map</b> into this mod.'
        : esc((r && r.error) || 'The Test Zoo isn’t running.'),
      7000,
    );
  },
  stEditMap: async (file) => {
    const stem = String(file).split('/').pop().replace(/\.scn$/i, '');
    const m = MK.mods.find((x) => x.folder === MK.sel);
    if (m && !m.enabled) {
      await lab.gameModsEnable(m.folder, true);
      lab.engineRestart();
      toast('Switched on: the Test Zoo starts again with it, then loads the map.', 4000);
      await new Promise((r) => setTimeout(r, 1500));
    }
    setTab('scene');
    const r = await CONTROL.sceneLoad({ map: stem });
    if (r && r.ok && !(sc() || {}).editing) lab.engineSend({ cmd: 'mapEdit', on: true });
    if (r && r.error) toast(esc(r.error), 6000);
  },
  // Any of the game's items, copied into the mod to change it
  stChangeItem: async () => {
    if (!MK.sel) return;
    const cat = await stCatalog();
    if (!cat.length) return toast('The Test Zoo’s engine lists the game’s items: start it first (Test Zoo).', 5000);
    const kinds = { animal: 'Animals', building: 'Buildings', object: 'Scenery and objects', fence: 'Fences', path: 'Paths', staff: 'Staff' };
    const opts = cat
      .filter((i) => /\.(ai|uca|ucs|ucb)$/i.test(i.file) && !i.fromMod)
      .map((i) => ({ i, k: toolKindOf(i) === 'object' && /scenery\/building\//.test(i.file) ? 'building' : toolKindOf(i) }))
      .filter((x) => kinds[x.k])
      .sort((a, b) => kinds[a.k].localeCompare(kinds[b.k]) || itemName(a.i).localeCompare(itemName(b.i)))
      .map((x) => ({ value: x.i.file, label: `${kinds[x.k]}: ${itemName(x.i)}` }));
    const a = await ask('Change a game item', [{ key: 'file', label: 'Which one (its copy takes the game’s place while the mod is on)', value: opts[0] && opts[0].value, options: opts }], 'Copy it in');
    if (!a || !a.file) return;
    const r = await lab.modkitCopyIn(MK.sel, a.file);
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be copied in.'), 5000);
    ST.tree = null;
    MK.contents = null;
    stSetNode('item:' + r.ztd + '|' + r.entry);
    toast(r.had ? 'It was already in the mod.' : 'Copied in: its values are in the Inspector.', 3000);
    renderModKit();
  },
});

// (Write Code's mod files saved, a drawing saved, a map packed: the tree read
// again next time)
function stInvalidate() {
  ST.tree = null;
  ST.problems = null;
  MK.contents = null;
}
