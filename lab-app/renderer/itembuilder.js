// Item authoring uses the shared pixel editor, importer and game art compiler.
const IB = { folder: null, items: [] };
async function ibLoad(folder) {
  if (IB.folder === folder) return;
  const r = await lab.animalDo(folder, { op: 'animals', itemOp: true });
  IB.folder = folder;
  IB.items = r.ok ? r.items : [];
}
async function ibSelect(key) {
  const row = IB.items.find(i => i.key === key);
  if (!row) return;
  if (AN.item?.key !== key) {
    AN.item = row; AN.type = key; AN.info = null; AN.ed = null;
    AN_DIRS.splice(0, AN_DIRS.length, ...(row.kind === 'ground' || row.kind === 'path' ? ['N'] : ['N', 'NE', 'E', 'SE', 'S', 'SW', 'W', 'NW']));
    await anLoadInfo();
    if (AN.info && !AN.info.subs[AN.sub]) AN.sub = Object.keys(AN.info.subs)[0];
  } else if (!AN.info) await anLoadInfo();
}
Object.assign(ACTIONS, {
  ibNew: async () => {
    if (!MK.sel || AN.busy) return;
    const kind = await ask('Build an item', [
      { key: 'kind', label: 'What it does in the zoo', value: 'building', options: [
        { value: 'building', label: 'Building' }, { value: 'fence', label: 'Fence' },
        { value: 'staff', label: 'Staff member' }, { value: 'guest', label: 'Guest appearance' },
        { value: 'filter', label: 'Tank filter' }, { value: 'scenery', label: 'Scenery or object' },
        { value: 'tankwall', label: 'Marine tank walls' }, { value: 'entrance', label: 'Zoo entrance' },
        { value: 'foliage', label: 'Plant or tree' }, { value: 'rock', label: 'Rock' },
        { value: 'shelter', label: 'Animal shelter' }, { value: 'toy', label: 'Animal enrichment or toy' },
        { value: 'path', label: 'Path tiles (20 connected shapes)' }, { value: 'ground', label: 'Ground texture (reskins an existing terrain)' },
        { value: 'food', label: 'Animal food' }] },
      { key: 'mode', label: 'Workflow', value: 'new', options: [
        { value: 'new', label: 'Create a separate item with its own name and art' },
        { value: 'change', label: 'Customize an existing item while this mod is on' }] },
    ], 'Next');
    if (!kind) return;
    const cat = await stCatalog();
    const k = i => i.section === 'tankwall' ? 'tankwall' : i.section === 'food' ? 'food' :
      (i.members || []).includes('zoogate') ? 'entrance' :
      (i.members || []).includes('shelters') ? 'shelter' :
      (i.members || []).includes('toys') ? 'toy' :
      (i.members || []).includes('rocks') ? 'rock' :
      i.file.startsWith('scenery/foliage/') ? 'foliage' :
      i.section === 'filter' || toolKindOf(i) === 'filter' ? 'filter' :
      i.file.startsWith('scenery/building/') ? 'building' : toolKindOf(i) === 'object' ? 'scenery' : toolKindOf(i);
    const options = kind.kind === 'ground' ? [['grass','Grass'],['grass_sv','Savannah grass'],['sand','Sand'],['dirt','Dirt'],['ffloor','Rainforest floor'],['ffloorc','Conifer floor'],['ffloord','Deciduous floor'],['gravel','Gravel'],['snow','Snow'],['ccrete','Concrete'],['gunnite','Gunite']].map(([v,label]) => ({value:'terrain/' + v + '.tga',label})) : kind.kind === 'guest' ? ['man', 'woman', 'boy', 'girl'].map(x => ({ value: x, label: x })) :
      cat.filter(i => !i.fromMod && k(i) === kind.kind && /\.ai$/i.test(i.file)).sort((a,b) => itemName(a).localeCompare(itemName(b))).map(i => ({ value: i.file, label: itemName(i) }));
    if (kind.kind === 'food' && !options.length) for (const [key,label] of [['herbchow','Herbivore chow'],['carnchow','Meat'],['dinogras','Dinosaur greens'],['dinocarn','Dinosaur meat'],['fish','Fish'],['krill','Krill']]) options.push({value:(key === 'fish' || key === 'krill' ? 'scenery/food/' : 'scenery/other/') + key + '.ai',label});
    if (!options.length) return toast('The catalog is not ready for that kind. Open Test Zoo and try again.', 5000);
    const a = await ask(kind.kind === 'guest' ? 'Make a guest appearance' : 'Name and role', [
      { key: 'name', label: 'Name for this creation' },
      { key: 'source', label: kind.kind === 'guest' ? 'Guest kind whose appearance it replaces (the game has four guest kinds)' : 'Working role and properties to start from', value: options[0].value, options },
    ], 'Create it');
    if (!a?.name) return;
    AN.busy = 'Creating the item…';
    let r;
    try { r = await lab.animalDo(MK.sel, { op: 'new', itemOp: true, kind: kind.kind, mode: kind.mode, name: a.name,
      source: kind.kind === 'guest' ? 'guests/guests.ai' : a.source, guestKind: kind.kind === 'guest' ? a.source : undefined }); }
    finally { AN.busy = ''; }
    if (!r?.ok) return toast(esc(r?.error || 'The item could not be created.'), 6000);
    IB.folder = null; await ibLoad(MK.sel); AN.info = null; AN.item = null; AN.ed = null;
    stInvalidate(); stSetNode('built:' + r.key); await renderModKit();
    toast('Created. Draw an animation with Blank, import external art, or choose Remake to customize the source.', 6000);
  },
});
ST_FIELDS.filter = [['Price', [['cPurchaseCost', 'Price to build', 'money']]],
  ['Filtering', [['cFilterDelay', 'Seconds between cleanings'], ['cFilterCleanAmount', 'Cleaning when healthy'],
    ['cFilterDecayedCleanAmount', 'Cleaning when worn'], ['cFilterUpkeep', 'Cost per cleaning'],
    ['cStartingHealth', 'Initial health'], ['cDecayedHealth', 'Worn at health'], ['cDecayTime', 'Seconds per health point']]]];
ST_FIELDS.staff = [['Pay and movement', [['cPurchaseCost', 'Hiring cost / monthly pay', 'money'], ['cSlowRate', 'Walking rate']]]];
ST_FIELDS.rock = ST_FIELDS.foliage = ST_FIELDS.scenery;
ST_FIELDS.shelter = ST_FIELDS.toy = ST_FIELDS.building;
ST_FIELDS.entrance = ST_FIELDS.scenery;
ST_FIELDS.tankwall = ST_FIELDS.fence;
ST_FIELDS.food = [['Food', [['cPurchaseCost', 'Cost per pile', 'money'], ['cFoodUnits', 'Units in a pile'], ['cMaxFoodUnits', 'Maximum units']]]];
