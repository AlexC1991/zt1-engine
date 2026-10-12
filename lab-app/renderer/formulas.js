// Formula checks: zoo.exe's own function (run in re/lab/zoorun.py on objects
// laid out from each case's inputs) against our engine's (the engine's
// "formula" command: the very function the game calls), case by case.
// re/lab/formulas.json says what to run; the last results are kept in
// re/private/formula-results.json. Run from Explore > Code 1:1, and by
// themselves after every build that works.
const FX = { checks: null, results: null, running: false, wait: null };

async function fxLoad() {
  const r = await lab.formulasLoad();
  FX.checks = r.checks || [];
  if (!FX.results) FX.results = r.results;
}

// One case's objects and arguments, as zoorun takes them
function fxRequest(ch, c) {
  const input = (name) => c[ch.inputs.indexOf(name)];
  const objects = ch.objects.map((o) => ({
    name: o.name,
    struct: o.struct,
    size: parseInt(o.size || '0x400', 16),
    fields: o.fields.map(([off, type, what]) => {
      const f = { off: parseInt(off, 16), type };
      if (what.startsWith('=')) f.value = Number(what.slice(1));
      else if (what.startsWith('->')) f.to = +what.slice(2);
      else f.value = input(what);
      return f;
    }),
  }));
  const args = (ch.args || []).map(([type, name]) => {
    if (type !== 'float') return input(name);
    const dv = new DataView(new ArrayBuffer(4));
    dv.setFloat32(0, input(name), true);
    return dv.getUint32(0, true);
  });
  const req = { addr: ch.zoo, objects, args, calls: ch.calls || 'skip', keep: 0 };
  if (ch.result.startsWith('field:')) {
    const [, o, off, t] = ch.result.split(':');
    req.readback = [[+o, parseInt(off, 16), t]];
  }
  return req;
}

// Our engine's answers for all of a check's cases at once
function fxEngine(name, cases) {
  return new Promise((resolve) => {
    if (S.engine.status !== 'connected') return resolve({ error: 'The engine isn’t running.' });
    FX.wait = (m) => resolve(m.name === name ? { values: m.values } : { error: 'answered for ' + m.name });
    lab.engineSend({ cmd: 'formula', name, cases });
    setTimeout(() => FX.wait && ((FX.wait = null), resolve({ error: 'The engine didn’t answer.' })), 15000);
  });
}
SCENE_MSG.formula = (m) => {
  const w = FX.wait;
  FX.wait = null;
  if (w) w(m);
};

async function fxRunCheck(ch) {
  const zoo = [];
  for (const c of ch.cases) {
    const r = await lab.zooRun(fxRequest(ch, c));
    const v = !r.ok ? null : ch.result === 'st0' ? r.st0 : ch.result === 'eax' ? r.eax : (r.readback || [])[0];
    zoo.push(v === undefined ? null : v);
  }
  const ours = await fxEngine(ch.id, ch.cases);
  const tol = ch.tolerance || 0;
  const rows = ch.cases.map((c, i) => {
    const z = zoo[i], o = ours.values ? ours.values[i] : null;
    return { in: c, zoo: z, ours: o, ok: z !== null && o !== null && Math.abs(z - o) <= tol };
  });
  return {
    id: ch.id, title: ch.title, zoo: ch.zoo, ours: ch.ours, oursAt: ch.oursAt, inputs: ch.inputs,
    error: ours.error || (zoo.some((z) => z === null) ? 'zoo.exe’s function didn’t finish for every case (try it in Original Game)' : ''),
    pass: rows.filter((r) => r.ok).length, cases: rows.length, rows,
  };
}

function fxSummary(R) {
  const all = R.checks.reduce((a, c) => a + c.cases, 0), ok = R.checks.reduce((a, c) => a + c.pass, 0);
  return { checks: R.checks.length, failing: R.checks.filter((c) => c.pass < c.cases).map((c) => c.id), cases: all, pass: ok };
}

// The section in Explore > Code 1:1
function fxSection() {
  const R = FX.results;
  const run = `<span class="btn small${FX.running ? '' : ' primary'}" data-act="fxRun">${FX.running ? 'Running…' : 'Run the checks'}</span>`;
  if (!R) return `<div class="ex-area">Formula checks <span class="muted">- zoo.exe’s function against ours, on the same numbers</span> ${run}</div><div class="muted">Not run yet.</div>`;
  const rows = R.checks
    .map((c) => {
      const good = c.pass === c.cases && !c.error;
      const open = EX.fidOpen === 'x:' + c.id;
      const bad = c.rows.filter((r) => !r.ok);
      const detail = open
        ? `<div class="ex-detail"><div><span class="ex-fn" data-act="openCode" data-arg="${c.zoo}">zoo.exe ${esc(c.zoo)}</span> against <span class="ex-fn in" data-act="engOpen" data-arg="${esc(c.oursAt)}:1">${esc(c.ours)}</span></div>${c.error ? `<div class="red">${esc(c.error)}</div>` : ''}<table class="mono" style="font-size:11.5px;margin-top:6px"><tr><th style="text-align:left">${c.inputs.map(esc).join(', ')}</th><th>zoo.exe</th><th>ours</th></tr>${(bad.length ? bad : c.rows)
            .slice(0, 40)
            .map((r) => `<tr class="${r.ok ? '' : 'red'}"><td>${r.in.join(', ')}</td><td style="padding:0 10px">${r.zoo === null ? '-' : +(+r.zoo).toFixed(4)}</td><td>${r.ours === null ? '-' : +(+r.ours).toFixed(4)}</td></tr>`)
            .join('')}</table>${bad.length ? '' : '<div class="muted">Every case the same.</div>'}</div>`
        : '';
      return `<div class="ex-act${open ? ' open' : ''}"><div class="ex-act-h clickable" data-act="exFidOpen" data-arg="x:${esc(c.id)}"><span class="${good ? 'green' : 'red'}">${good ? '✓' : '✗'}</span> <span>${esc(c.title)}</span><span class="spacer"></span><span class="muted">${c.pass} / ${c.cases}</span></div>${detail}</div>`;
    })
    .join('');
  const s = fxSummary(R);
  return `<div class="ex-area">Formula checks <span class="muted">- ${s.pass} of ${s.cases} cases the same as zoo.exe; run ${esc((R.at || '').replace('T', ' ').slice(0, 16))}</span> ${run}</div>${rows}`;
}

Object.assign(ACTIONS, {
  // Every formula check: zoo.exe's against ours; saved, and shown
  fxRun: async () => {
    if (FX.running) return { busy: true };
    FX.running = true;
    if (S.tab === 'explore') renderExplore();
    await fxLoad();
    const checks = [];
    for (const ch of FX.checks) checks.push(await fxRunCheck(ch));
    FX.results = { at: new Date().toISOString(), checks };
    FX.running = false;
    await lab.formulasSave(FX.results);
    if (S.tab === 'explore') renderExplore();
    const s = fxSummary(FX.results);
    toast(s.pass === s.cases ? `Formula checks: all ${s.cases} cases the same as zoo.exe.` : `Formula checks: ${s.cases - s.pass} of ${s.cases} cases differ from zoo.exe (${s.failing.join(', ')}) - Explore › Code 1:1`, s.pass === s.cases ? 3000 : 8000);
    return { ...s, results: checks.map((c) => ({ id: c.id, pass: c.pass, cases: c.cases, error: c.error, differ: c.rows.filter((r) => !r.ok).map((r) => `${r.in.join(',')}: zoo.exe ${r.zoo} ours ${r.ours}`) })) };
  },
});

// After a build that works, once the new engine's up: the checks again
lab.onBuildDone((r) => {
  if (!r.ok) return;
  // (the old engine goes and the new one comes: connected after a gap,
  // or after 15 s if the gap was missed)
  let tries = 0, gap = false;
  const wait = () => {
    if (S.engine.status !== 'connected') gap = true;
    else if (gap || tries > 15) return ACTIONS.fxRun();
    if (++tries < 120) setTimeout(wait, 1000);
  };
  setTimeout(wait, 1000);
});
fxLoad();
