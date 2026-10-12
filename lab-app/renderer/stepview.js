// ZT1 Lab: the step looked at, in the dock's Steps tab. Beside the steps
// list: the step itself (what, who, when, all its values) and panes, one or
// two side by side:
//   Our code        the engine's line the step is at (renderStepSource)
//   zoo.exe C       the function it copies, decompiled (renderStepSource)
//   zoo.exe binary  that function's own machine code, run on what's picked
//                   (re/lab/zoorun.py, as Try zoo.exe): every instruction,
//                   what it read and wrote
//   Compare         our step's values against what the original read and
//                   wrote, by name
//   Errors          what the original reached for and nobody's mapped
// (loaded after try.js)

const BIN = { key: null, res: null, running: false, cur: 0, ranOn: '', timer: null };
const PANES = [
  ['code', 'Our code'],
  ['c', 'zoo.exe C'],
  ['bin', 'zoo.exe binary'],
  ['cmp', 'Compare'],
  ['err', 'Errors'],
];
DK.split = store.get('dkSplit', true);
DK.panes = store.get('dkPanes', ['code', 'bin']);
DK.listW = store.get('dkListW', 340);
DK.big = false;

// The dock's Steps tab, laid out (renderConsole puts it in)
function stepsLayoutHtml() {
  return `<div class="dkv" id="dk-v" style="--listw:${DK.listW}px">
    <div class="dklist"><input class="search small" id="dk-q" placeholder="Filter steps" style="width:100%"><div id="dk-list" class="dkrows"></div></div>
    <div class="dkvgrip" id="dk-vgrip" title="Drag to make the list wider or narrower"></div>
    <div class="dkview">
      <div class="dkstep" id="dk-step"></div>
      <div class="dkpanes" id="dk-panes">
        <div class="dkpane" data-pane="code"><div class="dkhead" id="dk-srchead"></div><div class="code dkcode" id="dk-src"></div></div>
        <div class="dkpane zoo" data-pane="c"><div class="dkhead" id="dk-zoohead"></div><div class="code dkcode" id="dk-zoo"></div></div>
        <div class="dkpane zoo" data-pane="bin"><div class="dkhead" id="dk-binhead"></div><div class="dkcode dkbin" id="dk-bin"></div></div>
        <div class="dkpane" data-pane="cmp"><div class="dkhead" id="dk-cmphead"></div><div class="dkcode dkpad" id="dk-cmp"></div></div>
        <div class="dkpane" data-pane="err"><div class="dkhead" id="dk-errhead"></div><div class="dkcode dkpad" id="dk-err"></div></div>
      </div>
    </div>
  </div>`;
}

// The step's zoo.exe function: its own, else the nearest frame's with one
function stepZoo(st) {
  if (!st) return null;
  let zoo = (DK.frame && DK.frame.zoo) || st.zoo;
  if (!zoo) for (let i = (st.stack || []).length - 1; i >= 0 && !zoo; i--) zoo = st.stack[i].zoo;
  return zoo || null;
}

const normName = (n) =>
  String(n || '')
    .toLowerCase()
    .replace(/[^a-z0-9]/g, '')
    .replace(/^(m|c)(?=[a-z])/, '');
// A step value's names: its own, and one in brackets ("lives underwater
// (cUnderwater)")
function valNames(v) {
  const out = [normName(v.n)];
  const m = /\(([^)]+)\)/.exec(v.n);
  if (m) out.push(normName(m[1]));
  return out.filter((x) => x.length > 2);
}

// Ours against the original's: each of the step's values and a field the
// run touched by the same name
function compareRows(st, r) {
  const rows = [];
  if (!st || !r) return rows;
  const touched = (r.touched || []).filter((t) => t.name);
  for (const v of st.vals || []) {
    if (v.s !== undefined) continue;
    const names = valNames(v);
    const t = touched.find((x) => {
      const tn = normName(x.name);
      return names.some((n) => n === tn || (n.length > 4 && tn.length > 4 && (n.includes(tn) || tn.includes(n))));
    });
    if (!t) {
      rows.push({ v, t: null });
      continue;
    }
    const ours = v.v;
    const theirs = t.writes ? t.last : t.first;
    const same = typeof ours === 'number' && typeof theirs === 'number' && Math.abs(ours - theirs) < 0.51;
    rows.push({ v, t, ours, theirs, same });
  }
  return rows;
}

function renderStepView() {
  const hd = $('dk-step');
  if (!hd) return;
  const st = dkStep(DK.cur);
  const zoo = stepZoo(st);
  const s = sc();
  const sel = s && s.sel;
  // (the original run when a pane shows it: once a step, after a moment so
  // going through the list doesn't start one each step)
  // (panels on: the binary, Compare and Errors are panels of their own)
  const paneBox = $('dk-panes');
  const wantBin = paneBox ? DK.panes.some((p) => p === 'bin' || p === 'cmp' || p === 'err') : !!($('dk-bin') || $('dk-cmp') || $('dk-err'));
  const key = st && zoo && sel ? `${zoo}|${st.n}|${sel.kind}:${sel.id}` : null;
  if (wantBin && key && BIN.key !== key) {
    BIN.key = key;
    BIN.res = null;
    clearTimeout(BIN.timer);
    BIN.timer = setTimeout(() => binRun(st, zoo, key), 250);
  }
  const r = BIN.res;
  const errs = r ? (r.errors || []).length : 0;
  const cmp = compareRows(st, r);
  const differ = cmp.filter((x) => x.t && !x.same).length;
  // the step: what, who, when, where; all its values
  const tabs = !paneBox ? '' : PANES.map(([id, l]) => {
    const badge = id === 'err' && errs ? ` <span class="pbadge red">${errs}</span>` : id === 'cmp' && differ ? ` <span class="pbadge amber">${differ}</span>` : '';
    return `<div class="${DK.panes.includes(id) ? 'on' : ''}" data-act="dkPane" data-arg="${id}">${l}${badge}</div>`;
  }).join('');
  setHtml(
    hd,
    st
      ? `<div class="dkstep1"><span class="dim mono">${fmtTime(st.time)}</span><b>${esc(st.what)}</b><span class="muted">${esc(st.whoName || '')}</span>${
          zoo ? `<span class="mono amber clickable" data-act="openCode" data-arg="${esc(zoo)}" title="Open in Code">${esc(zoo)}</span>` : '<span class="dim">no zoo.exe function yet</span>'
        }<div class="spacer"></div>${
          paneBox
            ? `<div class="seg mini ptabs">${tabs}</div><span class="opt2${DK.split ? ' on' : ''}" data-act="dkSplit" title="Two panes side by side, or one">◫ Split</span><span class="opt2${DK.big ? ' on' : ''}" data-act="dkBig" title="Make the dock tall, or back">⤢</span>`
            : ''
        }</div>${
          (st.vals || []).length ? `<div class="dkvals">${st.vals.map((v) => `<span class="vchip"><span class="vk">${esc(v.n)}</span> ${valText(v)}</span>`).join('')}</div>` : ''
        }`
      : `<div class="dkstep1"><span class="muted">Pick a step to see it here: our code, and zoo.exe’s, side by side.</span><div class="spacer"></div>${
          paneBox
            ? `<div class="seg mini ptabs">${tabs}</div><span class="opt2${DK.split ? ' on' : ''}" data-act="dkSplit">◫ Split</span><span class="opt2${DK.big ? ' on' : ''}" data-act="dkBig">⤢</span>`
            : ''
        }</div>`,
  );
  // which panes show, in the order picked
  if (paneBox) {
    paneBox.style.setProperty('--n', DK.panes.length);
    for (const el of paneBox.children) {
      const i = DK.panes.indexOf(el.dataset.pane);
      el.classList.toggle('show', i >= 0);
      el.style.order = i;
    }
  }
  // the binary
  const bh = $('dk-binhead') || document.createElement('div'),
    bb = $('dk-bin') || document.createElement('div');
  const why = !st ? 'Pick a step.' : !zoo ? noZooText() : !sel ? 'Pick what it’s about in the view (the animal, the exhibit…): the original runs on it.' : '';
  if (why) {
    BIN.drawn = null;
    setHtml(bh, '<span class="muted">zoo.exe binary</span>');
    setHtml(bb, `<div class="muted dkmsg">${why}</div>`);
  } else {
    const f = fnInfo(zoo);
    setHtml(
      bh,
      `<span><span class="mono amber">${esc(f.addr)}</span> ${esc(displayName(f))}${r ? ` <span class="dim">· on ${esc(BIN.ranOn)}</span>` : ''}</span><span class="dklinks"><span class="clickable green" data-act="binRerun" title="Run the original again on what's picked, as it is now">${
        BIN.running ? 'Running…' : '▶ Run again'
      }</span><span class="clickable blue" data-act="tryFn" data-arg="${f.addr}" title="Open it in Try zoo.exe: give values, name fields, the C beside it">Open in Try</span></span>`,
    );
    if (!r) setHtml(bb, `<div class="muted dkmsg">${BIN.running ? 'Running zoo.exe’s own code on it…' : 'About to run it…'}</div>`);
    else if (BIN.drawn !== `${BIN.key}:${BIN.cur}:${!!DK.paused}`) {
      // (thousands of instructions: drawn again only when they or the one
      // looked at change, not with every new step)
      BIN.drawn = `${BIN.key}:${BIN.cur}:${!!DK.paused}`;
      setHtml(
        bb,
        `<div class="trystat ${r.ok ? 'green' : 'red'}">${
          r.ok ? `Ran ${r.count.toLocaleString()} instructions and returned <b>${r.eax}</b>` : r.returned ? `Returned ${r.eax}, with problems` : 'It didn’t get to the end'
        }${errs ? ` · <span class="red clickable" data-act="dkPane" data-arg="err">${errs} it couldn’t find</span>` : ''} <span class="dim">· its values as they are now${
          DK.paused ? ' (stopped here: as at this step)' : ', not at the step'
        }</span></div>` + asmRowsHtml(r, BIN.cur, 'binStep'),
      );
    }
  }
  // ours against the original's
  if ($('dk-cmp')) setHtml($('dk-cmphead'), `<span class="muted">Ours against the original</span>${r ? `<span class="dim">${cmp.filter((x) => x.t).length} of ${cmp.length} values matched to a field it used</span>` : ''}`);
  if ($('dk-cmp')) setHtml(
    $('dk-cmp'),
    !st
      ? '<div class="muted dkmsg">Pick a step.</div>'
      : why
        ? `<div class="muted dkmsg">${why}</div>`
        : !r
          ? '<div class="muted dkmsg">Waiting for the original’s run…</div>'
          : !cmp.length
            ? '<div class="muted dkmsg">This step has no numbers to set beside the original’s.</div>'
            : `<div class="cmptab"><div class="cmph"><span>Value</span><span>Ours</span><span>zoo.exe</span><span></span></div>${cmp
                .map((x) =>
                  x.t
                    ? `<div class="cmpr${x.same ? '' : ' diff'}"><span class="vk">${esc(x.v.n)}</span><span>${valText(x.v)}</span><span>${fmt(x.theirs)} <span class="dim">${x.t.writes ? 'written' : 'read'} · ${esc(x.t.name)}</span></span><span>${
                        x.same ? '<span class="green">✓ same</span>' : '<span class="amber">✗ differs</span>'
                      }</span></div>`
                    : `<div class="cmpr none"><span class="vk">${esc(x.v.n)}</span><span>${valText(x.v)}</span><span class="dim">not a field it used (or not named yet)</span><span></span></div>`,
                )
                .join('')}</div>`,
  );
  // what it couldn't find
  if ($('dk-err')) setHtml($('dk-errhead'), `<span class="${errs ? 'red' : 'muted'}">${r ? (errs ? `It wanted, and couldn’t find (${errs})` : 'Nothing missing') : 'Errors'}</span>`);
  if ($('dk-err')) setHtml($('dk-err'), !st || why ? `<div class="muted dkmsg">${why || 'Pick a step.'}</div>` : !r ? '<div class="muted dkmsg">Waiting for the original’s run…</div>' : errorsHtml(r));
  const cur = document.querySelector('#dk-bin .arow.on');
  if (cur && BIN.scrolledTo !== `${BIN.key}:${BIN.cur}`) {
    BIN.scrolledTo = `${BIN.key}:${BIN.cur}`;
    const a = $('dk-bin');
    a.scrollTop = Math.max(0, cur.getBoundingClientRect().top - a.getBoundingClientRect().top + a.scrollTop - a.clientHeight / 3);
  }
  wireStepsLayout();
}

const noZooText = () => 'This code isn’t tied to a zoo.exe function yet. When you find the one it copies, add its address to the step (<span class="mono">ZT_STEP("0x…", …)</span>) and its binary runs here.';

// The original's instructions as they ran, what each touched
function asmRowsHtml(r, curIdx, act) {
  const steps = r.steps || [];
  const cur = steps[curIdx];
  let h = cur ? `<div class="regs mono">${(r.regs || []).map((n, i) => `<span><span class="dim">${n}</span> ${(cur.r[i] >>> 0).toString(16)}</span>`).join('')}</div>` : '';
  h += steps
    .map((x, i) => {
      const bad = x.acc.some((a) => (a.k === 'obj' && !a.known) || a.k === 'nothing');
      const acc = x.acc
        .map((a) => {
          const what = a.k === 'obj' ? a.name || `${(r.objects[a.o] || {}).struct}+${hx(a.off)}` : a.k === 'global' ? hx(a.a) : `${hx(a.a)}?`;
          const cls = a.k === 'obj' ? (a.known ? 'green' : 'red') : a.k === 'nothing' ? 'red' : 'dim';
          return `<span class="${cls}">${a.w ? '←' : ''}${esc(what)}${a.w ? ' = ' + a.v : ' ' + a.v}</span>`;
        })
        .join(' ');
      return `<div class="arow${i === curIdx ? ' on' : ''}${bad ? ' bad' : ''}" data-act="${act}" data-arg="${i}"><span class="k-addr">${x.a.toString(16)}</span><span style="padding-left:${x.d * 10}px">${highlightAsm(x.t)}${
        x.call ? ` <span class="${x.call.skipped ? 'dim' : 'blue'}">${esc(x.call.name)}${x.call.skipped ? ' (skipped)' : ''}</span>` : ''
      }</span><span class="acc">${acc}</span></div>`;
    })
    .join('');
  if (r.truncated) h += `<div class="dim" style="padding:6px 10px">… ${(r.count - steps.length).toLocaleString()} more not kept</div>`;
  return h;
}

// What it reached for that nobody's mapped, and how it went wrong
function errorsHtml(r) {
  const errs = r.errors || [];
  if (!errs.length) return '<div class="green dkmsg">It found everything it reached for.</div>';
  return errs
    .map((e) => {
      if (e.kind === 'unmapped') {
        const t = (r.touched || []).find((x) => x.obj === e.obj && x.off === e.off);
        return `<div class="need bad"><div><span class="mono">${esc(e.struct)}+${hx(e.off)}</span> <span class="muted">${t ? (t.writes ? (t.reads ? 'read and written' : 'written') : 'read') : ''}</span></div><div class="nacts"><span class="clickable" data-act="binShow" data-arg="${
          e.at
        }">where</span><span class="clickable blue" data-act="tryName" data-arg="${esc(e.struct)}|${e.off}|${t ? t.type : 'int'}">Name it</span><span class="clickable" data-act="tryFill" data-arg="${esc(e.struct)}|${e.off}">Give a value</span></div></div>`;
      }
      return `<div class="need bad"><div><span class="badge red">${esc(e.kind)}</span> ${esc(e.text)}</div><div class="nacts"><span class="clickable" data-act="binShow" data-arg="${e.at}">where</span></div></div>`;
    })
    .join('');
}

async function binRun(st, zoo, key) {
  const s = sc();
  const sel = s && s.sel;
  if (!sel || BIN.key !== key) return;
  BIN.running = true;
  renderStepView();
  try {
    const f = fnInfo(zoo);
    const structName = tryStructFor(sel, f);
    const objects = await tryObjects(sel, structName);
    const res = await lab.zooRun({ addr: hexKey(zoo), calls: TRY.calls, objects, names: tryNames(), ecx: 'obj:0', limit: 20000, keep: 4000 });
    if (BIN.key !== key) return; // (another step picked meanwhile)
    BIN.drawn = null;
    res.objects = objects;
    res.structName = structName;
    BIN.res = res;
    BIN.ranOn = `${sel.name || sel.kind} as ${structName}`;
    const bad = (res.steps || []).findIndex((x) => x.acc.some((a) => (a.k === 'obj' && !a.known) || a.k === 'nothing'));
    BIN.cur = bad >= 0 ? bad : 0;
  } catch (e) {
    BIN.res = { ok: false, errors: [{ kind: 'crash', text: String(e), at: 0 }], steps: [], touched: [], objects: [], count: 0 };
  }
  BIN.running = false;
  renderStepView();
}

// The list's width, dragged
function wireStepsLayout() {
  const g = $('dk-vgrip');
  if (!g || g.onmousedown) return;
  g.onmousedown = (e) => {
    const x0 = e.clientX,
      w0 = DK.listW;
    const move = (ev) => {
      DK.listW = Math.max(200, Math.min(window.innerWidth * 0.6, w0 + ev.clientX - x0));
      $('dk-v').style.setProperty('--listw', DK.listW + 'px');
    };
    const up = () => {
      window.removeEventListener('mousemove', move);
      window.removeEventListener('mouseup', up);
      store.set('dkListW', DK.listW);
    };
    window.addEventListener('mousemove', move);
    window.addEventListener('mouseup', up);
    e.preventDefault();
  };
}

Object.assign(ACTIONS, {
  // A pane on or off: split, two show (a third replaces the older); else one
  dkPane: (id) => {
    if (!DK.split) DK.panes = [id];
    else if (DK.panes.includes(id)) DK.panes = DK.panes.length > 1 ? DK.panes.filter((p) => p !== id) : DK.panes;
    else DK.panes = [...DK.panes, id].slice(-2);
    store.set('dkPanes', DK.panes);
    renderStepView();
  },
  dkSplit: () => {
    DK.split = !DK.split;
    if (!DK.split) DK.panes = DK.panes.slice(-1);
    else if (DK.panes.length < 2) DK.panes = DK.panes[0] === 'code' ? ['code', 'bin'] : ['code', DK.panes[0]];
    store.set('dkSplit', DK.split);
    store.set('dkPanes', DK.panes);
    renderStepView();
  },
  dkBig: () => {
    DK.big = !DK.big;
    if (DK.big) {
      DK.smallHeight = DK.height;
      DK.height = Math.max(DK.height, window.innerHeight - 300);
    } else DK.height = DK.smallHeight || 260;
    $('console').style.height = DK.height + 'px';
    sizeCanvas();
    renderStepView();
  },
  binStep: (i) => {
    BIN.cur = +i;
    renderStepView();
  },
  binShow: (at) => {
    const i = (BIN.res && BIN.res.steps ? BIN.res.steps : []).findIndex((x) => x.a === +at);
    if (i < 0) return toast('That was past the instructions kept.');
    BIN.cur = i;
    if (!DK.panes.includes('bin')) ACTIONS.dkPane('bin');
    renderStepView();
  },
  binRerun: () => {
    BIN.key = null;
    renderStepView();
  },
});
