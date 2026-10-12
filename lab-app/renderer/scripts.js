// Write Code > New script: engine code with the zoo's parts set up and handed
// over (src/EngineScripts.hpp) - made from a template in src/scripts/, built
// in, run on the Test Zoo from Write Code: once (Build & run), or each tick
// while it's switched on
const ES = { list: [], asked: 0, busy: '', pending: null };

const SCRIPT_KINDS = [
  ['once', 'Run once', 'A button: it runs when you press ▶ Build & run'],
  ['tick', 'Every tick', 'Each tick of the zoo, while it’s switched on'],
  ['animal', 'Each animal, every tick', 'For every animal, each tick, while it’s switched on'],
];

function scriptFnName(name) {
  const w = name.replace(/[^A-Za-z0-9 ]/g, ' ').trim().split(/\s+/).filter(Boolean);
  const s = w.map((x, i) => (i ? x[0].toUpperCase() + x.slice(1) : x[0].toLowerCase() + x.slice(1))).join('');
  return /^[A-Za-z_]/.test(s) ? s : 'script' + s;
}

// The file a new script starts as: every part of the zoo named at the top,
// and a small working example of its kind
function scriptTemplate(name, kind, about) {
  const fn = scriptFnName(name) || 'myScript';
  // (in a C++ string: quotes and backslashes kept as text)
  const cstr = (t) => t.replace(/\\/g, '\\\\').replace(/"/g, '\\"');
  const what = cstr(about || 'what it does');
  name = cstr(name);
  const head = `// ${name}: ${what}
//
// A Lab script (Write Code > New script): engine code, built in with the
// rest, run on the Test Zoo from Write Code - ${
    kind === 'once' ? 'once, each time you press ▶ Build & run' : 'each tick of the zoo while it’s switched on (its bar: On)'
  }.
// Set up for you, in zoo:
//   zoo.world                 the whole world (World.hpp)
//   zoo.animals               animals.members(), animals.member(id), animals.types()
//   zoo.guests                guests.guests(), guests.guest(id)
//   zoo.staff                 staff.members(), staff.member(id)
//   zoo.fences                fences.exhibits(), fences.exhibit(id)
//   zoo.map                   map.getTile(x, y)
//   zoo.clock, zoo.dt         seconds of play so far; seconds this tick (0: once)
//   zoo.animal                the animal picked in the Test Zoo (nullptr: none)
//   zoo.guest, zoo.staffMember, zoo.exhibit   ids picked (-1: none)
//   zoo.tileX, zoo.tileY      the tile picked, or under the mouse (-1: none)
//   zoo.say("...")            a line in the engine log, and a step in Steps
//   num(x), num(x, 2)         a number as words (23.5, not 23.500000)
// Copying zoo.exe? Cite it, and the Lab shows it in our engine:
//   ZT_STEP("0x<its address>", "What it does", {"name", value});
#include "../EngineScripts.hpp"
#include "../LabTrace.hpp"
#include "../World.hpp"

#include <cmath>
#include <string>

namespace {
`;
  const body = {
    once: `void ${fn}(Zoo &zoo) {
  // The animal picked in the Test Zoo (click one first)
  Animals::Member *a = zoo.animal;
  if (!a) {
    zoo.say("Pick an animal in the Test Zoo first");
    return;
  }
  const Animals::Type &type = zoo.animals.types()[a->type];
  zoo.say(a->name + " (" + type.file + ") at " + num(a->x) + ", " + num(a->y) + ": " + a->anim + ", hunger " + num(a->hunger, 0));
}
} // namespace

ZT_SCRIPT("${name}", "${what}", EngineScripts::Once, ${fn});
`,
    tick: `void ${fn}(Zoo &zoo) {
  // Every tick is a lot of ticks: this one speaks once a second
  // (and a new zoo - its clock back at 0 - starts it again)
  static double last = -1;
  if (zoo.clock >= last && zoo.clock - last < 1.0)
    return;
  last = zoo.clock;
  int loose = 0;
  for (const Animals::Member &m : zoo.animals.members())
    loose += m.escaped ? 1 : 0;
  zoo.say(std::to_string(zoo.animals.members().size()) + " animals, " + std::to_string(loose) + " loose, " +
          std::to_string(zoo.guests.guests().size()) + " guests");
}
} // namespace

ZT_SCRIPT("${name}", "${what}", EngineScripts::EveryTick, ${fn});
`,
    animal: `void ${fn}(Zoo &zoo, Animals::Member &m) {
  // For each animal, every tick. An example: the picked one never goes
  // hungry (pick it in the Test Zoo)
  if (zoo.animal && m.id == zoo.animal->id && m.hunger > 50) {
    m.hunger = 0;
    zoo.say(m.name + " fed");
  }
}
} // namespace

ZT_SCRIPT_EACH_ANIMAL("${name}", "${what}", ${fn});
`,
  }[kind];
  return head + body;
}

// The script a source file registers (its name and kind), if it's one
function scriptOf(rel) {
  if (!rel || !/^src\/.+\.cpp$/.test(rel) || !CodeEditor.isOpen(rel)) return null;
  const t = CodeEditor.textOf(rel) || '';
  const m = /ZT_SCRIPT(_EACH_ANIMAL)?\(\s*"([^"]+)"\s*,\s*"[^"]*"\s*,\s*(EngineScripts::(\w+))?/.exec(t);
  if (!m) return null;
  return { name: m[2], kind: m[1] ? 'animal' : m[4] === 'EveryTick' ? 'tick' : 'once' };
}

function scriptsAsk() {
  if (S.engine.status !== 'connected' || Date.now() - ES.asked < 1500) return;
  ES.asked = Date.now();
  lab.engineSend({ cmd: 'engineScripts' });
}

// The bar over an open script: what it is, and run it / switch it
function scriptBarHtml(rel) {
  const s = scriptOf(rel);
  if (!s) return '';
  scriptsAsk();
  const built = ES.list.find((x) => x.name === s.name);
  const kind = SCRIPT_KINDS.find((k) => k[0] === s.kind);
  const state = !built
    ? '<span class="amber">not built in yet: ▶ Build & run</span>'
    : `<span class="muted">${esc(kind[1])}${built.on ? ' · <span class="green">on</span>' : ''}</span>`;
  const btn =
    s.kind === 'once'
      ? `<div class="btn small primary${ES.busy ? ' disabled' : ''}" data-act="scriptRun" data-arg="${esc(s.name)}" title="Save, build the engine if it changed, and run it on the Test Zoo: what it says shows in Steps and the engine log">▶ Build & run</div>`
      : `<div class="btn small${built && built.on ? '' : ' primary'}${ES.busy ? ' disabled' : ''}" data-act="scriptToggle" data-arg="${esc(s.name)}" title="Switch it on (each tick of the Test Zoo) or off; built first if it changed">${
          built && built.on ? 'Switch off' : '▶ Build & switch on'
        }</div>`;
  return `<div class="engbar"><span class="muted">Script</span> <b>${esc(s.name)}</b> ${state}<div class="spacer"></div>${
    ES.busy ? `<span class="muted">${esc(ES.busy)}</span>` : ''
  }<div class="btn small ghost" data-act="scriptTestZoo" title="The Test Zoo, to pick what it works on and watch it">Test Zoo</div>${btn}</div>`;
}

SCENE_MSG.engineScripts = (m) => {
  ES.list = m.list || [];
  if (S.tab === 'engine') renderEngine();
};
SCENE_MSG.scriptRan = (m) => {
  ES.busy = '';
  toast(m.ok ? `Ran <b>${esc(m.name)}</b>: what it said is in Steps and the engine log.` : `No script called <b>${esc(m.name)}</b> in the engine.`, 3500);
  if (S.tab === 'engine') renderEngine();
};

// After a build the engine restarts: waited for, then the script run or
// switched on
function scriptAfterBuild(then) {
  ES.pending = then;
}
lab.onBuildDone((r) => {
  if (!ES.pending) return;
  const then = ES.pending;
  ES.pending = null;
  if (!r.ok) {
    ES.busy = '';
    S.eng.tab = 'probs';
    if (S.tab === 'engine') renderEngine();
    return;
  }
  ES.busy = 'Starting the engine on the new build…';
  const t0 = Date.now();
  let down = false;
  const wait = setInterval(() => {
    if (S.engine.status !== 'connected') down = true;
    if ((down && S.engine.status === 'connected') || Date.now() - t0 > 20000) {
      clearInterval(wait);
      // (the Test Zoo's set-up is made again first: then the script)
      setTimeout(() => {
        ES.busy = '';
        then();
      }, 2500);
    }
  }, 300);
});

async function scriptBuildThen(name, then) {
  // (saved; built only when a source file changed since the last build)
  const dirty = S.eng.opened.some((f) => !/^game:/.test(f) && CodeEditor.isDirty(f));
  // (the engine asked which it has, fresh - a Lab just started hasn't been
  // told yet - before it's built for nothing)
  if (!dirty && !ES.list.some((x) => x.name === name) && S.engine.status === 'connected') {
    const got = new Promise((r) => {
      const was = SCENE_MSG.engineScripts;
      SCENE_MSG.engineScripts = (m) => {
        SCENE_MSG.engineScripts = was;
        was(m);
        r();
      };
      setTimeout(r, 2000);
    });
    ES.asked = Date.now();
    lab.engineSend({ cmd: 'engineScripts' });
    await got;
  }
  const built = ES.list.some((x) => x.name === name);
  if (!dirty && built) return then();
  ES.busy = 'Building the engine…';
  scriptAfterBuild(then);
  if (S.tab === 'engine') renderEngine();
  await ACTIONS.engBuild();
}

Object.assign(ACTIONS, {
  // (given "Name|kind|about" - by code - it's made without asking)
  engNewScript: async (given) => {
    const g = typeof given === 'string' && given ? given.split('|') : null;
    const a = g ? { name: g[0], kind: g[1] || 'once', about: g[2] || '' } : await ask(
      'A new script',
      [
        { key: 'name', label: 'Its name', value: 'My script' },
        { key: 'about', label: 'What it does', value: '' },
        { key: 'kind', label: 'When it runs', options: SCRIPT_KINDS.map(([k, l, d]) => ({ value: k, label: `${l} - ${d}` })), value: 'once' },
      ],
      'Make it',
    );
    if (!a || !a.name) return;
    const name = a.name.trim();
    const kind = SCRIPT_KINDS.some((k) => k[0] === a.kind) ? a.kind : 'once';
    const file = name.replace(/[^A-Za-z0-9]+/g, ' ').trim().split(' ').map((w) => w[0].toUpperCase() + w.slice(1)).join('') || 'MyScript';
    const rel = `src/scripts/${file}.cpp`;
    const r = await lab.srcCreate(rel, scriptTemplate(name, kind, (a.about || '').trim()));
    if (!r.ok) return toast(esc(r.error), 4000);
    S.eng.files = null;
    toast(`Made <b>${esc(rel)}</b>: the zoo's parts are set up at the top. ▶ Build & run when it's ready.`, 4000);
    openEngineFile(rel, 1);
  },
  scriptRun: (name) =>
    scriptBuildThen(name, () => {
      ES.busy = 'Running…';
      lab.engineSend({ cmd: 'scriptRun', name });
      if (S.tab === 'engine') renderEngine();
    }),
  scriptToggle: async (name) => {
    // (on or off as the engine has it now, not as a page just reloaded
    // last heard)
    if (S.engine.status === 'connected') {
      const got = new Promise((r) => {
        const was = SCENE_MSG.engineScripts;
        SCENE_MSG.engineScripts = (m) => {
          SCENE_MSG.engineScripts = was;
          was(m);
          r();
        };
        setTimeout(r, 2000);
      });
      ES.asked = Date.now();
      lab.engineSend({ cmd: 'engineScripts' });
      await got;
    }
    const s = ES.list.find((x) => x.name === name);
    if (s && s.on) return lab.engineSend({ cmd: 'scriptOn', name, on: false });
    scriptBuildThen(name, () => lab.engineSend({ cmd: 'scriptOn', name, on: true }));
  },
  scriptTestZoo: () => setTab('scene'),
});
