// ZT1 Lab: an animal's needs, in the mod studio (studio.js: an animal's
// Needs tab). Everything the game weighs for it, as its species file keeps
// it (re/lab/animalmaker.py needs / setneeds / copyneeds):
//  - its kind ([Member]: a zoo animal, Marine Mania's "aqua", Dinosaur
//    Digs' "dinosaur"), who looks after it (cKeeper), its food
//    (cKeeperFoodType), whether it needs a shelter
//  - its home: biome (cHabitat), continent (cLocation), family (cFamily)
//  - the ground it likes ([cCompatibleTerrain]), the plants, rocks, shelters
//    and toys it likes ([cSuitableObjects]) and the animals it lives with
//    happily or not ([cCompatibleAnimals]) - each a value, -100 to 100
//  - or another animal's needs, all at once (Copy needs from…)
// Changes are saved into the mod a moment after the last one.
// (loaded after studio.js)

const NE = { key: null, data: null, error: '', texts: {}, saveT: null, note: '' };
// (food.cfg's order, then Dinosaur Digs' food01.cfg and Marine Mania's
// food02.cfg: the keeper's food, by cKeeperFoodType - src/Staff.cpp foodFor)
const NE_FOODS = ['Herbivore chow', 'Meat', 'Fruit', 'Bamboo', 'Grass', 'Fish chow', 'Insects', 'Dinosaur meat', 'Dinosaur greens', 'Krill', 'Fish', 'Chum', 'Sea grass', 'Sea urchins'];
// (numbers the game has no text for: the game's own files name them in
// comments - "Non-habitat", "Bad-habitat", "Poo")
const NE_KNOWN = { 9410: 'No habitat (paths, buildings)', 9414: 'Bad habitat', 6062: 'Dung' };
// (the ground types' names as players know them: the engine's are joined up)
const NE_GROUND = { savannahgrass: 'Savannah grass', forestfloor: 'Rainforest floor', brownrock: 'Brown rock', grayrock: 'Gray rock', freshwater: 'Fresh water', saltwater: 'Salt water', deciduousforestfloor: 'Deciduous forest floor', coniferousforestfloor: 'Coniferous forest floor', gunnite: 'Gunite' };
const neGround = (n) => NE_GROUND[n] || (n ? n.charAt(0).toUpperCase() + n.slice(1) : n);
const NE_KINDS = [['zoo', 'Zoo animal'], ['aqua', 'Marine animal'], ['dinosaur', 'Dinosaur']];
// (the text numbers each list's names come from)
const NE_RANGES = { habitat: [9400, 9414], location: [9600, 9640], family: [5200, 5240], keeper: [9550, 9553] };

// The file a node's needs are in: a new animal's .uca, or a game animal the
// mod changes
function neFileOf(node) {
  if (node.startsWith('animal:')) return { type: node.slice(7) };
  const it = stItemOf(node);
  return it && it.kind === 'animal' ? { file: it.file, type: it.file.split('/').pop().replace(/\.\w+$/, '') } : null;
}

// (one read at a time for a node: a page drawn again meanwhile waits on the
// same one)
function neLoad(m, node) {
  const key = m.folder + '|' + node;
  if (NE.key !== key || !NE.loading) NE.loading = neRead(m, node, key);
  return NE.loading;
}
async function neRead(m, node, key) {
  const f = neFileOf(node);
  if (!f) return;
  if (NE.key === key && NE.data) return;
  NE.key = key;
  NE.data = null;
  NE.error = '';
  const r = await lab.animalDo(m.folder, { op: 'needs', ...f });
  if (NE.key !== key) return;
  if (!r.ok) NE.error = r.error || 'Its needs couldn’t be read.';
  else NE.data = r;
  // (the names of every number in it, and of the lists to pick from)
  const ids = new Set();
  for (const [a, b] of Object.values(NE_RANGES)) for (let i = a; i <= b; i++) ids.add(i);
  if (NE.data) for (const k of ['objects', 'animals']) for (const [id] of NE.data[k]) ids.add(id);
  await neTexts([...ids]);
}
async function neTexts(ids) {
  const want = ids.filter((i) => NE.texts[i] === undefined);
  for (let i = 0; i < want.length; i += 400) {
    const chunk = want.slice(i, i + 400);
    const r = await ACTIONS.gameText(chunk.join(' '));
    for (const id of chunk) NE.texts[id] = '';
    for (const t of (r && r.texts) || []) NE.texts[t.id] = t.text;
  }
}
// (a number's name: an item's from the catalog, else the game's text)
function neName(id) {
  const c = (SC.catalog || []).find((i) => i.nameId === id);
  return (c && itemName(c)) || NE.texts[id] || NE_KNOWN[id] || '#' + id;
}
function neIcon(id) {
  const c = (SC.catalog || []).find((i) => i.nameId === id);
  return c && c.icon ? iconImg(c.icon, 'neicon') : '<span class="neicon none"></span>';
}

function needsHtml() {
  if (NE.error) return `<div class="red" style="padding:20px">${esc(NE.error)}</div>`;
  const n = NE.data;
  if (!n) return '<div class="muted" style="padding:20px">Reading its needs…</div>';
  const opt = (v, label, cur) => `<option value="${esc(String(v))}"${String(v) === String(cur) ? ' selected' : ''}>${esc(label)}</option>`;
  const range = (k) => {
    const [a, b] = NE_RANGES[k];
    const out = [];
    for (let i = a; i <= b; i++) if (NE.texts[i]) out.push([i, NE.texts[i]]);
    if (n[k] && !out.some(([i]) => i === n[k])) out.push([n[k], neName(n[k])]);
    return out;
  };
  const sel = (k, label, items, tip) =>
    `<label class="nefield" title="${esc(tip)}"><span>${label}</span><select class="neval" data-k="${k}">${items.map(([v, l]) => opt(v, l, n[k] === null ? '' : n[k])).join('')}</select></label>`;
  const kinds = NE_KINDS.map(([k, l]) => `<div class="${n.kind === k ? 'on' : ''}" data-act="neKind" data-arg="${k}">${l}</div>`).join('');
  const keepers = [['', 'A keeper'], ...range('keeper').filter(([i]) => i !== 9550 && i !== 9553)];
  const slider = (list, id, value, label, icon) =>
    `<div class="nerow" title="${esc(label)}: ${value > 0 ? 'likes it' : value < 0 ? 'dislikes it' : 'doesn’t mind'} (${value})">${icon || ''}<span class="nm">${esc(label)}</span><input type="range" class="neslide" data-list="${list}" data-id="${id}" min="-100" max="100" step="1" value="${value}"><b class="${value > 0 ? 'pos' : value < 0 ? 'neg' : ''}">${value > 0 ? '+' : ''}${value}</b>${
      list === 'terrain' ? '' : `<span class="nex" data-act="neDrop" data-arg="${list}|${id}" title="Take it off the list">×</span>`
    }</div>`;
  const terr = new Map(n.terrain.map(([t, v]) => [t, v]));
  const terrains = (SC.terrains || []).length
    ? SC.terrains.map((t) => slider('terrain', t.type, terr.get(t.type) || 0, neGround(t.name), iconImg(t.icon, 'neicon'))).join('')
    : n.terrain.map(([t, v]) => slider('terrain', t, v, 'Ground ' + t)).join('');
  // (its likes: the biomes the game counts, then the things it can be given)
  const biome = (id) => id >= 9400 && id <= 9414;
  const objs = n.objects.slice().sort((a, b) => biome(b[0]) - biome(a[0]) || b[1] - a[1]);
  const anims = n.animals.slice().sort((a, b) => b[1] - a[1]);
  return `<div class="needs">
      <div class="nehead"><div><div class="ststartt">What it needs</div><div class="muted">Everything the game weighs for it: its food, its home, the ground, plants, rocks and shelters it likes and the animals it gets on with. Saved into the mod as you change it. <span id="ne-note" class="green">${esc(NE.note)}</span></div></div>
        <div class="spacer"></div><div class="btn small" data-act="neCopy" title="Another animal's needs, all at once: a preset to start from">Copy needs from…</div></div>
      <div class="negrid">
        <div class="edcard"><div class="edcardh"><span>Kind and care</span></div>
          <div class="seg mini nekinds" title="Its [Member]: a Marine Mania animal is “aqua”, a Dinosaur Digs one “dinosaur”">${kinds}</div>
          ${sel('keeper', 'Looked after by', keepers, 'cKeeper: who feeds and cares for it')}
          <label class="nefield" title="cKeeperFoodType: plants or meat"><span>Eats</span><select class="neval" data-k="foodType">${opt('herbivore', 'Plants (herbivore)', n.food.type)}${opt('carnivore', 'Meat (carnivore)', n.food.type)}</select></label>
          <label class="nefield" title="cKeeperFoodType: the food the keeper brings"><span>Its food</span><select class="neval" data-k="chow">${NE_FOODS.map((f, i) => opt(i, f, n.food.chow)).join('')}</select></label>
          <label class="nefield" title="An enabled mod's food, supplied by its normal keeper"><span>Custom food</span><select class="neval" data-k="customFood">${opt('', 'Use the food above', n.food.custom)}${(SC.catalog || []).filter(i => i.section === 'food' && i.fromMod).map(i => opt(i.type || i.file.split('/').pop().replace(/\.ai$/,''), itemName(i), n.food.custom)).join('')}</select></label>
          <label class="nefield flag" title="cNeedShelter"><span>Needs a shelter</span><input type="checkbox" class="neval" data-k="needShelter"${n.needShelter ? ' checked' : ''}></label>
        </div>
        <div class="edcard"><div class="edcardh"><span>Home</span></div>
          ${sel('habitat', 'Biome', range('habitat'), 'cHabitat: the biome the exhibit should be')}
          ${sel('location', 'Continent', range('location'), 'cLocation: where it comes from (the zoo’s continent awards)')}
          ${sel('family', 'Family', range('family'), 'cFamily: its family (others list it by this to like or dislike living with it)')}
        </div>
      </div>
      <div class="edcard"><div class="edcardh"><span>Ground it likes</span><span class="dim">how much of the exhibit's ground it wants of each</span></div><div class="nelist">${terrains}</div></div>
      <div class="edcard"><div class="edcardh"><span>Biomes, plants, rocks and shelters it likes</span><span class="spacer"></span><div class="btn small ghost" data-act="neAdd" data-arg="objects">+ Add</div></div>
        <div class="nelist">${objs.map(([id, v]) => slider('objects', id, v, neName(id), neIcon(id))).join('') || '<div class="muted">Nothing yet.</div>'}</div></div>
      <div class="edcard"><div class="edcardh"><span>Animals it lives with</span><span class="spacer"></span><div class="btn small ghost" data-act="neAdd" data-arg="animals">+ Add</div></div>
        <div class="nelist">${anims.map(([id, v]) => slider('animals', id, v, neName(id), neIcon(id))).join('') || '<div class="muted">Nothing yet.</div>'}</div></div>
    </div>`;
}

function needsMounted() {
  const n = NE.data;
  if (!n) return;
  document.querySelectorAll('.neval').forEach(
    (el) =>
      (el.onchange = () => {
        const k = el.dataset.k;
        if (k === 'foodType') n.food.type = el.value;
        else if (k === 'chow') n.food.chow = +el.value;
        else if (k === 'customFood') n.food.custom = el.value;
        else if (k === 'needShelter') n.needShelter = el.checked ? 1 : 0;
        else n[k] = el.value === '' ? 0 : +el.value;
        neSave();
      }),
  );
  document.querySelectorAll('.neslide').forEach((el) => {
    el.oninput = () => {
      const b = el.nextElementSibling;
      const v = +el.value;
      b.textContent = (v > 0 ? '+' : '') + v;
      b.className = v > 0 ? 'pos' : v < 0 ? 'neg' : '';
    };
    el.onchange = () => {
      const list = el.dataset.list,
        id = +el.dataset.id,
        v = +el.value;
      const row = n[list].find(([a]) => a === id);
      if (row) row[1] = v;
      else n[list].push([id, v]);
      // (ground not wanted at all: off its list, as the game's files leave it)
      if (list === 'terrain') n.terrain = n.terrain.filter(([, x]) => x !== 0);
      neSave();
    };
  });
}

// (saved a moment after the last change: a slider dragged saves once)
function neSave() {
  clearTimeout(NE.saveT);
  NE.saveT = setTimeout(async () => {
    const node = stNode();
    const f = neFileOf(node);
    if (!f || !NE.data) return;
    const n = NE.data;
    const r = await lab.animalDo(MK.sel, { op: 'setneeds', ...f, needs: { kind: n.kind, keeper: n.keeper, food: n.food, habitat: n.habitat, location: n.location, family: n.family, needShelter: n.needShelter, terrain: n.terrain, objects: n.objects, animals: n.animals } });
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be saved.'), 5000);
    NE.note = 'Saved';
    // (the Inspector's copy of the file is old now: read again, so its next
    // change doesn't put this one back)
    ST.insp = null;
    const el = $('ne-note');
    if (el) el.textContent = 'Saved';
    ST.problems = null;
    ST.insp = null;
    setTimeout(() => {
      NE.note = '';
      const e2 = $('ne-note');
      if (e2) e2.textContent = '';
    }, 1800);
  }, 700);
}
function neRender() {
  const el = $('ne');
  if (!el) return;
  el.innerHTML = needsHtml();
  needsMounted();
}

Object.assign(ACTIONS, {
  neKind: (k) => {
    const n = NE.data;
    if (!n || n.kind === k) return;
    n.kind = k;
    // (and who looks after that kind, as the game's own: a marine specialist,
    // a paleontologist, a keeper)
    n.keeper = k === 'aqua' ? 9552 : k === 'dinosaur' ? 9551 : 0;
    toast(
      k === 'zoo'
        ? 'A zoo animal now: it lives in a fenced exhibit, cared for by a keeper.'
        : k === 'aqua'
          ? 'A marine animal now (Marine Mania): it lives in a tank, cared for by a marine specialist. Its swimming and tank behaviour come from the animal it behaves like - start it from a marine one for those.'
          : 'A dinosaur now (Dinosaur Digs): cared for by a paleontologist. It needs an egg - its egg_idle, egg_hatch and egg_break animations - and dinosaur behaviour comes from the animal it behaves like.',
      7000,
    );
    neSave();
    neRender();
  },
  neDrop: (arg) => {
    const [list, id] = String(arg).split('|');
    NE.data[list] = NE.data[list].filter(([a]) => a !== +id);
    neSave();
    neRender();
  },
  neAdd: async (list) => {
    const cat = await stCatalog();
    let opts;
    if (list === 'objects') {
      const biomes = [];
      for (let i = 9400; i <= 9414; i++) if (NE.texts[i]) biomes.push({ value: String(i), label: 'Biome: ' + NE.texts[i] });
      const kindOf = (i) => (/^scenery\/foliage\//i.test(i.file) ? 'Plant' : /rock/i.test(i.file) ? 'Rock' : (i.members || []).includes('shelters') ? 'Shelter' : (i.members || []).includes('toys') ? 'Toy' : 'Scenery');
      opts = biomes.concat(
        cat
          .filter((i) => i.nameId > 0 && toolKindOf(i) === 'object' && (!/^scenery\/building\//i.test(i.file) || (i.members || []).some(m => m === 'shelters' || m === 'toys')))
          .map((i) => ({ value: String(i.nameId), label: `${kindOf(i)}: ${itemName(i)}` }))
          .sort((a, b) => a.label.localeCompare(b.label)),
      );
    } else {
      const fams = [];
      for (let i = 5200; i <= 5240; i++) if (NE.texts[i]) fams.push({ value: String(i), label: 'Family: ' + NE.texts[i] });
      opts = fams.concat(
        cat
          .filter((i) => i.nameId > 0 && toolKindOf(i) === 'animal')
          .map((i) => ({ value: String(i.nameId), label: 'Animal: ' + itemName(i) }))
          .sort((a, b) => a.label.localeCompare(b.label)),
      );
    }
    const have = new Set(NE.data[list].map(([a]) => String(a)));
    opts = opts.filter((o) => !have.has(o.value));
    if (!opts.length) return toast('The Test Zoo’s engine lists them: start it first (Test Zoo).', 4000);
    const a = await ask(list === 'objects' ? 'Something it likes (or doesn’t)' : 'An animal or family it lives with', [
      { key: 'id', label: 'Which', value: opts[0].value, options: opts },
      { key: 'v', label: 'How much (-100 hates it, 0 doesn’t mind, 100 loves it)', value: '10' },
    ], 'Add it');
    if (!a) return;
    const v = Math.max(-100, Math.min(100, Math.round(+a.v || 0)));
    NE.data[list].push([+a.id, v]);
    await neTexts([+a.id]);
    neSave();
    neRender();
  },
  neCopy: async () => {
    const cat = (await stCatalog()).filter((i) => toolKindOf(i) === 'animal' && /\.(ai|uca)$/i.test(i.file)).sort((x, y) => itemName(x).localeCompare(itemName(y)));
    if (!cat.length) return toast('The Test Zoo’s engine lists the animals: start it first (Test Zoo).', 4000);
    const a = await ask('Copy needs from another animal', [
      { key: 'from', label: 'Its food, home, ground, plants, rocks, shelters and neighbours, from', value: cat[0].file, options: cat.map((i) => ({ value: i.file, label: itemName(i) })) },
      { key: 'kind', label: 'Its kind and who looks after it too', value: 'no', options: [{ value: 'no', label: 'No: keep this one’s' }, { value: 'yes', label: 'Yes' }] },
    ], 'Copy them');
    if (!a) return;
    const f = neFileOf(stNode());
    const r = await lab.animalDo(MK.sel, { op: 'copyneeds', ...f, from: a.from, kindToo: a.kind === 'yes' });
    if (!r.ok) return toast(esc(r.error || 'They couldn’t be copied.'), 5000);
    toast(`Its needs are the ${esc(itemName(cat.find((i) => i.file === a.from)))}'s now: change any of them.`, 4000);
    NE.key = null;
    ST.problems = null;
    ST.insp = null;
    renderModKit();
  },
});
