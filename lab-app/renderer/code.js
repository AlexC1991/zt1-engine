// Helpers the views share: HTML escaping, C highlighting, sorting functions
// into the game's systems, reading what a mod does from its source.
const esc = (s) =>
  String(s ?? '').replace(/[&<>"']/g, (c) => ({ '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' })[c]);

const C_KEYWORDS = new Set(
  ('if else while for do return break continue switch case default goto sizeof struct union enum typedef static const ' +
    'void int char bool float double long short unsigned signed auto true false nullptr this new delete ' +
    'undefined undefined1 undefined2 undefined4 undefined8 byte ushort uint ulong code class virtual override include define').split(' '),
);

// One line of C as highlighted HTML. fields(offsetHex) gives a mapped
// field's name for "+ 0x2a8" (shown as the name, clickable)
// A global's name from the knowledge base (constants are kept by their
// number in decimal; a global's meaning starts "name: ...")
function globalName(hex) {
  const c = typeof S !== 'undefined' && S.kb && S.kb.constants;
  const k = c && c[String(parseInt(hex, 16))];
  if (!k || k.kind !== 'global') return null;
  const name = (/^\s*(?:Number \S+ = )?([A-Za-z_]\w*)/.exec(k.meaning || '') || [])[1];
  return name ? { name, meaning: k.meaning } : null;
}

// Pointers' element sizes in the function being drawn (int *param_1: 4),
// so p[0xec] is shown as the byte offset it is (+0x3b0) - Ghidra indexes a
// struct it doesn't know as an array of ints
let C_ELEM = {};
const C_SIZES = { 1: /^(char|uchar|byte|undefined1|undefined|bool)$/, 2: /^(short|ushort|undefined2|word|wchar_t)$/, 4: /^(int|uint|undefined4|float|dword|long|ulong)$/, 8: /^(double|undefined8|longlong|ulonglong|qword)$/ };
function cElemSizes(text) {
  const out = {};
  for (const m of text.matchAll(/\b([A-Za-z_]\w*)\s*\*\s*([A-Za-z_]\w*)\s*[;,)]/g))
    for (const [n, re] of Object.entries(C_SIZES)) if (re.test(m[1])) out[m[2]] = +n;
  return out;
}

function highlightC(line, fields) {
  let out = '';
  const re = /(\/\/.*$|\/\*.*?\*\/|\/\*.*$)|("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(0x[0-9a-fA-F]+|\b\d+(?:\.\d+)?\b)|([A-Za-z_][A-Za-z0-9_:]*)(\s*\()?|([^A-Za-z0-9_"'\/]+|\/)/g;
  let m;
  let last = 0;
  while ((m = re.exec(line))) {
    if (m.index > last) out += esc(line.slice(last, m.index));
    last = re.lastIndex;
    if (m[1]) out += `<span class="k-com">${esc(m[1])}</span>`;
    else if (m[2]) out += `<span class="k-str">${esc(m[2])}</span>`;
    else if (m[3]) {
      // (an index into a pointer whose element size is known: its byte offset
      // shown, and the field there if it's mapped)
      const ix = /([A-Za-z_]\w*)\s*\[\s*$/.exec(line.slice(0, m.index));
      if (ix && C_ELEM[ix[1]] && /^0x/.test(m[3]) && /^\s*\]/.test(line.slice(re.lastIndex))) {
        const off = '0x' + (parseInt(m[3], 16) * C_ELEM[ix[1]]).toString(16);
        const f = fields ? fields(off, ix[1]) : null;
        out += f
          ? `<span class="k-field" data-act="fieldTip" data-arg="${esc(f.key)}" title="${esc(f.title)}">${esc(m[3])}<span class="dim"> +${off} ${esc(f.name)}</span></span>`
          : `<span class="k-num" title="byte offset ${off}">${esc(m[3])}<span class="dim"> +${off}</span></span>`;
        continue;
      }
      // (an offset after "+ " may be a mapped field)
      const before = line.slice(0, m.index).trimEnd();
      const base = before.endsWith('+') ? (before.slice(0, -1).trimEnd().match(/([A-Za-z_]\w*)$/) || [])[1] : null;
      // (pointer maths on a typed pointer - (param_1 + 0x17) with int
      // *param_1 - counts in its elements: the byte offset is 4 times it;
      // ((int)param_1 + 0x5c), cast to a number first, is bytes already)
      const cast = base && /\(\s*int\s*\)\s*$/.test(before.slice(0, -1).trimEnd().slice(0, -base.length));
      const scale = base && !cast && C_ELEM[base] > 1 && /^0x/.test(m[3]) ? C_ELEM[base] : 1;
      const off = scale > 1 ? '0x' + (parseInt(m[3], 16) * scale).toString(16) : m[3].toLowerCase();
      const f = fields && base ? fields(off, base) : null;
      const shown = scale > 1 ? ` +${off}` : '';
      out += f
        ? `<span class="k-field" data-act="fieldTip" data-arg="${esc(f.key)}" title="${esc(f.title)}">${esc(m[3])}<span class="dim">${shown} ${esc(f.name)}</span></span>`
        : `<span class="k-num"${shown ? ` title="byte offset ${off}"` : ''}>${esc(m[3])}${shown ? `<span class="dim">${shown}</span>` : ''}</span>`;
    } else if (m[4]) {
      const word = m[4];
      // (another function: opens it; a global: where it's used)
      const fn = word.match(/^FUN_([0-9a-fA-F]{6,8})$/);
      const dat = word.match(/^(?:DAT|PTR|s_\w*?)_([0-9a-fA-F]{8})$/) || word.match(/^DAT_([0-9a-fA-F]{6,8})$/);
      if (fn) out += `<span class="k-fn k-link" data-act="openCode" data-arg="0x${fn[1].replace(/^0+/, '')}" title="Open it">${esc(word)}</span>${esc(m[5] || '')}`;
      else if (dat) {
        // (a global named in the knowledge base - Where used › Name it - shows
        // its name beside it, as a mapped field does)
        const g = globalName(dat[1]);
        out += `<span class="k-link${g ? ' k-field' : ''}" data-act="xrefs" data-arg="0x${dat[1].replace(/^0+/, '')}" title="${esc(g ? g.meaning : 'Where is it used?')}">${esc(word)}${g ? `<span class="dim"> ${esc(g.name)}</span>` : ''}</span>${esc(m[5] || '')}`;
      }
      else if (C_KEYWORDS.has(word)) out += `<span class="k-kw">${esc(word)}</span>${esc(m[5] || '')}`;
      else if (m[5]) out += `<span class="k-fn">${esc(word)}</span>${esc(m[5])}`;
      else out += esc(word);
    } else out += esc(m[0]);
  }
  if (last < line.length) out += esc(line.slice(last));
  return out;
}

// One assembly line ("MOV EAX,dword ptr [ESI + 0x2a8]")
function highlightAsm(text) {
  const m = String(text).match(/^(\S+)(\s*)(.*)$/);
  if (!m) return esc(text);
  // (&nbsp;: a plain space between two spans in a flex row is dropped)
  return `<span class="k-fn">${esc(m[1].toLowerCase())}</span>${m[2] ? '&nbsp;' : ''}${esc(m[3]).replace(
    /0x[0-9a-fA-F]+/g,
    (h) => `<span class="k-num">${h}</span>`,
  )}`;
}

// The game's systems, by what a function is called or does
const SYSTEMS = [
  ['Animals', /animal|species|zebra|lion|hunger|happi|predator|prey|dust ?ball|swim|tired|baby|babies|breed|health|chase|flee|caught/i],
  ['Habitats', /exhibit|habitat|fence|tank|suitab|terrain|wave|water|shelter|foliage|rock/i],
  ['Guests', /guest|crowd|donation|tour|price|buy|attraction|admission/i],
  ['Staff', /keeper|maint|staff|trash|litter|compost|vet|scientist/i],
  ['Zoo & goals', /goal|rating|research|award|undo|news|cash|program|scenario|money/i],
  ['Menus & UI', /credit|menu|layout|button|dialog|screen|panel|filter|list|click|options/i],
  ['Sprites & art', /sprite|anim(?!al)|art\b|draw|icon|render|frame|panel|list|filter|ui/i],
];
function systemOf(text) {
  for (const [name, re] of SYSTEMS) if (re.test(text)) return name;
  return 'Other';
}

const isDefaultName = (n) => !n || /^(FUN_|meth_0x|virt_|cls_0x|thunk_|switchD|caseD_|LAB_|entry$)/.test(n);

// "animalUpdateHappiness" -> "Update happiness"; "ZTAnimal::checkHunger" -> "Check hunger"
function friendlyName(name) {
  if (!name) return '';
  const base = name.split('::').pop();
  const words = base
    .replace(/^(animal|exhibit|guest|keeper|maint|tank|zoo|goal|staff)(?=[A-Z])/, '')
    .replace(/([a-z0-9])([A-Z])/g, '$1 $2')
    .replace(/_/g, ' ')
    .trim();
  return words ? words[0].toUpperCase() + words.slice(1).toLowerCase() : base;
}

const prettyStem = (stem) => stem.replace(/[_-]+/g, ' ').replace(/^\w/, (c) => c.toUpperCase());

// What a mod does, read from its source: plain sentences
function modWill(src, kbFns) {
  const out = [];
  const seen = new Set();
  for (const m of src.matchAll(/zoo\.exe\s+(0x[0-9a-fA-F]{6})/g)) {
    if (seen.has(m[1].toLowerCase())) continue;
    seen.add(m[1].toLowerCase());
    const f = kbFns[m[1].toLowerCase()];
    out.push(`Checks <b>${esc(f ? friendlyName(f.name) : m[1])}</b> (${esc(m[1])} in zoo.exe)`);
  }
  for (const m of src.matchAll(/exhibit\(\s*(\d+)\s*,\s*(\d+)\s*,\s*"([^"]*)"/g))
    out.push(`Builds a <b>${m[1]}×${m[2]}</b> exhibit (${esc(m[3])} fence) in the lab zoo`);
  const animals = [...src.matchAll(/animal\(\s*"([^"]+)"/g)].map((m) => m[1]);
  if (animals.length) out.push(`Puts in: <b>${esc([...new Set(animals)].join(', '))}</b>`);
  const run = src.match(/runEngine\(\s*([\d.]+)f?\s*\)/);
  if (run) out.push(`Runs the engine <b>${esc(run[1])} s</b> a step (one tick of each animal's checks)`);
  const steps = src.match(/return\s+step\s*<\s*(\d+)/);
  if (steps) out.push(`Stops after <b>${steps[1]}</b> steps`);
  const watches = [...src.matchAll(/watch\(\s*"([^"]+)"/g)].map((m) => m[1]);
  if (watches.length) out.push(`Watches <b>${esc(watches.join(', '))}</b>`);
  const checks = [...src.matchAll(/expect\(\s*"([^"]+)"/g)].map((m) => m[1]);
  for (const c of checks) out.push(`Checks that <b>${esc(c)}</b>`);
  const views = [...src.matchAll(/view\(\s*"([^"]+)"/g)].map((m) => m[1]);
  if (views.length) out.push(`Shows the <b>${esc([...new Set(views)].join(', '))}</b> view`);
  return out;
}

// The Lab API, from re/lab/lab_api.h: each call and what it does
function parseApi(lines) {
  const out = [];
  let comment = [];
  let last = '';
  for (const raw of lines) {
    const l = raw.trim();
    if (l.startsWith('//')) {
      const t = l.replace(/^\/\/\s?/, '');
      if (/^---/.test(t)) comment = [];
      else comment.push(t);
      continue;
    }
    const m = l.match(/^virtual\s+(.+?)\s*(\w+)\(([^)]*)\)\s*=\s*0;/);
    if (m) {
      // (a comment over several calls in a row - exeU32, exeF32, exeF64 -
      // is each one's)
      const about = comment.length ? comment.join(' ') : last;
      out.push({ name: m[2], sig: `lab->${m[2]}(${m[3].replace(/const char \*/g, '').replace(/\b(int|float|double|bool|unsigned)\s+/g, '').replace(/\*/g, '')})`, about });
      last = about;
      comment = [];
      continue;
    }
    comment = [];
    last = '';
  }
  return out;
}

// Closest name (for "did you mean")
function closest(word, names) {
  const d = (a, b) => {
    const m = Array.from({ length: a.length + 1 }, (_, i) => [i]);
    for (let j = 1; j <= b.length; j++) m[0][j] = j;
    for (let i = 1; i <= a.length; i++)
      for (let j = 1; j <= b.length; j++)
        m[i][j] = Math.min(m[i - 1][j] + 1, m[i][j - 1] + 1, m[i - 1][j - 1] + (a[i - 1] === b[j - 1] ? 0 : 1));
    return m[a.length][b.length];
  };
  let best = null;
  let bd = 3;
  for (const n of names) {
    const x = d(word.toLowerCase(), n.toLowerCase());
    if (x < bd) {
      bd = x;
      best = n;
    }
  }
  return best;
}
