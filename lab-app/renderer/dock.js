// ZT1 Lab: Original Game and Write Code laid out as panels, like the Test Zoo
// (dockview-core): drag a panel's tab beside, above, below or in with
// another, or out to float; drag the edges to size them; close one with its
// ×, bring it back from Layout. Each tab's layout is kept. The panels are the
// tab's own pieces, made once; the tab's render functions fill them.
// (loaded after layout.js: its theme and slim bars)

const TABDOCK = {};

// A tab's dock: its panels (id, title, class of the element), and how they
// sit at first
function tabDock(view, key, defs, layout, sizes) {
  const have = TABDOCK[view];
  if (have && document.getElementById('v-' + view).contains(have.root)) return have;
  const host = $('v-' + view);
  host.innerHTML = '';
  host.classList.add('docked', 'tabdocked');
  host.classList.toggle('dv-slim', LAYOUT.slim);
  const root = document.createElement('div');
  root.className = 'dvroot';
  host.appendChild(root);
  const els = (have && have.els) || {};
  for (const [id, , cls] of defs)
    if (!els[id]) {
      const el = document.createElement('div');
      el.className = 'dvcontent ' + (cls || '');
      el.id = 'dk-' + view + '-' + id;
      els[id] = el;
    }
  const api = DV.createDockview(root, {
    theme: dockTheme(),
    createComponent: (o) => ({ element: els[o.name], init() {} }),
  });
  const d = { view, key, defs, layout, sizes: sizes || [], api, root, els, saveT: null, wantSizes: false };
  d.add = (id, position, more = {}) => {
    const def = defs.find((x) => x[0] === id);
    const o = { id, component: id, title: def ? def[1] : id, renderer: 'always', ...more };
    if (position) o.position = position;
    return api.addPanel(o);
  };
  let ok = false;
  const saved = store.get(key, null);
  if (saved)
    try {
      api.fromJSON(saved);
      ok = api.panels.length > 0;
    } catch (e) {
      console.warn('layout', e);
    }
  if (!ok) tabDockDefault(d);
  // (sized once the panels have room: made while the tab's hidden, they've
  // none, and sizes given then come out as equal parts)
  new ResizeObserver(() => d.wantSizes && setTimeout(() => tabDockSizes(d), 250)).observe(root);
  api.onDidLayoutChange(() => {
    clearTimeout(d.saveT);
    d.saveT = setTimeout(() => store.set(key, api.toJSON()), 400);
  });
  TABDOCK[view] = d;
  return d;
}

function tabDockDefault(d) {
  d.api.clear();
  d.layout(d);
  d.wantSizes = true;
  // (after dockview's own first layout, which shares the room out evenly)
  setTimeout(() => tabDockSizes(d), 250);
}
function tabDockSizes(d) {
  if (!d.wantSizes || d.root.clientWidth < 500 || d.root.clientHeight < 300) return;
  d.wantSizes = false;
  for (const [id, size] of d.sizes) {
    const p = d.api.getPanel(id);
    if (p) p.group.api.setSize(size);
  }
}

// A panel to the front (opened again where it'll fit, if it was closed)
function tabDockShow(d, id) {
  let p = d.api.getPanel(id);
  if (!p) {
    const near = d.defs.map((x) => x[0]).find((x) => x !== id && d.api.getPanel(x));
    p = d.add(id, near ? { referencePanel: near, direction: 'right' } : undefined);
  }
  p.api.setActive();
}

// ---------------------------------------------------- Original Game
const ORIG_PANELS = [
  ['finder', 'Find', 'finder'],
  ['code', 'Code', 'codepane'],
  ['details', 'Name & details', 'detailspane'],
];
function originalDock() {
  return tabDock('code', 'dockOrig', ORIG_PANELS, (d) => {
    d.add('code');
    d.add('finder', { referencePanel: 'code', direction: 'left' }, { initialWidth: 330 });
    d.add('details', { referencePanel: 'code', direction: 'right' }, { initialWidth: 300 });
  }, [['finder', { width: 330 }], ['details', { width: 300 }]]);
}

// ------------------------------------------------------- Write Code
const ENG_PANELS = [
  ['files', 'Files', 'modlist engfiles'],
  ['editor', 'Editor', 'engeditor'],
  ['output', 'Build', 'engout'],
];
function engineDock() {
  return tabDock('engine', 'dockEng', ENG_PANELS, (d) => {
    d.add('editor');
    d.add('files', { referencePanel: 'editor', direction: 'left' }, { initialWidth: 320 });
    d.add('output', { referencePanel: 'editor', direction: 'below' }, { initialHeight: 210 });
  }, [['files', { width: 320 }], ['output', { height: 210 }]]);
}

// ------------------------------------------------------ the menu
// (Layout, on these tabs: their panels, back to the default)
function tabDockMenu(d) {
  const m = $('modal');
  m.innerHTML = `<div class="dialog lmenu"><div class="h">Layout · ${esc(TABS.find((t) => t[0] === d.view)[1])}</div>
    <div class="label">Panels</div>${d.defs
      .map(([id, t]) => `<label class="lrow"><input type="checkbox" data-panel="${id}"${d.api.getPanel(id) ? ' checked' : ''}> ${esc(t)}</label>`)
      .join('')}
    <div class="muted" style="font-size:12px;margin-top:8px">Drag a panel’s tab onto another panel’s side to dock it there, onto its middle to put it in with it, or off to float it. Drag the edges between panels to size them.</div>
    <div class="btns"><div class="btn" data-reset="1">Reset to the default</div><div class="spacer"></div><div class="btn primary" data-x="no">Done</div></div></div>`;
  m.classList.remove('hidden');
  const close = () => {
    m.classList.add('hidden');
    m.innerHTML = '';
  };
  m.onchange = (e) => {
    const id = e.target.dataset.panel;
    if (!id) return;
    if (e.target.checked) tabDockShow(d, id);
    else {
      const p = d.api.getPanel(id);
      if (p) p.api.close();
    }
  };
  m.onclick = (e) => {
    const t = e.target.closest('[data-x],[data-reset]') || e.target;
    if (t === m || t.dataset.x === 'no') return close();
    if (t.dataset.reset) {
      tabDockDefault(d);
      close();
      // (the pieces drawn into their panels again)
      setTab(S.tab);
    }
  };
}
{
  const sceneMenu = ACTIONS.layoutMenu;
  ACTIONS.layoutMenu = () => {
    if (S.tab === 'code') return tabDockMenu(originalDock());
    if (S.tab === 'engine') return tabDockMenu(engineDock());
    return sceneMenu();
  };
  const top = renderTop;
  renderTop = function () {
    top.apply(this, arguments);
    const b = $('layout-btn');
    if (b) b.style.display = S.tab === 'scene' || S.tab === 'code' || S.tab === 'engine' ? '' : 'none';
  };
}
