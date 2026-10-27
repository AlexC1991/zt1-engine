// ZT1 Lab: where the original keeps a value (Play original > Find a value).
// The way a cheat finder works: scan its memory for a value you can see -
// its cash, $75,000 - change it in the game (spend some), rescan for the new
// one, and what's left is where it's kept. Then Watch writes: a hardware
// watchpoint on it reports each instruction that changes it, in its function
// (read it in Original Game) - and the registers, the object it's written
// through. main.js orig:find / orig:ask / orig:writes; the hook does the work
// (src/original/Hook.cpp, "its memory").

const MEM = { type: 'int', value: '', found: null, watching: '', writes: [] };

// ---------------------------------------------------------------- live
// The original's state, read from its memory as it plays - what's been found
// of it (re/knowledge.json, 2026-10-12): the game manager (global
// 0x638048: cash +0xc, the date +0x1194, a SYSTEMTIME) and the world (global
// 0x638040: map size +0x34/+0x38, every entity in a vector at +0x80). Each
// entity's class is its function table (+0); its type (+0x128) has its short
// name at +0xa4 and its kind at +0x98
const LIVE = {
  gameMgr: 0x638048,
  world: 0x638040,
  classes: {
    '0x62ff54': 'Animals',
    '0x630f88': 'Guests',
    '0x62d808': 'Fences',
    '0x62dbc0': 'Tank walls',
    '0x62d950': 'Scenery',
    '0x62da88': 'Paths',
    '0x62e0b0': 'Buildings',
    '0x62d6ec': 'Ambient animals',
  },
  // (animals' and guests' happiness, -100..100: +0x2a8 in both, as
  // zooRatingUpdate 0x41f881 reads it - a new guest's is 50)
  happy: ['Animals', 'Guests'],
  names: {}, // (type -> [codename, kind], read once each)
  timer: null,
  last: null,
};
const leHex = (hex, i, n) => parseInt(hex.slice(i * 2, (i + n) * 2).match(/../g).reverse().join(''), 16);
async function livePointer(global) {
  const r = await ACTIONS.memPeek(global.toString(16) + ' 4');
  return r.error ? 0 : leHex(r.bytes, 0, 4);
}
async function liveText(at) {
  const s = await ACTIONS.memStrs(at.toString(16) + ' 4');
  return (s.texts && s.texts[0] && s.texts[0].text) || '';
}
async function liveRead() {
  const gm = await livePointer(LIVE.gameMgr);
  const w = await livePointer(LIVE.world);
  if (!gm || !w) return null;
  const money = await ACTIONS.memPeek(`${(gm + 0xc).toString(16)} 4`);
  const date = await ACTIONS.memPeek(`${(gm + 0x1194).toString(16)} 16`);
  const map = await ACTIONS.memPeek(`${(w + 0x34).toString(16)} 8`);
  const list = await ACTIONS.memEach(`${(w + 0x80).toString(16)} 0 128 2a8`);
  if (money.error || date.error || map.error) return null;
  const buf = new DataView(new Uint8Array(money.bytes.match(/../g).map((b) => parseInt(b, 16))).buffer);
  const out = {
    cash: buf.getFloat32(0, true),
    date: { year: leHex(date.bytes, 0, 2), month: leHex(date.bytes, 2, 2), weekday: leHex(date.bytes, 4, 2), day: leHex(date.bytes, 6, 2), hour: leHex(date.bytes, 8, 2), minute: leHex(date.bytes, 10, 2) },
    map: { width: leHex(map.bytes, 0, 4), height: leHex(map.bytes, 4, 4) },
    entities: list.error ? 0 : list.count,
    classes: {},
  };
  for (const o of list.objects || []) {
    const [vt, type, happy] = o.values;
    if (!LIVE.names[type]) LIVE.names[type] = [await liveText(parseInt(type, 16) + 0xa4), await liveText(parseInt(type, 16) + 0x98)];
    // (the knowledge base's class for a table not in the list: ZTKeeper...)
    const kbName = S.kb && Object.entries(S.kb.classes || {}).find(([, c]) => parseInt(c.vtable, 16) === parseInt(vt, 16));
    const cls = LIVE.classes[vt] || (kbName && kbName[0]) || 'Class ' + vt;
    const c = (out.classes[cls] = out.classes[cls] || { table: vt, count: 0, types: {} });
    c.count++;
    // (signed: an unhappy animal's is below 0)
    if (LIVE.happy.includes(cls)) c.happiness = (c.happiness || 0) + (parseInt(happy, 16) | 0);
    // (fence types have no short name: their kind is it - zoowall, wpicket)
    const name = LIVE.names[type][0] || LIVE.names[type][1] || type;
    c.types[name] = (c.types[name] || 0) + 1;
  }
  // (happiness as each class's average - summed above)
  for (const c of Object.values(out.classes)) if (c.happiness !== undefined) c.happiness = Math.round(c.happiness / c.count);
  return out;
}
const MONTHS = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'];
const WEEKDAYS = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'];
// (as the original shows it: "Jan, Year 1"; its date tooltip "Tue. Jan 9")
function liveDate(d) {
  return `${WEEKDAYS[d.weekday] || '?'} ${MONTHS[d.month - 1] || '?'} ${d.day}, Year ${d.year - 2000}`;
}
// Its part of the Play bar: kept up to date every 2 s while the original runs
async function livePoll() {
  clearTimeout(LIVE.timer);
  if (!S.play.running || S.play.kind !== 'original') return;
  const busy = REC.status && (REC.status.active || REC.status.learning);
  if (!busy) {
    const r = await liveRead().catch(() => null);
    if (r) LIVE.last = r;
    const el = $('orig-live');
    if (el && LIVE.last) {
      const l = LIVE.last;
      const who = (k, one, many) => {
        const c = l.classes[k];
        if (!c) return '';
        return ` · ${c.count} ${c.count === 1 ? one : many}${c.happiness !== undefined ? ` (happiness ${c.happiness})` : ''}`;
      };
      el.innerHTML = `${esc(liveDate(l.date))} · <b>$${Math.round(l.cash).toLocaleString()}</b>${who('Animals', 'animal', 'animals')}${who('Guests', 'guest', 'guests')} · ${l.entities} things`;
      el.title = `The original's own state, read from its memory:\n${Object.entries(l.classes)
        .map(([k, c]) => `${k}: ${c.count}`)
        .join('\n')}\nMap ${l.map.width}×${l.map.height}`;
    }
  }
  LIVE.timer = setTimeout(livePoll, 2000);
}

// "75000" or "float 75000" -> [type, value]
function memParse(arg) {
  const parts = String(arg || '').trim().split(/\s+/);
  return parts.length > 1 ? [parts[0], parts[1]] : [MEM.type, parts[0]];
}

function memShow() {
  const m = $('modal');
  const f = MEM.found;
  const places = f
    ? f.places
        .map(
          (p) =>
            `<tr><td class="mono">${esc(p.address)}</td><td class="muted">${esc(p.where)}</td><td>${
              MEM.watching === p.address
                ? '<span class="green">watching</span>'
                : `<span class="blue clickable" data-watch="${esc(p.address)}" title="Report each instruction that writes it">Watch writes</span>`
            }</td></tr>`,
        )
        .join('')
    : '';
  const writes = MEM.writes
    .map(
      (w) =>
        `<tr><td class="mono">${esc(w.address)}</td><td>${w.function ? `<span class="blue clickable" data-go="${esc(w.function.address)}">${esc(w.function.name)}</span>` : '<span class="muted">?</span>'} <span class="muted mono">at ${esc(
          w.after,
        )}</span></td><td class="muted">${w.from ? 'from ' + esc(w.from.name) : ''}</td><td class="muted mono" style="font-size:11px">${esc(
          Object.entries(w.regs)
            .map(([k, v]) => k + ' ' + v)
            .join('  '),
        )}</td></tr>`,
    )
    .join('');
  m.innerHTML = `<div class="dialog" style="width:min(980px,92vw);max-height:86vh;display:flex;flex-direction:column"><div class="h">Find a value in the original</div><div class="muted" style="margin-bottom:8px">Scan for a value you can see in the game (its cash: 75000). Close this, change it in the game (spend some), come back and Rescan for the new value: what's left is where it's kept. Then Watch writes on it, change it again, and see which code writes it.</div><div style="display:flex;gap:8px;align-items:center;margin-bottom:10px"><select class="field" id="mem-type" style="width:auto">${['int', 'float', 'double']
    .map((t) => `<option${t === MEM.type ? ' selected' : ''}>${t}</option>`)
    .join('')}</select><input class="field" id="mem-value" style="width:160px" value="${esc(MEM.value)}" placeholder="75000"><div class="btn primary" data-x="scan">Scan</div><div class="btn${f ? '' : ' disabled'}" data-x="rescan">Rescan</div><span class="muted">${
    f ? `${f.count} place${f.count === 1 ? '' : 's'}${f.count > f.places.length ? ` (the first ${f.places.length} shown)` : ''}` : ''
  }</span></div><div style="overflow:auto;flex:1">${places ? `<table class="rec-table">${places}</table>` : ''}${
    writes ? `<div class="label" style="margin-top:12px">Written by</div><table class="rec-table">${writes}</table>` : MEM.watching ? `<div class="muted" style="margin-top:12px">Watching ${esc(MEM.watching)}: change it in the game, then come back.</div>` : ''
  }</div><div class="btns">${MEM.watching ? '<div class="btn" data-x="unwatch">Stop watching</div>' : ''}<div class="btn" data-x="no">Close</div></div></div>`;
  m.classList.remove('hidden');
  sendPlayRect();
  m.onclick = async (e) => {
    const x = e.target.dataset.x;
    const go = e.target.closest('[data-go]');
    const watch = e.target.closest('[data-watch]');
    const read = () => {
      MEM.type = $('mem-type').value;
      MEM.value = $('mem-value').value.trim();
    };
    if (x === 'scan' || x === 'rescan') {
      read();
      await ACTIONS[x === 'scan' ? 'memScan' : 'memRescan'](x === 'scan' ? `${MEM.type} ${MEM.value}` : MEM.value);
      memShow();
    } else if (watch) {
      await ACTIONS.memWatch(watch.dataset.watch);
      memShow();
    } else if (x === 'unwatch') {
      await ACTIONS.memUnwatch();
      memShow();
    } else if (go || x === 'no' || e.target === m) {
      m.classList.add('hidden');
      m.innerHTML = '';
      sendPlayRect();
      if (go) ACTIONS.openCode(go.dataset.go);
    }
  };
}

// ---------------------------------------------------------- side by side
// The same thing in both games, day by day: the original's first animal (its
// happiness, +0x2a8, and its exhibit's +0xe8 - [0x63805c]+0x1c is the
// exhibits) and the Test Zoo's selected animal (its live values), each game
// day (12 s of play in both). Start both at the same moment - an animal just
// put in each - and see where they part
const SIDE = { on: false, timer: null, rows: [], startOrig: null, startOurs: null, days: 0, label: '' };
function dayOf(d) {
  // (days since 1 Jan 2001, with the hour: the original's date as a number)
  return (Date.UTC(d.year, d.month - 1, d.day, d.hour, d.minute) - Date.UTC(2001, 0, 1)) / 86400000;
}
async function sideOriginal() {
  const gm = await livePointer(LIVE.gameMgr);
  const w = await livePointer(LIVE.world);
  const hm = await livePointer(0x63805c);
  if (!gm || !w) return null;
  const date = await ACTIONS.memPeek(`${(gm + 0x1194).toString(16)} 16`);
  const animals = await ACTIONS.memEach(`${(w + 0x80).toString(16)} 0 2a8 only 62ff54`);
  // (each guest: its happiness +0x2a8 and the four needs zooRatingUpdate
  // checks, +0x2b0, +0x2b8, +0x2c0, +0x2c8)
  const guests = await ACTIONS.memEach(`${(w + 0x80).toString(16)} 0 2a8 2b0 2b8 2c0 2c8 only 630f88`);
  const exhibits = hm ? await ACTIONS.memEach(`${(hm + 0x1c).toString(16)} 0 e8`) : { objects: [] };
  if (date.error || animals.error) return null;
  const d = { year: leHex(date.bytes, 0, 2), month: leHex(date.bytes, 2, 2), day: leHex(date.bytes, 6, 2), hour: leHex(date.bytes, 8, 2), minute: leHex(date.bytes, 10, 2) };
  const a = animals.objects[0];
  const x = (exhibits.objects || [])[0];
  const gs = guests.error ? [] : guests.objects.map((g) => parseInt(g.values[1], 16) | 0);
  // (the first guest seen, followed by its address while it's in the zoo)
  if (!SIDE.guestAt && !guests.error && guests.objects.length) SIDE.guestAt = guests.objects[0].at;
  const fg = guests.error ? null : guests.objects.find((g) => g.at === SIDE.guestAt);
  const firstGuest = fg ? fg.values.slice(1).map((v) => parseInt(v, 16) | 0) : null;
  // (the zoo rating: the game manager's +0x2c - the rating object, its +0x10,
  // keeps it at +0x1c: zooRatingTerms 0x41fcc1)
  const rating = await ACTIONS.memPeek(`${(gm + 0x2c).toString(16)} 4`);
  return {
    day: dayOf(d),
    happiness: a ? parseInt(a.values[1], 16) | 0 : null,
    exhibit: x ? parseInt(x.values[1], 16) | 0 : null,
    guests: gs.length,
    guestHappiness: gs.length ? Math.round(gs.reduce((t, v) => t + v, 0) / gs.length) : null,
    rating: rating.error ? null : leHex(rating.bytes, 0, 4) | 0,
    firstGuest,
  };
}
function sideOurs() {
  const s = sc() || {};
  const a = s.sel && s.sel.kind === 'animal' ? s.sel : null;
  // (a guest selected in the Test Zoo: followed as the original's first)
  const g = s.sel && s.sel.kind === 'guest' ? s.sel : null;
  const guest = g ? [g.happiness, g.hunger, g.thirst, g.bathroom, g.tired].map((v) => (v === undefined ? null : Math.round(v))) : null;
  return { guest, day: (s.clock || 0) / 12, happiness: a ? a.happiness : null, hunger: a ? a.hunger : null, suitability: a ? a.suitability : null, guests: s.counts ? s.counts.guests : null, guestHappiness: s.counts ? s.counts.guestHappiness : null };
}
async function sideTick() {
  if (!SIDE.on) return;
  const o = await sideOriginal().catch(() => null);
  // (the Test Zoo keeps no books: its guests come by the original's rating)
  if (o && o.rating !== null && o.rating !== SIDE.lastRating) {
    lab.engineSend({ cmd: 'economy', rating: o.rating });
    SIDE.lastRating = o.rating;
  }
  const u = sideOurs();
  if (o && SIDE.startOrig === null) SIDE.startOrig = o.day;
  if (SIDE.startOurs === null) SIDE.startOurs = u.day;
  const od = o ? Math.floor(o.day - SIDE.startOrig) : -1;
  const ud = Math.floor(u.day - SIDE.startOurs);
  // (a row a day of either: what both were then)
  const last = SIDE.rows[SIDE.rows.length - 1];
  if (!last || od > last.origDay || ud > last.oursDay)
    SIDE.rows.push({ origDay: od, oursDay: ud, original: o ? { happiness: o.happiness, exhibit: o.exhibit, guests: o.guests, guestHappiness: o.guestHappiness, rating: o.rating, firstGuest: o.firstGuest } : null, ours: { happiness: u.happiness, hunger: u.hunger, suitability: u.suitability, guests: u.guests, guestHappiness: u.guestHappiness, guest: u.guest } });
  if (Math.min(od, ud) >= SIDE.days) return ACTIONS.sideStop();
  SIDE.timer = setTimeout(sideTick, 1000);
}
// Both happiness curves, day by day (an SVG chart)
function sideChart(rows) {
  const W = 760, H = 260, pad = 30;
  const maxDay = Math.max(1, ...rows.map((r) => Math.max(r.origDay, r.oursDay)));
  const X = (d) => pad + (d / maxDay) * (W - 2 * pad);
  const Y = (v) => H / 2 - (v / 100) * (H / 2 - pad);
  const line = (pick, color) => {
    const pts = rows.map((r) => pick(r)).filter((p) => p && p[1] !== null && p[1] !== undefined && p[0] >= 0);
    return pts.length ? `<polyline fill="none" stroke="${color}" stroke-width="2" points="${pts.map(([d, v]) => `${X(d).toFixed(1)},${Y(v).toFixed(1)}`).join(' ')}"/>` : '';
  };
  const grid = [-100, -50, 0, 50, 100].map((v) => `<line x1="${pad}" x2="${W - pad}" y1="${Y(v)}" y2="${Y(v)}" stroke="var(--bg3)"/><text x="4" y="${Y(v) + 4}" font-size="10" fill="var(--text2)">${v}</text>`).join('');
  return `<svg viewBox="0 0 ${W} ${H}" style="width:100%;height:auto">${grid}${line((r) => [r.origDay, r.original && r.original.happiness], '#e8a33d')}${line((r) => [r.oursDay, r.ours.happiness], '#4cc38a')}</svg><div class="muted"><span style="color:#e8a33d">■</span> the original &nbsp; <span style="color:#4cc38a">■</span> our engine - happiness by game day</div>`;
}

Object.assign(ACTIONS, {
  // Side by side: "<days> <what's being compared>" - starts the Test Zoo's
  // clock too (an animal must be selected there, and one in the original)
  sideStart: async (arg) => {
    const m = String(arg || '').match(/^\s*(\d+)\s*(.*)$/);
    Object.assign(SIDE, { on: true, rows: [], startOrig: null, startOurs: null, lastRating: null, guestAt: null, days: m ? +m[1] : 20, label: (m && m[2]) || 'Side by side' });
    lab.engineSend({ cmd: 'play', on: true });
    sideTick();
    return { started: true, days: SIDE.days };
  },
  sideStop: async () => {
    clearTimeout(SIDE.timer);
    if (!SIDE.on) return { rows: SIDE.rows };
    SIDE.on = false;
    lab.engineSend({ cmd: 'play', on: false });
    const run = { label: SIDE.label, on: new Date().toISOString(), rows: SIDE.rows };
    const all = store.get('sideRuns', []);
    all.unshift(run);
    store.set('sideRuns', all.slice(0, 20));
    return run;
  },
  // The last run (or the one at index arg), charted
  sideShow: async (arg) => {
    const run = SIDE.on ? { label: SIDE.label + ' (running)', rows: SIDE.rows } : store.get('sideRuns', [])[+arg || 0];
    if (!run) return toast('No side-by-side run yet.', 3000);
    const m = $('modal');
    const rows = run.rows
      .map(
        (r) =>
          `<tr><td class="mono">${Math.max(r.origDay, r.oursDay)}</td><td class="mono">${r.original ? r.original.happiness : '-'}</td><td class="mono muted">${r.original ? r.original.exhibit : '-'}</td><td class="mono">${r.ours.happiness ?? '-'}</td><td class="mono muted">${r.ours.hunger ?? '-'}</td><td class="mono muted">${r.ours.suitability ?? '-'}</td><td class="mono">${r.original ? r.original.guests ?? '-' : '-'}</td><td class="mono muted">${r.original ? r.original.guestHappiness ?? '-' : '-'}</td><td class="mono">${r.ours.guests ?? '-'}</td><td class="mono muted">${r.ours.guestHappiness ?? '-'}</td><td class="mono muted">${r.original ? r.original.rating ?? '-' : '-'}</td></tr>`,
      )
      .join('');
    m.innerHTML = `<div class="dialog" style="width:min(860px,92vw);max-height:88vh;display:flex;flex-direction:column"><div class="h">${esc(run.label)}</div>${sideChart(run.rows)}<div style="overflow:auto;flex:1;margin-top:8px"><table class="rec-table"><tr class="muted"><td>day</td><td>original happiness</td><td>its exhibit +0xe8</td><td>ours happiness</td><td>ours hunger</td><td>ours suitability</td><td>original guests</td><td>their happiness</td><td>ours guests</td><td>their happiness</td><td>zoo rating (both)</td></tr>${rows}</table></div><div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
    m.classList.remove('hidden');
    sendPlayRect();
    m.onclick = (e) => {
      if (e.target.dataset.x === 'no' || e.target === m) {
        m.classList.add('hidden');
        m.innerHTML = '';
        sendPlayRect();
      }
    };
    return run;
  },
  // The original's state now (live): its date, cash, map, every entity
  // counted by class and type
  origLive: async () => (await liveRead()) || { error: 'Nothing to read: is a zoo open in the original?' },
  // What's in its zoo, by class and type
  origCensus: async () => {
    const l = await liveRead();
    if (!l) return toast('Nothing to read: open a zoo in the original first.', 4000);
    const m = $('modal');
    m.innerHTML = `<div class="dialog" style="width:min(820px,92vw);max-height:86vh;display:flex;flex-direction:column"><div class="h">What's in the original's zoo</div><div class="muted" style="margin-bottom:8px">${esc(liveDate(l.date))} · $${Math.round(
      l.cash,
    ).toLocaleString()} · map ${l.map.width}×${l.map.height} · ${l.entities} things - read from its memory as it plays.</div><div style="overflow:auto;flex:1"><table class="rec-table">${Object.entries(l.classes)
      .map(
        ([k, c]) =>
          `<tr><td><b>${esc(k)}</b><div class="muted mono" style="font-size:11px">${esc(c.table)}</div></td><td class="mono">${c.count}${c.happiness !== undefined ? `<div class="muted" style="font-size:11px">happiness ${c.happiness}</div>` : ''}</td><td>${Object.entries(c.types)
            .sort((a, b) => b[1] - a[1])
            .map(([t, n]) => `${esc(t)} <span class="muted">×${n}</span>`)
            .join(', ')}</td></tr>`,
      )
      .join('')}</table></div><div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
    m.classList.remove('hidden');
    sendPlayRect();
    m.onclick = (e) => {
      if (e.target.dataset.x === 'no' || e.target === m) {
        m.classList.add('hidden');
        m.innerHTML = '';
        sendPlayRect();
      }
    };
    return l;
  },
  memOpen: async () => {
    if (MEM.watching) MEM.writes = await lab.origWrites();
    memShow();
  },
  // ("75000", or "float 75000")
  memScan: async (arg) => {
    const [type, value] = memParse(arg);
    MEM.type = type;
    MEM.value = value;
    const r = await lab.origFind(`scan ${type} ${value}`);
    if (r.error) return toast(esc(r.error), 5000);
    MEM.found = r;
    return r;
  },
  memRescan: async (value) => {
    MEM.value = String(value).trim();
    const r = await lab.origFind(`rescan ${MEM.value}`);
    if (r.error) return toast(esc(r.error), 5000);
    MEM.found = r;
    return r;
  },
  // ("<address> <bytes>")
  memPeek: async (arg) => {
    const [at, n] = String(arg).trim().split(/\s+/);
    const r = await lab.origAsk(`peek ${at.replace(/^0x/, '')} ${n || 16}`, 'peek ');
    return r.error ? r : { bytes: r.line.split(' ')[2] };
  },
  // ("<slots> <address> <address>...": class function tables - each table's
  // first <slots> entries as addresses, to compare what each class replaces)
  // ("uniq <slots> <address>...": just every different entry, once - to make
  // them all functions at once with makeFns)
  // A 4-byte number written into the running original ("0x1234 100"): a
  // test sets a type's numbers to see what the game then does (Hook.cpp poke)
  memPoke: async (arg) => {
    const [at, v] = String(arg || '').trim().split(/[\s,]+/);
    if (!at || v === undefined) return { error: 'memPoke "0xADDRESS value"' };
    const r = await lab.origAsk(`poke ${at.replace(/^0x/, '')} ${parseInt(v, 10)}`, 'poke ');
    if (r.error) return r;
    return { ok: / ok$/.test(r.line), line: r.line };
  },
  memTables: async (arg) => {
    let parts = String(arg).trim().split(/\s+/);
    const uniq = parts[0] === 'uniq';
    if (uniq) parts = parts.slice(1);
    const [n, ...ats] = parts;
    const slots = Math.min(+n || 32, 320);
    const tables = {};
    for (const at of ats) {
      const r = await ACTIONS.memPeek(`${at} ${slots * 4}`);
      if (r.error || !r.bytes) {
        tables[at] = r.error || 'unreadable';
        continue;
      }
      const words = [];
      for (let i = 0; i + 8 <= r.bytes.length; i += 8) {
        const b = r.bytes.slice(i, i + 8).match(/../g).reverse().join('');
        words.push('0x' + b.replace(/^0+/, '').padStart(6, '0'));
      }
      tables['0x' + at.replace(/^0x/, '')] = words;
    }
    if (uniq) return { entries: [...new Set(Object.values(tables).filter(Array.isArray).flat())].sort().join(' ') };
    return { tables };
  },
  // ("<address> [bytes]": the text stored right there - a name the code
  // compares against, "fRun" - up to its end; memStrs follows pointers)
  memText: async (arg) => {
    const [at, n] = String(arg).trim().split(/\s+/);
    const r = await ACTIONS.memPeek(`${at} ${n || 64}`);
    if (r.error || !r.bytes) return r;
    let text = '';
    for (let i = 0; i + 1 < r.bytes.length; i += 2) {
      const c = parseInt(r.bytes.slice(i, i + 2), 16);
      if (!c) break;
      text += c >= 32 && c < 127 ? String.fromCharCode(c) : '.';
    }
    return { at: '0x' + at.replace(/^0x/, ''), text };
  },
  // ("<list> <offset>...", hex: a begin/end pair of object pointers - each
  // object with the 4 bytes at each offset)
  // ("... only <table>": just the objects whose first value is that - a
  // class's function table, with 0 the first offset)
  memEach: async (arg) => {
    let text = String(arg).trim();
    let only = '';
    const m = text.match(/\s+only\s+(0x)?([0-9a-f]+)$/i);
    if (m) {
      only = '0x' + m[2].toLowerCase();
      text = text.slice(0, m.index);
    }
    const parts = text.split(/\s+/).map((x) => x.replace(/^0x/, ''));
    const r = await lab.origAsk('each ' + parts.join(' '), 'each ');
    if (r.error) return r;
    const [, n, ...objs] = r.line.split(' ');
    if (n === '-') return { error: 'Not a list there.' };
    const objects = objs
      .map((o) => {
        const [at, vals] = o.split(':');
        return { at: '0x' + at, values: (vals || '').split(',').filter(Boolean).map((v) => '0x' + v) };
      })
      .filter((o) => !only || o.values[0] === only);
    return { count: +n, objects };
  },
  // ("<address> <bytes>": each field in it pointing at text)
  memStrs: async (arg) => {
    const [at, n] = String(arg).trim().split(/\s+/);
    const r = await lab.origAsk(`strs ${at.replace(/^0x/, '')} ${n || 256}`, 'strs');
    if (r.error) return r;
    return { texts: r.line.split(' ').slice(1).map((x) => ({ off: '0x' + x.split('=')[0], text: x.split('=').slice(1).join('=').replace(/_/g, ' ') })) };
  },
  // The zoo's entities in the original, counted: the world ([0x638040]) and
  // its list (+0x80), each by its class (its function table, +0) and type
  // (+0x128), each type named by the text it points at
  memCensus: async () => {
    const world = await ACTIONS.memPeek('638040 4');
    if (world.error) return world;
    const w = parseInt(world.bytes.match(/../g).reverse().join(''), 16);
    const list = await ACTIONS.memEach(`${(w + 0x80).toString(16)} 0 128`);
    if (list.error) return list;
    const classes = {};
    for (const o of list.objects) {
      const [vt, type] = o.values;
      const c = (classes[vt] = classes[vt] || { count: 0, types: {} });
      c.count++;
      c.types[type] = (c.types[type] || 0) + 1;
    }
    for (const c of Object.values(classes)) {
      const named = [];
      for (const [type, count] of Object.entries(c.types)) {
        const s = await ACTIONS.memStrs(`${type} 512`);
        const texts = (s.texts || []).map((t) => t.off + ' ' + t.text);
        named.push({ type, count, texts: texts.slice(0, 6) });
      }
      c.types = named;
    }
    return { world: '0x' + w.toString(16), entities: list.count, classes };
  },
  // Lines to the game in Play, one after another, with waits:
  // "click 952 462; wait 1500; click 644 470" (the original's hook, or our
  // engine: the same click lines)
  playSteps: async (arg) => {
    for (const step of String(arg).split(';').map((s) => s.trim()).filter(Boolean)) {
      const wait = step.match(/^wait\s+(\d+)/);
      if (wait) await new Promise((r) => setTimeout(r, +wait[1]));
      else {
        lab.playCmd(step);
        await new Promise((r) => setTimeout(r, 400));
      }
    }
    return { done: true };
  },
  playWindowCheck: () => lab.playWindowCheck(),
  // Windows' own dialogs the original opens (Save a zoo..., Open a zoo...):
  // not drawn on its surface, and it waits on them. "" names the one open,
  // "text <name>" fills its file name, "ok" / "cancel" presses its button
  origDialog: async (arg) => lab.origAsk(('dialog ' + (arg || '')).trim(), 'dialog'),
  // A key to the original: its virtual-key code (27 Esc, 13 Enter, 32 Space)
  origKey: async (vk) => lab.origAsk('key ' + parseInt(vk, 10), 'done key'),
  // Pictures of the original one after another, as it plays - a clip to look
  // through frame by frame ("<folder> <count> <ms between>": f000.png...)
  origFrames: async (arg) => {
    const [dir, n, ms] = String(arg || '').trim().split(/\s+/);
    if (!dir) return { error: 'origFrames "<folder> [count] [ms]"' };
    const count = Math.min(+n || 20, 600), gap = Math.max(+ms || 500, 0);
    const files = [];
    for (let i = 0; i < count; i++) {
      const t = Date.now();
      const r = await lab.playShot(`${dir.replace(/[\\/]$/, '')}/f${String(i).padStart(3, '0')}.png`);
      if (r.error) return { error: r.error, files };
      files.push(r.file);
      await new Promise((res) => setTimeout(res, Math.max(0, gap - (Date.now() - t))));
    }
    return { count: files.length, first: files[0], last: files[files.length - 1] };
  },
  memWatch: async (at) => {
    await lab.origWrites(true);
    MEM.writes = [];
    const r = await lab.origAsk(`watch ${String(at).replace(/^0x/, '')}`, 'watching');
    if (!r.error) MEM.watching = '0x' + String(at).replace(/^0x/, '').padStart(6, '0');
    return r;
  },
  memUnwatch: async () => {
    MEM.writes = await lab.origWrites();
    const r = await lab.origAsk('unwatch', 'unwatched');
    MEM.watching = '';
    return r;
  },
  // What the watch has caught so far ("clear" to start again)
  memWrites: async (arg) => {
    const writes = await lab.origWrites(arg === 'clear');
    MEM.writes = writes;
    return { writes };
  },
});
