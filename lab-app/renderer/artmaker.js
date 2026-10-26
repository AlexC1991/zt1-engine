// ZT1 Lab: the art maker - Mods > a mod > Art. A mod's art designed in the
// Lab (re/lab/artmaker.py reads and saves it, in the mod's .ztd):
//  - its animations, each playing in every direction, frame by frame
//  - its palette: a click on the animal (or a swatch) picks a colour, and
//    changing it changes it everywhere it's used - one palette colours all
//    of an animal's animations, so one change recolours the whole animal
//  - paint: a pencil with one of the palette's colours, on a frame
//  - a pattern faded along the body: picked colours turned into another,
//    from the head back, in every frame of every animation (worked out for
//    each facing) - the quagga's stripes fading out towards its tail
//  - PNGs: every frame out to paint elsewhere, and back in
// (loaded after modkit.js)

const AM = {
  folder: null, // the mod
  anims: null, // [{ani, folder, dirs}]
  q: '',
  ani: null,
  data: null, // the animation: {folder, ms, dirs: [{name, pal, frames: [{w,h,ox,oy,shadow,px,mask}]}]}
  pals: {}, // name -> ["#rrggbb" x256], as changed here
  saved: {}, // name -> as in the mod (or the game)
  dirtyPal: new Set(),
  dirtyFrames: {}, // dir -> Set of frame numbers
  dir: 0,
  frame: 0,
  playing: true,
  zoom: 4,
  bg: store.get('amBg', 'checker'),
  tool: 'pick', // pick | paint
  sel: new Set(), // palette indices picked
  brush: 1,
  fade: { to: null, start: 0.35, end: 0.85 },
  stripes: { start: 0.45, end: 0.95, count: 6, width: 0.45, darkness: 0.45, depth: 0.6 },
  dark: 30, // (pick by shade: darker than this, in %)
  undo: [],
  timer: null,
  busy: '',
  hover: null,
};

const amB64 = (s) => Uint8Array.from(atob(s), (c) => c.charCodeAt(0));
const amToB64 = (a) => {
  let s = '';
  for (let i = 0; i < a.length; i += 0x8000) s += String.fromCharCode.apply(null, a.subarray(i, i + 0x8000));
  return btoa(s);
};
const amDir = () => (AM.data ? AM.data.dirs[Math.min(AM.dir, AM.data.dirs.length - 1)] : null);
const amPal = () => {
  const d = amDir();
  return d ? AM.pals[d.pal] : null;
};
const amDirty = () => AM.dirtyPal.size > 0 || Object.values(AM.dirtyFrames).some((s) => s.size);

// ---------------------------------------------------------------- the page
async function artPage(m) {
  if (AM.folder !== m.folder) {
    if (amDirty() && !confirm('Leave this art without saving it?')) return '';
    Object.assign(AM, { folder: m.folder, anims: null, ani: null, data: null, pals: {}, saved: {}, dirtyPal: new Set(), dirtyFrames: {}, sel: new Set(), undo: [] });
  }
  if (!AM.anims) {
    const r = await lab.artDo(m.folder, { op: 'anims' });
    AM.anims = r.ok ? r.anims : [];
    AM.error = r.ok ? '' : r.error;
  }
  return `<div class="am" id="am">${amHtml()}</div>`;
}
function artMounted() {
  amDraw();
  amPlay();
}
function amRender() {
  const el = $('am');
  if (!el) return;
  const list = el.querySelector('.amlist');
  const top = list ? list.scrollTop : 0;
  el.innerHTML = amHtml();
  const l2 = el.querySelector('.amlist');
  if (l2) l2.scrollTop = top;
  amDraw();
}

// (a slider row for the stripes: its id, label, value and how it's kept)
const AM_SLIDERS = { 'am-ss': ['start', 100], 'am-se': ['end', 100], 'am-sn': ['count', 1], 'am-sw': ['width', 100], 'am-sd': ['darkness', 100], 'am-sp': ['depth', 100] };
function amSlider(id, label, value, min, max, scale) {
  const v = Math.round(value * scale);
  return `<div class="amrow"><span class="dim" style="width:96px">${label}</span><input type="range" id="${id}" min="${min}" max="${max}" value="${v}"><span class="mono dim">${v}</span></div>`;
}

function amHtml() {
  if (AM.error) return `<div class="red" style="padding:20px">${esc(AM.error)}</div>`;
  if (!AM.anims.length)
    return `<div class="stagemsg"><b>No art in this mod yet</b><span>The art maker works on a mod's own animations. Make a new animal (Write Code › Game files › an animal's .ai › New species from this) or copy art in, then design it here.</span></div>`;
  const q = AM.q.trim().toLowerCase();
  const rows = AM.anims
    .filter((a) => !q || a.ani.toLowerCase().includes(q))
    .map((a) => `<div class="item amitem${a.ani === AM.ani ? ' sel' : ''}" data-act="amOpen" data-arg="${esc(a.ani)}" title="${esc(a.ani)}${a.inMod ? '' : ' - the game’s own: saving a change puts a copy in the mod'}"><span class="mono">${esc(a.folder.split('/').slice(2).join('/') || a.folder)}</span><span class="dim">${a.inMod ? '' : 'game · '}${a.dirs.length}</span></div>`)
    .join('');
  return `<div class="amleft"><input class="search" id="am-q" placeholder="Find an animation" value="${esc(AM.q)}"><div class="amlist">${rows}</div></div>
    <div class="amcentre">${amToolbar()}<div class="amstage ${AM.bg}" id="am-stage"><canvas id="am-canvas"></canvas></div>${amStatus()}</div>
    <div class="amright">${amSide()}</div>`;
}

function amToolbar() {
  if (!AM.data) return '<div class="amtools"><span class="muted">Pick an animation on the left.</span></div>';
  const d = amDir();
  const dirs = AM.data.dirs.map((x, i) => `<div class="${i === AM.dir ? 'on' : ''}" data-act="amDir" data-arg="${i}">${esc(x.name)}</div>`).join('');
  const tool = (k, label, tip) => `<div class="${AM.tool === k ? 'on' : ''}" data-act="amTool" data-arg="${k}" title="${tip}">${label}</div>`;
  return `<div class="amtools"><div class="seg">${dirs}</div>
    <div class="btn small ghost" data-act="amPlayToggle">${AM.playing ? '❚❚' : '▶'}</div>
    <div class="btn small ghost" data-act="amStep" data-arg="-1">‹</div><span class="mono dim">${AM.frame + 1}/${d.frames.length}</span><div class="btn small ghost" data-act="amStep" data-arg="1">›</div>
    <div class="seg">${tool('pick', 'Pick colours', 'Click the art: picks its colour (Shift adds more)')}${tool('paint', 'Paint', 'Paint this frame with the brush colour (right button: clear)')}</div>
    <div class="spacer"></div>
    <div class="btn small ghost" data-act="amZoom" data-arg="-1">−</div><span class="mono dim">${AM.zoom}×</span><div class="btn small ghost" data-act="amZoom" data-arg="1">+</div>
    <div class="btn small ghost" data-act="amBg" title="The background">◩</div></div>`;
}

function amStatus() {
  const h = AM.hover;
  const pal = amPal();
  return `<div class="amstatus mono">${
    h && pal ? `${h.x}, ${h.y} · ${h.i === null ? 'clear' : `colour ${h.i} <span class="amchip" style="background:${pal[h.i]}"></span> ${pal[h.i]}`}` : AM.data ? esc(AM.data.folder) : ''
  }${AM.busy ? ` · <span class="green-text">${esc(AM.busy)}</span>` : ''}</div>`;
}

// (which palette entries the animation uses, and how often)
function amCounts() {
  const n = new Uint32Array(256);
  if (!AM.data) return n;
  const d = amDir();
  for (const x of AM.data.dirs) {
    if (x.pal !== d.pal) continue;
    for (const f of x.frames) {
      if (f.shadow) continue;
      for (let i = 0; i < f.px.length; i++) if (f.mask[i]) n[f.px[i]]++;
    }
  }
  return n;
}

function amSide() {
  if (!AM.data) return '';
  const pal = amPal();
  const n = amCounts();
  const sw = pal
    .map((c, i) => `<div class="amsw${n[i] ? '' : ' unused'}${AM.sel.has(i) ? ' sel' : ''}${AM.brush === i ? ' brush' : ''}${AM.fade.to === i ? ' to' : ''}" style="background:${c}" data-act="amSwatch" data-arg="${i}" title="${i}: ${c}${n[i] ? ` · ${n[i]} pixels` : ' · not used here'}"></div>`)
    .join('');
  const sel = [...AM.sel];
  const one = sel.length === 1 ? pal[sel[0]] : '';
  const dirty = amDirty();
  return `<div class="amsec"><div class="label">Palette <span class="dim">${esc(amDir().pal)}</span></div><div class="amgrid">${sw}</div>
      <div class="dim" style="font-size:11.5px">Click a swatch or the art to pick a colour; Shift adds. Alt-click: the brush colour.</div></div>
    <div class="amsec"><div class="label">Picked <span class="dim">${sel.length ? sel.length + ' colour' + (sel.length > 1 ? 's' : '') : 'none'}</span></div>
      ${
        sel.length
          ? `<div class="amrow"><input type="color" id="am-colour" value="${one || '#888888'}" title="Set the picked colour${sel.length > 1 ? 's' : ''} to this"><div class="btn small" data-act="amSetColour">Set</div>
             <div class="btn small ghost" data-act="amSimilar" title="Every colour used near the picked ones in shade">+ similar</div><div class="btn small ghost" data-act="amSelClear">Clear</div></div>
             <div class="amrow"><span class="dim">Hue</span><input type="range" id="am-hue" min="-180" max="180" value="0"><span class="dim">Light</span><input type="range" id="am-light" min="-60" max="60" value="0"><div class="btn small" data-act="amShift">Shift</div></div>`
          : '<div class="dim" style="font-size:12px">Pick the colours to change - a whole part (a mane, stripes) is usually a few shades.</div>'
      }
      <div class="amrow"><span class="dim">By shade: every colour used here</span><input type="range" id="am-dark" min="2" max="98" value="${AM.dark}"><span class="mono dim" id="am-darkv">${AM.dark}%</span><div class="btn small ghost" data-act="amPickDark" title="Pick every colour used here that's darker than this (stripes, a mane - all their shades at once)">Darker</div><div class="btn small ghost" data-act="amPickLight" title="Pick every colour used here that's lighter than this (a coat's bright shades)">Lighter</div></div></div>
    <div class="amsec"><div class="label">Pattern: fade along the body</div>
      <div class="dim" style="font-size:11.5px">The picked colours turn into the <b>to</b> colour from the head back - in every frame of every animation of this animal, for each way it faces.</div>
      <div class="amrow"><div class="btn small${sel.length ? '' : ' disabled'}" data-act="amFadeTo" title="Then click the colour they become">To: ${AM.fade.to === null ? 'pick…' : `<span class="amchip" style="background:${pal[AM.fade.to]}"></span>${AM.fade.to}`}</div></div>
      <div class="amrow"><span class="dim">Starts</span><input type="range" id="am-fs" min="0" max="100" value="${Math.round(AM.fade.start * 100)}"><span class="dim">All by</span><input type="range" id="am-fe" min="0" max="100" value="${Math.round(AM.fade.end * 100)}"></div>
      <div class="btn small${sel.length && AM.fade.to !== null ? '' : ' disabled'}" data-act="amFade">Fade it in every frame</div></div>
    <div class="amsec"><div class="label">Pattern: stripes across the body</div>
      <div class="dim" style="font-size:11.5px">Bands of the picked colours made darker (each its own shade, darker) across the body, on its back - in every frame of every animation, for each way it faces.</div>
      ${amSlider('am-ss', 'From', AM.stripes.start, 0, 100, 100)}${amSlider('am-se', 'To', AM.stripes.end, 0, 100, 100)}
      ${amSlider('am-sn', 'Stripes', AM.stripes.count, 1, 16, 1)}${amSlider('am-sw', 'Width', AM.stripes.width, 10, 90, 100)}
      ${amSlider('am-sd', 'Darker', AM.stripes.darkness, 10, 90, 100)}${amSlider('am-sp', 'Down the body', AM.stripes.depth, 10, 100, 100)}
      <div class="btn small${sel.length ? '' : ' disabled'}" data-act="amStripes">Stripe every frame</div></div>
    <div class="amsec"><div class="label">Save</div>
      <div class="amrow"><div class="btn small primary${dirty ? '' : ' disabled'}" data-act="amSave">Save into the mod</div><div class="btn small ghost${AM.undo.length ? '' : ' disabled'}" data-act="amUndo">Undo</div></div>
      <div class="amrow"><div class="btn small ghost" data-act="amExport" title="Every frame of this animation as a PNG, to paint in another program">Frames out (PNG)</div><div class="btn small ghost" data-act="amImport" title="The PNGs back in, each pixel the palette's nearest colour">Frames in</div></div>
      <div class="btn small" data-act="mkTryMod" data-arg="${esc(AM.folder)}">▶ Try in the Test Zoo</div></div>`;
}

// ---------------------------------------------------------------- drawing
function amBounds(d) {
  let l = 0, t = 0, r = 1, b = 1;
  for (const f of d.frames) {
    l = Math.min(l, -f.ox);
    t = Math.min(t, -f.oy);
    r = Math.max(r, f.w - f.ox);
    b = Math.max(b, f.h - f.oy);
  }
  return { l, t, w: r - l, h: b - t };
}
function amDraw() {
  const cv = $('am-canvas');
  const d = amDir();
  if (!cv || !d) return;
  const f = d.frames[AM.frame % d.frames.length];
  const B = amBounds(d);
  const z = AM.zoom;
  cv.width = B.w * z;
  cv.height = B.h * z;
  const g = cv.getContext('2d');
  g.imageSmoothingEnabled = false;
  // (an empty frame - a building's moment with nothing on top: drawn as
  // nothing)
  if (!f.w || !f.h) return;
  const img = g.createImageData(f.w, f.h);
  const pal = AM.pals[d.pal];
  const rgb = pal.map((c) => [parseInt(c.slice(1, 3), 16), parseInt(c.slice(3, 5), 16), parseInt(c.slice(5, 7), 16)]);
  for (let i = 0; i < f.px.length; i++) {
    if (!f.mask[i]) continue;
    const c = f.shadow ? [0, 0, 0] : rgb[f.px[i]];
    const lit = !f.shadow && AM.sel.has(f.px[i]) && AM.blink;
    img.data[i * 4] = lit ? 255 : c[0];
    img.data[i * 4 + 1] = lit ? 64 : c[1];
    img.data[i * 4 + 2] = lit ? 255 : c[2];
    img.data[i * 4 + 3] = f.shadow ? 110 : 255;
  }
  const tmp = document.createElement('canvas');
  tmp.width = f.w;
  tmp.height = f.h;
  tmp.getContext('2d').putImageData(img, 0, 0);
  g.drawImage(tmp, (-f.ox - B.l) * z, (-f.oy - B.t) * z, f.w * z, f.h * z);
  AM.at = { f, x0: (-f.ox - B.l) * z, y0: (-f.oy - B.t) * z };
}
function amPlay() {
  clearInterval(AM.timer);
  const d = amDir();
  if (!d) return;
  AM.timer = setInterval(() => {
    if (!$('am-canvas')) return clearInterval(AM.timer);
    AM.tick = (AM.tick || 0) + 1;
    // (the picked colours blink, so you see where they are)
    const blink = AM.sel.size > 0 && AM.tick % 6 < 2 && Date.now() > (AM.quiet || 0);
    const step = AM.playing && d.frames.length > 1;
    if (!step && blink === !!AM.blink) return;
    AM.blink = blink;
    if (step) AM.frame = (AM.frame + 1) % d.frames.length;
    amDraw();
    const fr = document.querySelector('.amtools .mono.dim');
    if (fr) fr.textContent = `${AM.frame + 1}/${d.frames.length}`;
  }, Math.max(60, AM.data.ms || 100));
}

// The frame's pixel under the mouse
function amPixel(e) {
  const cv = $('am-canvas');
  if (!cv || !AM.at) return null;
  const r = cv.getBoundingClientRect();
  const sx = ((e.clientX - r.left) / r.width) * cv.width;
  const sy = ((e.clientY - r.top) / r.height) * cv.height;
  const x = Math.floor((sx - AM.at.x0) / AM.zoom), y = Math.floor((sy - AM.at.y0) / AM.zoom);
  const f = AM.at.f;
  if (x < 0 || y < 0 || x >= f.w || y >= f.h) return null;
  const k = y * f.w + x;
  return { x, y, k, i: f.mask[k] ? f.px[k] : null };
}
function amPaintAt(e, clear) {
  const p = amPixel(e);
  const d = amDir();
  if (!p || !d) return;
  const n = AM.frame % d.frames.length;
  const f = d.frames[n];
  if (f.shadow) return;
  if (!AM.stroke) {
    AM.stroke = true;
    AM.undo.push({ kind: 'frame', dir: AM.dir, n, px: f.px.slice(), mask: f.mask.slice() });
  }
  f.px[p.k] = clear ? 0 : AM.brush;
  f.mask[p.k] = clear ? 0 : 1;
  (AM.dirtyFrames[d.name] = AM.dirtyFrames[d.name] || new Set()).add(n);
  amDraw();
}
document.addEventListener('mousedown', (e) => {
  // (actions aren't given the click: its Shift and Alt kept here for them)
  AM.keys = { shift: e.shiftKey, alt: e.altKey };
  if (e.target.id !== 'am-canvas') return;
  const p = amPixel(e);
  if (AM.tool === 'paint') {
    AM.playing = false;
    AM.painting = e.button === 2 ? 'clear' : 'paint';
    AM.stroke = false;
    amPaintAt(e, AM.painting === 'clear');
    return;
  }
  if (!p || p.i === null) return;
  if (e.altKey) AM.brush = p.i;
  else amPick(p.i, e.shiftKey);
});
document.addEventListener('mousemove', (e) => {
  if (AM.painting) return amPaintAt(e, AM.painting === 'clear');
  if (e.target.id !== 'am-canvas') return;
  AM.hover = amPixel(e);
  const s = document.querySelector('.amstatus');
  if (s) s.outerHTML = amStatus();
});
document.addEventListener('mouseup', () => {
  if (!AM.painting) return;
  AM.painting = null;
  amRender();
});
document.addEventListener('contextmenu', (e) => e.target.id === 'am-canvas' && e.preventDefault());
document.addEventListener('input', (e) => {
  if (e.target.id === 'am-q') {
    AM.q = e.target.value;
    const el = document.querySelector('.amlist');
    if (el) {
      const caret = e.target.selectionStart;
      amRender();
      const q = $('am-q');
      q.focus();
      q.setSelectionRange(caret, caret);
    }
  } else if (e.target.id === 'am-dark') {
    AM.dark = +e.target.value;
    const v = $('am-darkv');
    if (v) v.textContent = AM.dark + '%';
  } else if (AM_SLIDERS[e.target.id]) {
    const [key, scale] = AM_SLIDERS[e.target.id];
    AM.stripes[key] = e.target.value / scale;
    const v = e.target.nextElementSibling;
    if (v) v.textContent = String(+e.target.value);
  } else if (e.target.id === 'am-fs') AM.fade.start = e.target.value / 100;
  else if (e.target.id === 'am-fe') AM.fade.end = e.target.value / 100;
});

function amPick(i, add) {
  if (AM.fade.picking) {
    AM.fade.to = i;
    AM.fade.picking = false;
  } else if (add) AM.sel.has(i) ? AM.sel.delete(i) : AM.sel.add(i);
  else AM.sel = new Set([i]);
  amRender();
}

// (a colour as hue, saturation, lightness, and back)
function amHsl(hex) {
  let r = parseInt(hex.slice(1, 3), 16) / 255, g = parseInt(hex.slice(3, 5), 16) / 255, b = parseInt(hex.slice(5, 7), 16) / 255;
  const mx = Math.max(r, g, b), mn = Math.min(r, g, b), l = (mx + mn) / 2;
  let h = 0, s = 0;
  if (mx !== mn) {
    const d = mx - mn;
    s = l > 0.5 ? d / (2 - mx - mn) : d / (mx + mn);
    h = mx === r ? (g - b) / d + (g < b ? 6 : 0) : mx === g ? (b - r) / d + 2 : (r - g) / d + 4;
    h *= 60;
  }
  return [h, s, l];
}
function amHex(h, s, l) {
  h = ((h % 360) + 360) % 360;
  const c = (1 - Math.abs(2 * l - 1)) * s, x = c * (1 - Math.abs(((h / 60) % 2) - 1)), m = l - c / 2;
  const [r, g, b] = h < 60 ? [c, x, 0] : h < 120 ? [x, c, 0] : h < 180 ? [0, c, x] : h < 240 ? [0, x, c] : h < 300 ? [x, 0, c] : [c, 0, x];
  return '#' + [r, g, b].map((v) => Math.round(Math.min(1, Math.max(0, v + m)) * 255).toString(16).padStart(2, '0')).join('');
}
function amChangePal(fn) {
  const d = amDir();
  const pal = AM.pals[d.pal];
  AM.undo.push({ kind: 'pal', name: d.pal, cols: pal.slice() });
  for (const i of AM.sel) if (i !== 0) pal[i] = fn(pal[i], i);
  AM.dirtyPal.add(d.pal);
  // (the picked colours stop blinking a few seconds: the change is what's
  // looked at now, not where they are - the blink's magenta read as the result)
  AM.quiet = Date.now() + 3000;
  AM.blink = false;
  amRender();
}

async function amLoad(ani) {
  AM.busy = 'Reading…';
  amRender();
  const ztdName = ((AM.anims || []).find((a) => a.ani === ani) || {}).ztd;
  const r = await lab.artDo(AM.folder, { op: 'load', ani, ztdName });
  AM.busy = '';
  if (!r.ok) return toast(esc(r.error || 'It couldn’t be read.'), 5000);
  for (const d of r.dirs) for (const f of d.frames) (f.px = amB64(f.px)), (f.mask = amB64(f.mask));
  for (const [name, cols] of Object.entries(r.palettes)) {
    if (!AM.pals[name]) AM.pals[name] = cols.slice();
    AM.saved[name] = AM.saved[name] || cols.slice();
  }
  AM.ani = ani;
  AM.ztd = ztdName;
  AM.data = r;
  AM.dir = Math.min(AM.dir, r.dirs.length - 1);
  AM.frame = 0;
  AM.undo = AM.undo.filter((u) => u.kind === 'pal');
  AM.dirtyFrames = {};
  amRender();
  amPlay();
}

Object.assign(ACTIONS, {
  amOpen: async (ani) => {
    if (Object.values(AM.dirtyFrames).some((s) => s.size) && !confirm('Leave this animation’s painted frames without saving them?')) return;
    await amLoad(ani);
  },
  amDir: (i) => {
    AM.dir = +i;
    AM.frame = 0;
    amRender();
    amPlay();
  },
  amPlayToggle: () => {
    AM.playing = !AM.playing;
    amRender();
  },
  amStep: (k) => {
    const d = amDir();
    if (!d) return;
    AM.playing = false;
    AM.frame = (AM.frame + +k + d.frames.length) % d.frames.length;
    amRender();
  },
  amZoom: (k) => {
    AM.zoom = Math.max(1, Math.min(12, AM.zoom + +k));
    amRender();
  },
  amBg: () => {
    AM.bg = { checker: 'grass', grass: 'dark', dark: 'checker' }[AM.bg] || 'checker';
    store.set('amBg', AM.bg);
    amRender();
  },
  amTool: (t) => {
    AM.tool = t;
    amRender();
  },
  amSwatch: (arg) => {
    // ("17+": added to the picked ones, as Shift-click - for the Lab's MCP)
    const i = parseInt(arg, 10);
    const e = /\+$/.test(String(arg)) ? { shiftKey: true } : AM.keys ? { shiftKey: AM.keys.shift, altKey: AM.keys.alt } : null;
    AM.keys = null;
    if (e && e.altKey) AM.brush = i;
    else if (AM.tool === 'paint' && !(e && e.shiftKey) && !AM.fade.picking) AM.brush = i;
    else return amPick(i, e && e.shiftKey);
    amRender();
  },
  amSelClear: () => {
    AM.sel.clear();
    amRender();
  },
  // (the colours used here whose shade is near a picked one's: a whole part
  // is several shades of one colour)
  amSimilar: () => {
    const pal = amPal();
    const n = amCounts();
    const picked = [...AM.sel].map((i) => amHsl(pal[i]));
    for (let i = 1; i < 256; i++) {
      if (!n[i]) continue;
      const [h, s, l] = amHsl(pal[i]);
      if (picked.some(([ph, ps, pl]) => (s < 0.12 && ps < 0.12 ? Math.abs(l - pl) < 0.2 : Math.min(Math.abs(h - ph), 360 - Math.abs(h - ph)) < 20 && Math.abs(l - pl) < 0.3)))
        AM.sel.add(i);
    }
    amRender();
  },
  // (every colour used here darker than the shade slider: all a part's
  // shades at once; with Shift, added to what's picked)
  amPickDark: (arg) => {
    if (arg) AM.dark = +arg;
    const pal = amPal();
    const n = amCounts();
    const add = AM.keys && AM.keys.shift;
    AM.keys = null;
    if (!add) AM.sel.clear();
    for (let i = 1; i < 256; i++) if (n[i] && amHsl(pal[i])[2] * 100 < AM.dark) AM.sel.add(i);
    amRender();
  },
  // (the same, lighter than the slider: a coat's bright shades)
  amPickLight: (arg) => {
    if (arg) AM.dark = +arg;
    const pal = amPal();
    const n = amCounts();
    const add = AM.keys && AM.keys.shift;
    AM.keys = null;
    if (!add) AM.sel.clear();
    for (let i = 1; i < 256; i++) if (n[i] && amHsl(pal[i])[2] * 100 > AM.dark) AM.sel.add(i);
    amRender();
  },
  amSetColour: () => {
    const v = ($('am-colour') || {}).value;
    if (!v) return;
    // (one picked: it becomes this; several: each keeps its shade, in this colour)
    if (AM.sel.size === 1) return amChangePal(() => v);
    const [th, ts] = amHsl(v);
    amChangePal((c) => amHex(th, ts, amHsl(c)[2]));
  },
  // (the sliders' hue and lightness; or "hue,light" given - from the Lab's MCP)
  amShift: (arg) => {
    const given = arg ? String(arg).split(',').map(Number) : null;
    const dh = given ? given[0] || 0 : +($('am-hue') || {}).value || 0;
    const dl = (given ? given[1] || 0 : +($('am-light') || {}).value || 0) / 100;
    amChangePal((c) => {
      const [h, s, l] = amHsl(c);
      return amHex(h + dh, s, Math.min(1, Math.max(0, l + dl)));
    });
  },
  amFadeTo: () => {
    AM.fade.picking = true;
    toast('Now click the colour they turn into (a swatch, or the art).', 3000);
  },
  // (from the Lab's MCP: "start,end,count,width,darkness,depth" in %, count as is)
  amStripes: async (arg) => {
    if (arg) {
      const [a, b, n, w, d, p] = String(arg).split(',').map(Number);
      Object.assign(AM.stripes, { start: a / 100, end: b / 100, count: n, width: w / 100, darkness: d / 100, depth: p / 100 });
    }
    const d = amDir();
    if (!AM.sel.size) return;
    if (amDirty() && !(await ACTIONS.amSave())) return;
    const folder = AM.data.folder.split('/').slice(0, 2).join('/') + '/';
    AM.busy = `Striping every frame of ${folder}…`;
    amRender();
    const r = await lab.artDo(AM.folder, { op: 'stripes', ztdName: AM.ztd, folder, pal: d.pal, from: [...AM.sel], ...AM.stripes });
    AM.busy = '';
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be striped.'), 5000);
    toast(`Striped ${r.frames} frame files.`, 3500);
    await amLoad(AM.ani);
  },
  amFade: async () => {
    const d = amDir();
    if (!AM.sel.size || AM.fade.to === null) return;
    if (amDirty() && !(await ACTIONS.amSave())) return;
    // (every frame of this animal: the type's folder - animals/quagga/)
    const folder = AM.data.folder.split('/').slice(0, 2).join('/') + '/';
    AM.busy = `Fading in every frame of ${folder}…`;
    amRender();
    const r = await lab.artDo(AM.folder, { op: 'fade', ztdName: AM.ztd, folder, pal: d.pal, from: [...AM.sel], to: AM.fade.to, start: AM.fade.start, end: AM.fade.end });
    AM.busy = '';
    if (!r.ok) return toast(esc(r.error || 'It couldn’t be faded.'), 5000);
    toast(`Faded in ${r.frames} frame files. Undo isn’t kept for this: make the animal again to start over.`, 4500);
    await amLoad(AM.ani);
  },
  amSave: async () => {
    if (!amDirty()) return true;
    const palettes = {};
    for (const name of AM.dirtyPal) palettes[name] = AM.pals[name];
    const frames = {};
    for (const [dir, set] of Object.entries(AM.dirtyFrames)) {
      const d = AM.data.dirs.find((x) => x.name === dir);
      for (const n of set) (frames[dir] = frames[dir] || {})[n] = { px: amToB64(d.frames[n].px), mask: amToB64(d.frames[n].mask) };
    }
    AM.busy = 'Saving into the mod…';
    amRender();
    const r = await lab.artDo(AM.folder, { op: 'save', ztdName: AM.ztd, ani: AM.ani, palettes, frames });
    AM.busy = '';
    if (!r.ok) {
      toast(esc(r.error || 'It couldn’t be saved.'), 5000);
      amRender();
      return false;
    }
    for (const name of AM.dirtyPal) AM.saved[name] = AM.pals[name].slice();
    AM.dirtyPal.clear();
    AM.dirtyFrames = {};
    toast(`Saved into <b>${esc(AM.folder)}</b>: ${r.wrote.length} file${r.wrote.length === 1 ? '' : 's'}. Try in the Test Zoo shows it.`, 3500);
    amRender();
    return true;
  },
  amUndo: () => {
    const u = AM.undo.pop();
    if (!u) return;
    if (u.kind === 'pal') {
      AM.pals[u.name] = u.cols;
      AM.dirtyPal.add(u.name);
    } else {
      const f = AM.data.dirs[u.dir].frames[u.n];
      f.px = u.px;
      f.mask = u.mask;
    }
    amRender();
  },
  amExport: async () => {
    if (!AM.ani) return;
    if (amDirty() && !(await ACTIONS.amSave())) return;
    const r = await lab.artDo(AM.folder, { op: 'export', ztdName: AM.ztd, ani: AM.ani });
    toast(r.ok ? `${r.files} PNGs (<span class="mono">${esc(r.out)}</span>): paint them, keep their names and sizes, then Frames in.` : esc(r.error), 6000);
  },
  amImport: async () => {
    if (!AM.ani) return;
    AM.busy = 'Reading the PNGs…';
    amRender();
    const r = await lab.artDo(AM.folder, { op: 'import', ztdName: AM.ztd, ani: AM.ani });
    AM.busy = '';
    if (!r.ok) return toast(esc(r.error || 'They couldn’t be read.'), 6000), amRender();
    toast(`${r.frames} frames in from the PNGs, saved into the mod.`, 3500);
    await amLoad(AM.ani);
  },
});
