// ZT1 Lab: the original's recorder (Play original). Which of zoo.exe's
// functions run while you do something in the game - each matched with what's
// known of it - so playing decodes the game: "oh, this is that". main.js
// (orig:*) arms the hook's one-time breakpoints; this is its part of the Play
// bar and the window showing what ran. Learn idle first (what runs with nothing
// being done is left out after), then Record, do the one thing, Stop.

const REC = { status: null, timer: null };

// Its part of the Play bar (while the original runs): the buttons, or what's
// being recorded and how much has run so far
function recBar() {
  const st = REC.status;
  if (!S.play.running || S.play.kind !== 'original') return '';
  if (st && (st.active || st.learning))
    return `<span class="rec-live" title="Each function of zoo.exe that's run (the first time) since it started">● ${esc(st.learning ? 'Learning idle' : st.label)} · ${st.count}</span>${
      st.active ? `<div class="btn" data-act="recStop" title="Stop, and see what ran">Stop recording</div>` : ''
    }<div class="btn ghost" data-act="stepsToggle" title="The original's steps beside the game: each function as it runs, known ones in words, unknown ones guessed">Steps</div>`;
  const known = st ? ` title="${st.quiet} functions always running (left out); ${st.coverage}${st.total ? ' of ' + st.total : ''} seen running so far"` : '';
  return `<span id="orig-live" class="muted clickable" data-act="origCensus" title="The original's own state, read from its memory">${LIVE.last ? '' : 'reading…'}</span><div class="btn" data-act="recIdle"${known}>Learn idle</div><div class="btn" data-act="recStart" title="Record which of zoo.exe's functions run while you do one thing in the game">Record</div><div class="btn ghost" data-act="recList" title="Recordings so far">Recordings</div><div class="btn ghost" data-act="stepsToggle" title="The original's steps beside the game: each function as it runs, known ones in words, unknown ones guessed">Steps</div><div class="btn ghost" data-act="memOpen" title="Where the original keeps a value you can see (its cash): scan, change it, rescan - then which code writes it">Find a value</div>`;
}

// (the bar kept up to date while it's recording)
async function recPoll() {
  REC.status = await lab.recStatus();
  if (S.tab === 'play') {
    const el = $('rec-bar');
    if (el) el.innerHTML = recBar();
  }
  clearTimeout(REC.timer);
  if (REC.status.active || REC.status.learning) REC.timer = setTimeout(recPoll, 400);
}

// What ran: one line a function, in the order they first ran, with what's
// known - its name (ours, Ghidra's), our engine's code citing it, the game's
// own debug text in it (what the developers called it: ticked to name it so)
function showRecording(r) {
  const m = $('modal');
  const fns = r.functions || [];
  const named = fns.filter((f) => f.known || (f.ghidra && !/^FUN_/.test(f.ghidra))).length;
  const suggested = fns.filter((f) => f.suggest).length;
  const inEngine = fns.filter((f) => f.engine && f.engine.length).length;
  // (what ran here and in no other recording: the code that's this action's
  // own - the 8 of a refused placement's 136 - narrowed down)
  const elsewhere = (f) => (f.seenBefore || []).filter((w) => w !== r.label).length > 0;
  const own = fns.filter((f) => !elsewhere(f));
  const shown = REC.onlyOwn ? own : fns;
  const row = (f) => {
    const name = f.known ? f.known.name + (f.known.provisional ? ' <span class="muted">(provisional)</span>' : '') : /^FUN_/.test(f.ghidra) ? '' : esc(f.ghidra);
    const says = [];
    if (f.suggest)
      says.push(
        `<label title="Its own debug text names it: tick to name it so in the knowledge base"><input type="checkbox" data-name="${esc(f.address)}" value="${esc(f.suggest)}" checked> <b class="amber">${esc(f.suggest)}</b> <span class="muted">(its debug text)</span></label>`,
      );
    else if (f.debug && f.debug.length && !f.known) says.push(`<span class="muted">its text: ${esc(f.debug.join(', '))}</span>`);
    if (f.source) says.push(`<span title="The original's source file it's from (its developers' path, left in its text)">file: <span class="mono">${esc(f.source)}</span></span>`);
    if (!f.known && !f.suggest && f.texts && f.texts.length)
      says.push(`<span class="muted" title="The text it uses">uses: ${f.texts.map((t) => `<span class="mono">“${esc(t)}”</span>`).join(', ')}</span>`);
    if (f.engine && f.engine.length) says.push(`<span class="green" title="Our engine's code citing it">in our engine: ${esc(f.engine.join(', '))}</span>`);
    if (f.seenBefore && f.seenBefore.length) says.push(`<span class="muted">also ran when: ${esc(f.seenBefore.join('; '))}</span>`);
    if (f.known && f.known.summary) says.push(`<span class="muted">${esc(f.known.summary)}</span>`);
    return `<tr><td class="muted mono">${f.order}</td><td class="mono"><span class="blue clickable" data-go="${esc(f.address)}">${esc(f.address)}</span></td><td>${
      name || `<span class="muted">${esc(f.ghidra)}</span>`
    }</td><td>${says.join('<br>')}</td><td class="muted">${
      f.fromFunction ? `from <span class="clickable" data-go="${esc(f.fromFunction.address)}">${esc(f.fromFunction.name)}</span>` : ''
    }</td></tr>`;
  };
  m.innerHTML = `<div class="dialog" style="width:min(1100px,92vw);max-height:86vh;display:flex;flex-direction:column"><div class="h">What ran: ${esc(r.label)}</div><div class="muted" style="margin-bottom:8px">${fns.length} of zoo.exe's functions ran (the always-running ones left out): ${named} already named, ${suggested} named by their own debug text, ${inEngine} in our engine. Click an address to read it in Original Game.</div><label class="muted" style="margin-bottom:8px;display:block" title="Leave out what also ran in other recordings: what's left is this action's own code"><input type="checkbox" data-own="1"${REC.onlyOwn ? ' checked' : ''}> Only what ran in no other recording (${own.length})</label><div style="overflow:auto;flex:1"><table class="rec-table">${shown.map(row).join('')}</table></div><div class="btns"><div class="btn" data-x="no">Close</div><div class="btn" data-x="steps" title="This recording as steps beside the game: known ones in words, unknown ones guessed">Its steps</div><div class="btn primary" data-x="save" title="Every function here gets 'ran when: ${esc(r.label)}' in the knowledge base (ZT1_Apply shows it in Ghidra), and the ticked names">Save to knowledge base</div></div></div>`;
  m.classList.remove('hidden');
  sendPlayRect();
  m.onclick = async (e) => {
    const go = e.target.closest('[data-go]');
    if (e.target.dataset.own) {
      REC.onlyOwn = e.target.checked;
      return showRecording(r);
    }
    if (go || e.target.dataset.x === 'no' || e.target === m) {
      m.classList.add('hidden');
      m.innerHTML = '';
      sendPlayRect();
      if (go) ACTIONS.openCode(go.dataset.go);
    } else if (e.target.dataset.x === 'steps') {
      m.classList.add('hidden');
      m.innerHTML = '';
      ACTIONS.stepsOf(r.file);
    } else if (e.target.dataset.x === 'save') {
      // (the ticked names, and those of rows the filter hides - ticked by default)
      const names = [...m.querySelectorAll('input[data-name]:checked')].map((c) => ({ address: c.dataset.name, name: c.value }));
      if (REC.onlyOwn) for (const f of fns) if (f.suggest && elsewhere(f)) names.push({ address: f.address, name: f.suggest });
      const res = await lab.recSave(r.file, names);
      m.classList.add('hidden');
      m.innerHTML = '';
      sendPlayRect();
      toast(res.error ? esc(res.error) : `Saved: ${fns.length} functions seen running${names.length ? `, ${names.length} named` : ''}. Run ZT1_Apply in Ghidra to see them there.`, 5000);
    }
  };
}

Object.assign(ACTIONS, {
  // (seconds: how long to watch it idle)
  recIdle: async (seconds) => {
    toast('Learning idle: leave the game alone for 10 seconds…', 4000);
    const p = lab.learnIdle(+seconds || 10);
    setTimeout(recPoll, 300);
    const r = await p;
    recPoll();
    toast(r.error ? esc(r.error) : `Idle learnt: ${r.added} more always-running functions (${r.quiet} in all), left out of recordings.`, 5000);
    return r;
  },
  recStart: async (given) => {
    let label = (given || '').trim();
    if (!label) {
      const a = await ask('Record what runs', [{ key: 'label', label: 'What you’re about to do in the game', value: '', hint: 'Placing a fence' }], 'Start recording');
      if (!a || !a.label) return;
      label = a.label;
    }
    const r = await lab.recStart(label);
    recPoll();
    if (r.error) return toast(esc(r.error), 5000);
    toast(`Recording “${esc(label)}”: ${r.armed} functions watched. Do it in the game, then Stop recording.`, 5000);
    return r;
  },
  recStop: async () => {
    const r = await lab.recStop();
    recPoll();
    if (r.error) return toast(esc(r.error), 5000);
    showRecording(r);
    return { label: r.label, file: r.file, count: r.functions.length };
  },
  // For the Lab's tools - a recording in three calls, checked between:
  // recDo "<label>|<steps>" (record, then playSteps' lines), look at a shot,
  // then recKeep (saved, with the names its own debug text gives) or recDrop
  // (set aside in recordings/discarded/: the game refused, a month ended...)
  recDo: async (arg) => {
    const i = String(arg).indexOf('|');
    const label = (i < 0 ? arg : arg.slice(0, i)).trim();
    const r = await ACTIONS.recStart(label);
    if (!r || r.error) return r || { error: 'not started' };
    if (i >= 0) await ACTIONS.playSteps(arg.slice(i + 1));
    return { recording: label, armed: r.armed };
  },
  recKeep: async () => {
    const r = await lab.recStop();
    recPoll();
    if (r.error) return r;
    const fns = r.functions || [];
    const names = fns.filter((f) => f.suggest).map((f) => ({ address: f.address, name: f.suggest }));
    const s = await lab.recSave(r.file, names);
    const own = fns.filter((f) => !(f.seenBefore || []).filter((w) => w !== r.label).length);
    return s.error ? s : { saved: r.label, ran: fns.length, own: own.length, named: names.length, ownFirst: own.slice(0, 12).map((f) => `${f.address} ${f.known ? f.known.name : f.ghidra}`) };
  },
  recDrop: async () => {
    const r = await lab.recStop();
    recPoll();
    if (r.error) return r;
    return lab.recDiscard(r.file);
  },
  recList: async () => {
    const list = await lab.recList();
    const m = $('modal');
    m.innerHTML = `<div class="dialog" style="width:560px"><div class="h">Recordings</div>${
      list.length
        ? list.map((x) => `<div class="xref" data-file="${esc(x.file)}"><span>${esc(x.label)}</span><span class="muted">${x.count} functions · ${esc((x.on || '').slice(0, 16).replace('T', ' '))}</span></div>`).join('')
        : '<div class="muted">None yet: Record, do something in the game, Stop.</div>'
    }<div class="btns"><div class="btn" data-x="no">Close</div></div></div>`;
    m.classList.remove('hidden');
    sendPlayRect();
    m.onclick = async (e) => {
      const f = e.target.closest('[data-file]');
      if (f) return showRecording(await lab.recLoad(f.dataset.file));
      if (e.target.dataset.x === 'no' || e.target === m) {
        m.classList.add('hidden');
        m.innerHTML = '';
        sendPlayRect();
      }
    };
  },
});

// (the lines again, with what's just been named: the replay re-read, or the
// live one fetched now)
async function stepsRefresh() {
  if (STEPS.replay) STEPS.replay = await lab.origStepsOf(STEPS.replay.file);
  else STEPS.data = await lab.origSteps(0);
  renderSteps();
}

// ------------------------------------------------- the original's steps
// Beside the game in Play, as the Scene shows our engine's: each of zoo.exe's
// functions as it first runs while recording, in order. Mapped ones in words
// (its name, what it does, our engine's code for it); unmapped ones "unknown",
// with a guess and the evidence for it (main.js guessLines: who called it,
// what it called, what ran either side, what you were doing). A guess can be
// accepted: a provisional name, confidence "guess", by you.
const STEPS = { open: store.get('stepsOpen', false), sel: null, paused: false, data: null, filter: store.get('stepsFilter', 'all'), timer: null, replay: null, follow: true };

// One line opened (clicked): name it and say what it does, how sure; a name
// that's only a guess so far can be confirmed as it is
function stepsEditor(l) {
  const kn = l.known || null;
  const name = kn ? kn.name.split('::').pop() : l.isKnown ? l.ghidra : l.suggestName;
  const cls = kn && kn.name.includes('::') ? kn.name.split('::').slice(0, -1).join('::') : '';
  const what = (kn && kn.summary) || (l.isKnown ? l.engineSays || '' : l.guess ? 'Guess: ' + l.guess : '');
  const confirm = kn && kn.provisional ? `<span class="btn small primary" data-act="stepsConfirm" data-arg="${esc(l.address)}" title="Keep this name, as sure (no longer provisional)">Confirm “${esc(kn.name)}”</span>` : '';
  return `<div class="ps-edit" data-addr="${esc(l.address)}" data-act="stepsNoop">
    <label>Name <input class="field mono" data-k="name" value="${esc(name || '')}"></label>
    <label>Class <input class="field mono" data-k="cls" value="${esc(cls)}" placeholder="(none)" style="width:110px"></label>
    <label class="wide">What it does <input class="field" data-k="summary" value="${esc(what)}"></label>
    <div class="row">How sure: <select class="field" data-k="conf"><option value="sure">Sure (I read its code / saw it)</option><option value="likely" selected>Likely</option><option value="guess">A guess</option></select>
    <span class="btn small primary" data-act="stepsSave" data-arg="${esc(l.address)}">Save</span>${confirm}<span class="btn small ghost" data-act="openCode" data-arg="${esc(l.address)}">Read its code</span><span class="clickable muted" data-act="stepsOpen" data-arg="">close</span></div></div>`;
}

function stepsLine(l) {
  const t = l.n > 1 ? `<span class="t" title="Times it ran in the window shown">×${l.n}</span>` : `<span class="t">+${(l.ms / 1000).toFixed(1)}s</span>`;
  const caller = l.callerNames && l.callerNames.length > 1 ? ` <span class="muted">called by ${esc(l.callerNames.slice(0, 3).join(', '))}${l.callerNames.length > 3 ? ' +' + (l.callerNames.length - 3) : ''}</span>` : l.fromFunction ? ` <span class="muted">called by <span class="clickable" data-act="openCode" data-arg="${esc(l.fromFunction.address)}">${esc(l.fromFunction.name)}</span></span>` : '';
  if (l.isKnown) {
    const name = l.known ? l.known.name : l.ghidra;
    const what = (l.known && l.known.summary) || l.engineSays || '';
    const eng = (l.engine || []).slice(0, 2).map((e) => `<span class="clickable blue mono" data-act="engOpen" data-arg="${esc(e)}">${esc(e)}</span>`).join(', ');
    const prov = l.known && l.known.provisional ? ' <span class="ps-conf medium" title="A provisional name: a guess so far - click the line to confirm or rename it">guess</span>' : '';
    return `<div class="ps-line known${STEPS.sel === l.address ? ' sel' : ''}" data-act="stepsOpen" data-arg="${esc(l.address)}">${t}<b>${esc(name)}</b>${prov} <span class="muted mono">${esc(l.address)}</span>${caller}${what ? `<div class="what">${esc(what.slice(0, 200))}</div>` : ''}${eng ? `<div class="why">our engine: ${eng}</div>` : ''}${STEPS.sel === l.address ? stepsEditor(l) : ''}</div>`;
  }
  return `<div class="ps-line unk${STEPS.sel === l.address ? ' sel' : ''}" data-act="stepsOpen" data-arg="${esc(l.address)}">${t}<b>unknown ${esc(l.address)}</b> <span class="ps-conf ${l.conf}" title="How sure the guess is: high - a known function called it; medium - it calls known ones or its text names its file; low - only what ran around it">${l.conf}</span>${l.guess ? `<div class="what">Guess: ${esc(l.guess)}</div>` : ''}<div class="why">${esc((l.why || []).join(' · '))}</div>${STEPS.sel === l.address ? stepsEditor(l) : ''}</div>`;
}

function renderSteps() {
  const el = $('play-steps');
  if (!el) return;
  el.classList.toggle('hidden', !STEPS.open);
  if (!STEPS.open) return;
  const d = STEPS.replay || STEPS.data;
  let lines = (d && d.lines) || [];
  // (live: each function once - its latest line - with how often it ran in
  // the window and who called it; "Every call" shows them all)
  if (!STEPS.replay && !STEPS.everyCall) {
    const by = new Map();
    for (const l of lines) {
      const g = by.get(l.address);
      const callers = new Set(g ? g.callerNames : []);
      if (l.fromFunction) callers.add(l.fromFunction.name);
      by.set(l.address, { ...l, n: (g ? g.n : 0) + 1, callerNames: [...callers] });
    }
    lines = [...by.values()];
  }
  const gen = lines.filter((l) => l.general).length;
  const shown = lines.filter((l) => (STEPS.general || !l.general) && (STEPS.filter === 'all' || (STEPS.filter === 'unknown' ? !l.isKnown : l.isKnown)));
  const unk = lines.filter((l) => !l.isKnown && !l.general).length;
  const chip = (k, label) => `<span class="chip clickable${STEPS.filter === k ? ' on' : ''}" data-act="stepsFilter" data-arg="${k}">${label}</span>`;
  const head = STEPS.replay
    ? `<b>Steps of a recording</b><span class="muted">${esc(STEPS.replay.label)}</span><span class="clickable blue" data-act="stepsLive">back to live</span>`
    : `<b>The original's steps</b><span class="muted">${STEPS.paused ? '❚❚ paused - the log holds still' : d && d.watching ? '● live - what the game is running now, each at most once a second' : d && d.active ? '● recording: ' + esc(d.label) : S.play.running && S.play.kind === 'original' ? 'starting…' : 'Start Play original: what it runs shows here, live'}</span>`;
  const live = !STEPS.replay && S.play.running && S.play.kind === 'original';
  const pause = live ? `<span class="btn small${STEPS.paused ? ' primary' : ''}" data-act="stepsPause" title="${STEPS.paused ? 'Unpause the game; the log goes on' : 'Pause the game (its own pause button) and hold the log still, to go through it'}">${STEPS.paused ? '▶ Resume' : '❚❚ Pause game and log'}</span>` : '';
  el.innerHTML = `<div class="ps-head">${head}<span style="flex:1"></span>${pause}<span class="clickable muted" data-act="stepsToggle">close</span></div><div class="ps-head">${chip('all', `All ${lines.length}`)}${chip('unknown', `Unknown ${unk}`)}${chip('known', `Known ${lines.length - unk - gen}`)}${STEPS.replay ? '' : `<span class="chip clickable${STEPS.everyCall ? ' on' : ''}" data-act="stepsEvery" title="Every call in order, not each function once">Every call</span>`}<span class="chip clickable${STEPS.general ? ' on' : ''}" data-act="stepsGeneral" title="Code that runs for nearly everything recorded (input, drawing, sound): hidden unless you want it">General code ${gen}</span></div><div class="ps-list" id="ps-list">${
    shown.map(stepsLine).join('') || '<div class="muted" style="padding:10px">Nothing yet. Each function the game runs (the first time, while recording) shows here: known ones in words, unknown ones with a guess from what ran around them. Learn idle first, so what always runs is left out.</div>'
  }</div><div class="ps-foot muted">${STEPS.paused ? '<b class="amber">Paused - click a line to name it or confirm it.</b> ' : 'Click a line to name it. '}${lines.length - unk - gen} known, ${unk} unknown, ${gen} general code ${STEPS.general ? 'shown' : 'hidden'}. Guesses come from what's around each one - check before you trust them.</div>`;
  // (open with nothing polling - the Lab just started, a replay closed: start)
  if (!STEPS.replay && !STEPS.polling) stepsPoll();
  const list = $('ps-list');
  if (STEPS.follow && !STEPS.replay) list.scrollTop = list.scrollHeight;
  list.onscroll = () => (STEPS.follow = list.scrollTop + list.clientHeight >= list.scrollHeight - 30);
}

async function stepsPoll() {
  clearTimeout(STEPS.timer);
  STEPS.polling = !!(STEPS.open && !STEPS.replay);
  if (!STEPS.polling) return;
  // (paused, or a line open being named: the list holds still)
  // (but not empty: after the Lab reloads, paused, it's read once)
  if ((STEPS.paused || STEPS.sel) && STEPS.data && STEPS.data.lines && STEPS.data.lines.length) {
    STEPS.timer = setTimeout(stepsPoll, 1000);
    return;
  }
  // (open, the original running, nothing recorded: watch it live)
  if (S.play.running && S.play.kind === 'original' && !(REC.status && (REC.status.active || REC.status.learning))) await lab.origWatch(true);
  STEPS.data = await lab.origSteps(0);
  if (S.tab === 'play') renderSteps();
  STEPS.timer = setTimeout(stepsPoll, STEPS.data.active ? 1000 : 2000);
}

Object.assign(ACTIONS, {
  stepsToggle: () => {
    STEPS.open = !STEPS.open;
    STEPS.replay = null;
    store.set('stepsOpen', STEPS.open);
    if (!STEPS.open) lab.origWatch(false);
    renderSteps();
    sendPlayRect();
    stepsPoll();
    return { open: STEPS.open };
  },
  stepsFilter: (k) => {
    STEPS.filter = k;
    store.set('stepsFilter', k);
    renderSteps();
  },
  stepsPause: async () => {
    // (whether it's running, from the game itself: its clock moves about two
    // game hours a real second - so its own pause button, pressed by hand,
    // never gets us out of step)
    const clock = async () => {
      const l = await ACTIONS.origLive();
      return l && l.date ? `${l.date.day} ${l.date.hour}:${l.date.minute}` : '';
    };
    const a = await clock();
    await new Promise((r) => setTimeout(r, 700));
    const running = a !== (await clock());
    // (always the opposite of what the game's doing now: running, pause it;
    // paused, by its button or a reload of the Lab, resume it)
    const r = await lab.origPauseGame(running);
    if (r.error) return toast(esc(r.error), 4000);
    STEPS.paused = r.paused;
    if (!STEPS.paused) STEPS.sel = null;
    // (paused: the list held is what ran up to the pause)
    else STEPS.data = await lab.origSteps(0);
    renderSteps();
    return r;
  },
  // A line opened ("<address>"), or closed ("")
  stepsOpen: (address) => {
    if (document.activeElement && document.activeElement.closest && document.activeElement.closest('.ps-edit') && address === STEPS.sel) return;
    STEPS.sel = STEPS.sel === address ? null : address || null;
    renderSteps();
    const ed = document.querySelector('.ps-edit input[data-k="name"]');
    if (ed) ed.focus();
  },
  // Its name and what it does saved ("<address>"; or "<address>|<name>|<summary>|<sure|likely|guess>" for the Lab's tools)
  stepsSave: async (arg) => {
    const [address, n, sum, conf, byGiven, clsGiven] = String(arg).split('|');
    const box = document.querySelector(`.ps-edit[data-addr="${address}"]`);
    const get = (k) => (box ? box.querySelector(`[data-k="${k}"]`).value.trim() : '');
    const name = (n || get('name')).replace(/[^\w]/g, '_');
    if (!name) return toast('Give it a name first.', 3000);
    // (what you map goes under your name: asked once)
    if (!n && !(await ensureMe())) return;
    const r = await lab.origNameFn(address, name, sum !== undefined ? sum : get('summary').replace(/^Guess: /, ''), conf || get('conf') || 'likely', clsGiven || (box ? get('cls') : ''), byGiven || S.me || '');
    toast(r.error ? esc(r.error) : `Saved <b>${esc(name)}</b>.`, 3000);
    STEPS.sel = null;
    await stepsRefresh();
    return r;
  },
  // Several at once: one "address|name|summary|confidence|by|class" a line -
  // a family read off one table (every behaviour function a dispatcher
  // calls by name), pasted as a list
  namesSave: async (arg) => {
    const rows = String(arg).split(/\r?\n/).map((x) => x.trim()).filter((x) => x && !x.startsWith('#'));
    // ("by: <who>" first: everyone's by, unless a row says)
    const byAll = /^by:/i.test(rows[0] || '') ? rows.shift().slice(3).trim() : '';
    const saved = [];
    const failed = [];
    for (const row of rows) {
      const [address, n, sum, conf, by, cls] = row.split('|');
      const name = (n || '').replace(/[^\w]/g, '_');
      if (!/^0x[0-9a-f]+$/i.test(address || '') || !name) {
        failed.push(row);
        continue;
      }
      const r = await lab.origNameFn(address.toLowerCase(), name, sum || '', conf || 'likely', cls || '', by || byAll || S.me || '');
      (r && r.error ? failed : saved).push(r && r.error ? `${row}: ${r.error}` : `${address} ${name}`);
    }
    toast(`Saved ${saved.length}${failed.length ? `, ${failed.length} not` : ''}.`, 4000);
    if (STEPS.open) await stepsRefresh();
    return { saved, failed };
  },
  // A provisional name confirmed as it is ("<address>")
  stepsConfirm: async (address) => {
    const d = STEPS.replay || STEPS.data;
    const l = d && d.lines.find((x) => x.address === address);
    if (!l || !l.known) return { error: 'nothing named there' };
    if (!(await ensureMe())) return;
    const parts = l.known.name.split('::');
    const r = await lab.origNameFn(address, parts.pop(), l.known.summary || '', 'sure', parts.join('::'), S.me || '');
    toast(r.error ? esc(r.error) : `Confirmed <b>${esc(l.known.name)}</b>.`, 3000);
    STEPS.sel = null;
    await stepsRefresh();
    return r;
  },
  // For the Lab's tools: the panel's lines now, in short ("unknown", "known" or all)
  stepsList: (which) => {
    const d = STEPS.replay || STEPS.data;
    const seen = new Set();
    const lines = ((d && d.lines) || []).slice().reverse().filter((l) => !seen.has(l.address) && seen.add(l.address)).reverse().filter((l) => !l.general && (!which || (which === 'unknown' ? !l.isKnown : l.isKnown)));
    return { paused: STEPS.paused, total: lines.length, lines: lines.map((l) => (l.isKnown ? `${l.address} KNOWN ${l.known ? l.known.name + (l.known.provisional ? ' (guess)' : '') : l.ghidra}` : `${l.address} [${l.conf}] ${l.guess} | ${(l.why || []).join(' · ')}`)) };
  },
  // (a click inside a line's form stays there)
  stepsNoop: () => {},
  stepsEvery: () => {
    STEPS.everyCall = !STEPS.everyCall;
    renderSteps();
  },
  stepsGeneral: () => {
    STEPS.general = !STEPS.general;
    renderSteps();
  },
  stepsLive: () => {
    STEPS.replay = null;
    stepsPoll();
  },
  // A recording's steps, in the panel ("<file>")
  stepsOf: async (file) => {
    STEPS.replay = await lab.origStepsOf(file);
    STEPS.open = true;
    if (S.tab !== 'play') setTab('play');
    renderSteps();
    sendPlayRect();
    const lines = STEPS.replay.lines || [];
    return { label: STEPS.replay.label, known: lines.filter((l) => l.isKnown).length, general: lines.filter((l) => l.general).length, unknown: lines.filter((l) => !l.isKnown && !l.general).map((l) => `${l.address} [${l.conf}] ${l.guess} | ${(l.why || []).join(' · ')}`).slice(0, 40) };
  },
  // A guess made a provisional name ("<address>" asks for the name; "<address>|<name>" doesn't)
  stepsAccept: async (arg) => {
    const [address, given] = String(arg).split('|');
    const d = STEPS.replay || STEPS.data;
    const l = d && d.lines.find((x) => x.address === address);
    if (!l) return { error: 'not in the steps' };
    let name = given;
    if (!name) {
      const a = await ask('Name it (a guess)', [{ key: 'name', label: `What ${address} probably is - ${l.guess}`, value: l.suggestName, hint: l.suggestName }], 'Save the guess');
      if (!a || !a.name) return;
      name = a.name;
    }
    const why = `Guess from the original's steps (${d.label}): ${l.guess} - ${(l.why || []).join('; ')}`;
    const r = await lab.acceptGuess(address, name.replace(/[^\w:]/g, '_'), why, S.me || '');
    toast(r.error ? esc(r.error) : `Saved <b>${esc(name)}</b> as a guess (provisional).`, 4000);
    if (STEPS.replay) STEPS.replay = await lab.origStepsOf(STEPS.replay.file);
    else stepsPoll();
    renderSteps();
    return r;
  },
});
