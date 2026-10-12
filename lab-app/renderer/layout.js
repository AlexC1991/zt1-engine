// ZT1 Lab: the Scene tab as panels, laid out as you like (dockview-core):
// drag a panel's tab to dock it beside, above, below or in with another, or
// out to float; drag the edges between them to size them; close one with its
// ×, bring it back from Layout. The layout is kept, and layouts can be saved
// by name. Each panel is one of the Lab's own pieces (the Scene view, the zoo
// outline, the inspector, the steps and each of a step's panes, Try, the log,
// the console), moved in as it is, so everything else draws into it as before.
// (loaded after stepview.js; Layout › Classic goes back to the fixed one)

const DV = window['dockview-core'];
const LAYOUT = {
  on: store.get('dockOn', true) && !!DV,
  api: null,
  els: {}, // panel id -> its element (made once, moved in and out)
  shownTab: null,
  saveT: null,
  overlays: store.get('stageOverlays', { tools: true, view: true, time: true, hover: true, labels: true }),
  slim: store.get('dockSlim', true), // (slim bars: tabs, gaps, panel heads)
};

// The panels: id, title, and what goes in it
const PANEL_DEFS = [
  ['scene', 'Scene', () => $('stage')],
  ['outline', 'Zoo', () => $('scene-left')],
  ['inspector', 'Inspector', () => $('scene-right')],
  ['steps', 'Steps', () => frag(`<div class="dkctl" id="dk-ctl"></div><div class="dklist"><input class="search small" id="dk-q" placeholder="Filter steps" style="width:100%"><div id="dk-list" class="dkrows"></div></div>`)],
  ['step', 'Step', () => frag(`<div class="dkstep" id="dk-step"></div>`)],
  ['code', 'Our code', () => frag(`<div class="dkpane show"><div class="dkhead" id="dk-srchead"></div><div class="code dkcode" id="dk-src"></div></div>`)],
  ['zooc', 'zoo.exe C', () => frag(`<div class="dkpane show zoo"><div class="dkhead" id="dk-zoohead"></div><div class="code dkcode" id="dk-zoo"></div></div>`)],
  ['bin', 'zoo.exe binary', () => frag(`<div class="dkpane show zoo"><div class="dkhead" id="dk-binhead"></div><div class="dkcode dkbin" id="dk-bin"></div></div>`)],
  ['cmp', 'Compare', () => frag(`<div class="dkpane show"><div class="dkhead" id="dk-cmphead"></div><div class="dkcode dkpad" id="dk-cmp"></div></div>`)],
  ['err', 'Errors', () => frag(`<div class="dkpane show"><div class="dkhead" id="dk-errhead"></div><div class="dkcode dkpad" id="dk-err"></div></div>`)],
  ['try', 'Try zoo.exe', () => frag(`<div id="try" class="try"></div>`)],
  ['log', 'Log', () => frag(`<div id="dk-log" class="conlog"></div>`)],
  ['watch', 'Watch', () => frag(`<div class="dkctl" id="wt-ctl"></div><div id="wt-list" class="dkrows wtrows"></div>`)],
  ['console', 'Console', () => frag(`<div id="dk-conlog" class="conlog"></div><div class="conin"><span class="green">›</span><input id="con-in" spellcheck="false" placeholder="The game’s console: type help"></div>`)],
];
const TITLE = Object.fromEntries(PANEL_DEFS.map(([id, t]) => [id, t]));
// (the old dock's tabs, as panels: code that "opens the Steps tab" brings
// its panel to the front)
const TAB_PANEL = { steps: 'steps', try: 'try', log: 'log', console: 'console' };

function frag(html) {
  const d = document.createElement('div');
  d.className = 'dvfill';
  d.innerHTML = html;
  return d;
}

// A panel's element: made (or found) once, kept when its panel closes
function panelEl(id) {
  if (!LAYOUT.els[id]) {
    const def = PANEL_DEFS.find((d) => d[0] === id);
    const el = def ? def[2]() : frag(`<div class="muted dkmsg">No panel “${esc(id)}”.</div>`);
    el.classList.add('dvcontent');
    LAYOUT.els[id] = el;
    if (id === 'steps') {
      const q = el.querySelector('#dk-q');
      q.oninput = (e) => {
        DK.filter = e.target.value.toLowerCase();
        renderSteps(true);
      };
    }
    if (id === 'console') el.querySelector('#con-in').onkeydown = conKey;
  }
  return LAYOUT.els[id];
}

function startLayout() {
  if (!LAYOUT.on) return;
  const host = $('v-scene');
  // (the pieces kept, the fixed layout's frame taken away)
  for (const id of ['scene', 'outline', 'inspector']) panelEl(id);
  host.innerHTML = '';
  host.classList.add('docked');
  host.classList.toggle('dv-slim', LAYOUT.slim);
  const root = document.createElement('div');
  root.className = 'dvroot';
  host.appendChild(root);
  // (the old dock's bar and box, kept out of sight: some code still sizes them)
  for (const id of ['consolebar', 'console']) {
    const e = document.createElement('div');
    e.id = id;
    e.className = 'hidden';
    host.appendChild(e);
  }
  LAYOUT.api = DV.createDockview(root, {
    theme: dockTheme(),
    createComponent: (o) => {
      const el = panelEl(o.name);
      return { element: el, init() {} };
    },
  });
  let ok = false;
  const saved = store.get('dockState', null);
  if (saved)
    try {
      LAYOUT.api.fromJSON(saved);
      ok = LAYOUT.api.panels.length > 0;
    } catch (e) {
      console.warn('layout', e);
    }
  if (!ok) defaultLayout();
  new ResizeObserver(() => LAYOUT.wantSizes && requestAnimationFrame(defaultSizes)).observe(root);
  LAYOUT.api.onDidLayoutChange(() => {
    clearTimeout(LAYOUT.saveT);
    LAYOUT.saveT = setTimeout(() => store.set('dockState', LAYOUT.api.toJSON()), 400);
    requestAnimationFrame(sizeCanvas);
  });
  applyOverlays();
  renderTop();
}

function dockTheme() {
  return Object.assign({}, DV.themeDark, { className: 'dockview-theme-dark lab-dock', gap: LAYOUT.slim ? 2 : 6 });
}
function setSlim(on) {
  LAYOUT.slim = on;
  store.set('dockSlim', on);
  $('v-scene').classList.toggle('dv-slim', on);
  if (LAYOUT.api && LAYOUT.api.updateOptions) LAYOUT.api.updateOptions({ theme: dockTheme() });
  requestAnimationFrame(sizeCanvas);
}

const P = (id, position, more = {}) => {
  const o = { id, component: id, title: TITLE[id], renderer: 'always', ...more };
  if (position) o.position = position;
  return LAYOUT.api.addPanel(o);
};
function defaultLayout() {
  const a = LAYOUT.api;
  a.clear();
  P('scene');
  P('outline', { referencePanel: 'scene', direction: 'left' }, { initialWidth: 240 });
  P('inspector', { referencePanel: 'scene', direction: 'right' }, { initialWidth: 290 });
  P('steps', { referencePanel: 'scene', direction: 'below' }, { initialHeight: 330 });
  P('code', { referencePanel: 'steps', direction: 'right' });
  P('zooc', { referencePanel: 'code', direction: 'within' }, { inactive: true });
  P('bin', { referencePanel: 'code', direction: 'right' });
  for (const id of ['cmp', 'err', 'try', 'log', 'console', 'watch']) P(id, { referencePanel: 'bin', direction: 'within' }, { inactive: true });
  P('step', { referencePanel: 'code', direction: 'above' }, { initialHeight: 84 });
  // (sized once the panels have room: made while the Scene tab's hidden,
  // they've none, and sizes given then come out as halves)
  LAYOUT.wantSizes = true;
  requestAnimationFrame(defaultSizes);
}
function defaultSizes() {
  const root = document.querySelector('.dvroot');
  if (!LAYOUT.wantSizes || !root || root.clientWidth < 400 || root.clientHeight < 300) return;
  LAYOUT.wantSizes = false;
  const H = root.clientHeight,
    W = root.clientWidth;
  const size = (id, s) => {
    const p = LAYOUT.api.getPanel(id);
    if (p) p.group.api.setSize(s);
  };
  size('outline', { width: 240 });
  size('inspector', { width: 290 });
  size('steps', { height: Math.round(H * 0.42), width: 330 });
  size('bin', { width: Math.round((W - 240 - 290 - 330) * 0.5) });
  size('step', { height: 84 });
}

// A panel to the front (opened again where it'll fit, if it was closed)
function showPanel(id) {
  const a = LAYOUT.api;
  if (!a) return;
  let p = a.getPanel(id);
  if (!p) {
    const near = ['bin', 'code', 'steps', 'scene'].find((x) => x !== id && a.getPanel(x));
    p = P(id, near ? { referencePanel: near, direction: 'within' } : undefined);
  }
  p.api.setActive();
}
function closePanel(id) {
  const p = LAYOUT.api && LAYOUT.api.getPanel(id);
  if (p) p.api.close();
}

// The Scene's floating pieces, each on or off
function applyOverlays() {
  const st = $('stage');
  if (!st) return;
  for (const k of ['tools', 'view', 'time', 'hover', 'labels']) st.classList.toggle('no-' + k, LAYOUT.overlays[k] === false);
}

// The docked dock: everything that's in a panel, drawn (renderConsole's
// work when panels are on)
function renderDocked() {
  if (DK.tab !== LAYOUT.shownTab) {
    LAYOUT.shownTab = DK.tab;
    if (TAB_PANEL[DK.tab] && LAYOUT.api) showPanel(TAB_PANEL[DK.tab]);
  }
  const c = $('dk-ctl');
  if (c) {
    const { ctl, right } = dkControls();
    setHtml(c, `${ctl}<div class="spacer"></div>${right}`);
  }
  if ($('dk-list')) renderSteps();
  else if ($('dk-step')) renderStepView();
  if ($('try')) renderTry();
  const log = $('dk-log');
  if (log) fillLog(log, S.engine.log.slice(-600), '');
  const cl = $('dk-conlog');
  if (cl) fillLog(cl, SC.conOut, '<span class="dim">The game’s dev console: type help below.</span>');
  renderWatch();
}

// ------------------------------------------------------------ the Watch log
// The animals you follow: a row each time one starts doing something else
// or has gone a tile, in game time - a hunt, a chase, a stand-off, in order.
// The engine keeps it (paused and skipping time too); this shows it
const WT = { rows: [], watching: [] };
SCENE_MSG.watch = (m) => {
  WT.rows = m.rows || [];
  WT.watching = m.watching || [];
  renderWatch();
};
function wtClock(t) {
  const m = Math.floor(t / 60);
  return `${m}:${(t - m * 60).toFixed(1).padStart(4, '0')}`;
}
function renderWatch() {
  const ctl = $('wt-ctl'),
    list = $('wt-list');
  if (!ctl || !list) return;
  const a = currentAnimal();
  const s = sc();
  const picked = s && s.sel && s.sel.kind === 'animal' ? s.sel : null;
  const following = picked && WT.watching.some((w) => w.id === picked.id);
  setHtml(
    ctl,
    `${
      picked
        ? `<div class="btn small" data-act="watchSel">${following ? 'Stop following' : 'Follow'} ${esc(picked.name || (a && a.name) || 'it')}</div>`
        : '<span class="dim">Pick an animal to follow it</span>'
    }${WT.watching.map((w) => `<span class="chip" data-act="watchOff" data-arg="${w.id}" title="Stop following">${esc(w.name)} ✕</span>`).join('')}<div class="spacer"></div><div class="btn small" data-act="watchClear">Clear</div>`,
  );
  const atEnd = list.scrollTop + list.clientHeight >= list.scrollHeight - 20;
  setHtml(
    list,
    WT.rows.length
      ? WT.rows
          .map(
            (r) =>
              `<div class="wtrow${r.doing === '(gone)' ? ' bad' : ''}"><span class="dim mono">${wtClock(r.t)}</span><span>${esc(r.name)}</span><span class="mono">${esc(r.doing)}</span><span class="dim mono">${r.doing === '(gone)' ? '' : `${r.x.toFixed(1)}, ${r.y.toFixed(1)}`}</span></div>`,
          )
          .join('')
      : '<div class="muted dkmsg">Follow an animal, then run the zoo or skip time: each thing it starts doing, and each tile it goes, shows here.</div>',
  );
  if (atEnd) list.scrollTop = list.scrollHeight;
}
Object.assign(ACTIONS, {
  // (the picked animal; or an animal's id, from code that picked it a moment
  // ago and can't wait for the page to hear of it)
  watchSel: (arg) => {
    const s = sc();
    const id = arg !== undefined && arg !== '' ? +arg : s && s.sel && s.sel.kind === 'animal' ? s.sel.id : -1;
    if (id < 0) return;
    const on = !WT.watching.some((w) => w.id === id);
    lab.engineSend({ cmd: 'watch', id, on });
    if (on) showPanel('watch');
  },
  watchOff: (id) => lab.engineSend({ cmd: 'watch', id: +id, on: false }),
  watchClear: () => lab.engineSend({ cmd: 'watch', clear: true }),
});
function fillLog(el, lines, empty) {
  const atEnd = el.scrollTop + el.clientHeight >= el.scrollHeight - 20;
  setHtml(
    el,
    lines.map((l) => (l.startsWith('› ') ? `<span class="cmd">${esc(l)}</span>` : /FAIL|error|crashed|couldn't/i.test(l) ? `<span class="bad">${esc(l)}</span>` : esc(l))).join('\n') || empty,
  );
  if (atEnd) el.scrollTop = el.scrollHeight;
}

// ------------------------------------------------------------ the menu
function layoutMenu() {
  const m = $('modal');
  const a = LAYOUT.api;
  const saved = store.get('dockLayouts', {});
  const open = (id) => !!(a && a.getPanel(id));
  const ov = [
    ['tools', 'Tools (Select, Place, Bulldoze…)'],
    ['view', 'Map and view bar'],
    ['time', 'Play, time and speed'],
    ['hover', 'What’s under the cursor (the status box)'],
    ['labels', 'Exhibit names'],
  ];
  m.innerHTML = `<div class="dialog lmenu"><div class="h">Layout</div>
    <div class="lcols">
      <div><div class="label">Panels</div>${PANEL_DEFS.map(
        ([id, t]) => `<label class="lrow"><input type="checkbox" data-panel="${id}"${open(id) ? ' checked' : ''}> ${esc(t)}</label>`,
      ).join('')}</div>
      <div><div class="label">On the Scene view</div>${ov
        .map(([k, t]) => `<label class="lrow"><input type="checkbox" data-ov="${k}"${LAYOUT.overlays[k] !== false ? ' checked' : ''}> ${esc(t)}</label>`)
        .join('')}
        <div class="label" style="margin-top:14px">Bars</div>
        <div class="seg mini"><div class="${LAYOUT.slim ? 'on' : ''}" data-slim="1">Slim</div><div class="${LAYOUT.slim ? '' : 'on'}" data-slim="0">Normal</div></div>
        <div class="label" style="margin-top:14px">Layouts</div>
        ${
          Object.keys(saved).length
            ? Object.keys(saved)
                .map((n) => `<div class="lrow"><span class="clickable blue" data-load="${esc(n)}">${esc(n)}</span><span class="spacer"></span><span class="clickable dim" data-del="${esc(n)}" title="Delete it">✕</span></div>`)
                .join('')
            : '<div class="muted" style="font-size:12px">None saved yet.</div>'
        }
        <div class="lrow" style="margin-top:8px"><input class="search small" id="lay-name" placeholder="A name for this layout" style="flex:1"><div class="btn small" data-save="1">Save</div></div>
      </div>
    </div>
    <div class="muted" style="font-size:12px;margin-top:8px">Drag a panel’s tab onto another panel’s side to dock it there, onto its middle to put it in with it, or off to float it. Drag the edges between panels to size them.</div>
    <div class="btns"><div class="btn" data-reset="1">Reset to the default</div><div class="btn" data-classic="1">${LAYOUT.on ? 'Classic (no panels)' : 'Panels'}</div><div class="spacer"></div><div class="btn primary" data-x="no">Done</div></div></div>`;
  m.classList.remove('hidden');
  m.onchange = (e) => {
    const t = e.target;
    if (t.dataset.panel) (t.checked ? showPanel : closePanel)(t.dataset.panel);
    if (t.dataset.ov) {
      LAYOUT.overlays[t.dataset.ov] = t.checked;
      store.set('stageOverlays', LAYOUT.overlays);
      applyOverlays();
    }
  };
  m.onclick = (e) => {
    const t = e.target.closest('[data-x],[data-load],[data-del],[data-save],[data-reset],[data-classic],[data-slim]') || e.target;
    const close = () => {
      m.classList.add('hidden');
      m.innerHTML = '';
    };
    if (t === m || t.dataset.x === 'no') return close();
    if (t.dataset.slim !== undefined) {
      setSlim(t.dataset.slim === '1');
      return layoutMenu();
    }
    if (t.dataset.load && saved[t.dataset.load]) {
      try {
        a.fromJSON(saved[t.dataset.load]);
      } catch {
        toast('That layout couldn’t be loaded.');
      }
      close();
    } else if (t.dataset.del) {
      delete saved[t.dataset.del];
      store.set('dockLayouts', saved);
      layoutMenu();
    } else if (t.dataset.save) {
      const n = ($('lay-name').value || '').trim();
      if (!n) return toast('Give it a name first.');
      saved[n] = a.toJSON();
      store.set('dockLayouts', saved);
      toast(`Layout “${esc(n)}” saved.`);
      layoutMenu();
    } else if (t.dataset.reset) {
      defaultLayout();
      close();
    } else if (t.dataset.classic) {
      store.set('dockOn', !LAYOUT.on);
      location.reload();
    }
  };
}

Object.assign(ACTIONS, { layoutMenu });

// (the Layout button: the Scene tab's)
{
  const top = renderTop;
  renderTop = function () {
    top.apply(this, arguments);
    const b = $('layout-btn');
    if (b) b.style.display = S.tab === 'scene' ? '' : 'none';
  };
}
startLayout();
