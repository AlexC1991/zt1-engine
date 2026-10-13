// ZT1 Lab: where everything is.
//
// The Lab is run two ways, and this is the only place that knows which:
//  - from a checkout (ZT1 Lab.cmd, npm start): the app sits in lab-app/ and
//    the whole tree - src/, re/, build/, vendor/ - is the folder above it.
//  - installed (ZT1-Lab-Setup): the app sits in an app.asar of its own and
//    the checkout is wherever the player pointed the installer at, so
//    nothing may be found by looking up from __dirname.
//
// So: the checkout is told (or taken from the folder above, or found beside
// the install), and what the app carries by itself is a small tree of its
// own inside the install - tree/re (the Lab's Python tools and the
// knowledge base as released), tree/engine (the engine built for the
// player), tree/clangd, tree/ghidra. It keeps the checkout's own layout
// (re/, engine/) so the tools, which find their files by looking up from
// their own path, work the same either way.
//
// What that means in practice: with a checkout, everything is read from it,
// exactly as it always was; without one, from the installed tree. A file
// that is in neither is null - what a view says is missing - never a guess.
const fs = require('fs');
const path = require('path');

const HERE = __dirname; // (the app: lab-app/ developed, app.asar once packed)
const PACKED = HERE.includes('app.asar');
const APP = HERE;
// The install's folder: what the installer (and the State file it leaves)
// calls the app's folder, and where tree/ is. Developed, that's the checkout
// above lab-app/ - never trusted as a root by itself
const SIDE = PACKED ? path.dirname(path.dirname(HERE)) : path.dirname(HERE);
// (a folder the app must be able to read and write while it runs)
const unpacked = (p) => (p && p.includes('app.asar') ? p.replace('app.asar', 'app.asar.unpacked') : p);

const exists = (p) => !!p && fs.existsSync(p);
const isDir = (p) => {
  try {
    return fs.statSync(p).isDirectory();
  } catch {
    return false;
  }
};
const writeTxt = (p, text) => {
  try {
    fs.mkdirSync(path.dirname(p), { recursive: true });
    fs.writeFileSync(p, text);
  } catch {}
};
const readTxt = (p) => {
  try {
    return fs.readFileSync(p, 'utf8').trim();
  } catch {
    return '';
  }
};
const json = (p, fallback) => {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    return fallback;
  }
};
// (the first of these that exists, else null)
const first = (paths) => paths.find(exists) || null;

// The pieces of a checkout the Lab reads: a folder is one when it has src/
// (or re/) and the flags clangd needs - a clone, an extracted release zip
const looksLikeCheckout = (dir) => {
  if (!isDir(dir)) return false;
  return exists(path.join(dir, 'src')) || (exists(path.join(dir, 're', 'knowledge.json')) && exists(path.join(dir, 'compile_flags.txt')));
};

// ------------------------------------------------------------ the checkout
// 1. what the app decided last time (it saves it, so a player can point the
//    Lab at a checkout anywhere and it comes back to it)
// 2. what this run was told: start.bat, a shortcut, or the MCP
// 3. the folder above the app, when it looks like a tree (developed in one)
// 4. the usual places a checkout is left, beside the install
const LOOSE = ['zt1-engine', 'ZT1-Engine', 'zt1-engine-master', 'zt1-engine-main'];
function findCheckout(statePath) {
  const said = process.env.ZT1_LAB_REPO;
  const saved = savedCheckout(statePath);
  const loose = LOOSE.map((n) => path.join(SIDE, n));
  const all = [said, saved, SIDE, ...loose].filter((p) => p && looksLikeCheckout(p));
  const root = all[0] || null;
  return { root, from: !root ? '' : root === said ? 'told' : root === saved ? 'saved' : root === SIDE ? 'above' : 'beside' };
}

// The checkout saved in the State file. Its format is the one the installer
// reads and writes - `key=value` lines, because that is what Inno Setup can
// write without a JSON library:
//
//   app=C:\path\lab
//   repo=C:\path\zt1-engine
//
// (JSON is read too: an older build of the Lab wrote {"repo": "..."} there, and
//  a player who has one must not lose the checkout they chose.)
function savedCheckout(statePath) {
  const text = readTxt(statePath);
  if (!text) return '';
  if (text[0] === '{') {
    try {
      return String(JSON.parse(text).repo || '');
    } catch {}
  }
  for (const line of text.split(/\r?\n/)) {
    const eq = line.indexOf('=');
    if (eq > 0 && line.slice(0, eq).trim() === 'repo') return line.slice(eq + 1).trim();
  }
  return '';
}

module.exports = function roots({ appData, resources }) {
  // (the app's own state. Where the Lab keeps what it found for the player -
  //  where zoo.exe is, which checkout, what it recorded - never the knowledge
  //  base. It is zt1-engine's folder when there is one: that is where the
  //  engine keeps its own settings, mods and saves, and where the installer
  //  writes the checkout the player chose at the wizard - so the Lab's state
  //  and the installer's choice sit together. LocalAppData is where that
  //  folder always is (the engine's installer and this one both put it there),
  //  so it is not guessed from the app's own path: an installed Lab's appData
  //  is under Roaming, and looking above it would never find the engine's.)
  const LOCAL = process.env.LOCALAPPDATA || '';
  const engineState = LOCAL ? path.join(LOCAL, 'zt1-engine') : '';
  const beside = path.join(path.dirname(appData), 'zt1-engine');
  // (an installed Lab was told at the wizard which checkout it has, and the
  //  installer puts that - and the engine's own settings - under LocalAppData.
  //  So for a packed build the installer's folder comes first: a stale or
  //  unrelated %APPDATA%\zt1-engine must not make an installed Lab ignore the
  //  choice and write its recordings somewhere else. Run from a checkout, the
  //  standing order is kept - the checkout's folder is found elsewhere anyway.
  //  `SIDE`/`PACKED` are only known later, so the test is on the caller's
  //  appData being under Roaming)
  const order = PACKED ? [engineState, beside, appData] : [beside, engineState, appData];
  const state = order.find(isDir) || appData;

  // What the app carries by itself: tree/ in the install's folder (beside
  // the app), and - developed in a checkout - lab-app/ itself
  const dirs = [SIDE, APP].filter((p) => p && isDir(p));
  const shippedTree = first(dirs.map((d) => path.join(d, 'tree')));
  const tree = (name) => (shippedTree ? path.join(shippedTree, name) : null);
  const inTree = (...p) => {
    const base = tree('re');
    return base ? path.join(base, ...p) : null;
  };

  const found = findCheckout(path.join(state, 'lab-roots.json'));
  const REPO = found.root;
  // (a State table, for the installer to read back: where the Lab is, so an
  // upgrade and the uninstaller find the same folder. Only the installed app
  // writes one: a run from a checkout leaves no marks outside it)
  if (PACKED) writeTxt(path.join(state, 'lab-paths.txt'), `app=${APP}\ninstall=${SIDE}\nrepo=${REPO || ''}\n`);

  // The two places our own files can be: the checkout's re/, then the
  // installed tree's. Everything of ours is read from the first that has it
  // (with a checkout, that's the checkout: the Lab works on live data)
  const reDirs = [REPO && path.join(REPO, 're'), tree('re')].filter(isDir);
  const at = (...parts) => {
    for (const d of reDirs) {
      const f = path.join(d, ...parts);
      if (exists(f)) return f;
    }
    return reDirs.length ? path.join(reDirs[0], ...parts) : null;
  };
  const engineDir = tree('engine');
  const clangdBase = tree('clangd');
  // The Python the Lab carries (tree/python), for an install on a machine
  // with none of its own. Nothing here without it: the caller falls back
  const pythonDir = tree('python');
  const python =
    pythonDir && exists(path.join(pythonDir, 'python.exe'))
      ? path.join(pythonDir, 'python.exe')
      : null;
  // Ghidra: the install the Lab carries (tree/ghidra), or null. The Lab only
  // reads it - a project still has to be made from the player's own zoo.exe,
  // because a project is built out of that file and cannot be shipped
  const ghidraBase = tree('ghidra');
  const hasHeadless = (d) => exists(path.join(d, 'support', 'analyzeHeadless.bat'));
  const ghidraHome = (() => {
    if (!ghidraBase) return null;
    if (hasHeadless(ghidraBase)) return ghidraBase;
    // (the releases unpack to ghidra_<version>_PUBLIC/: take that level too)
    try {
      for (const d of fs.readdirSync(ghidraBase)) {
        const p = path.join(ghidraBase, d);
        if (hasHeadless(p)) return p;
      }
    } catch {}
    return null;
  })();
  // Java, when the Lab carries one: Ghidra runs on the JDK, and a machine with
  // none cannot start it. Kept beside the Ghidra it is for
  const javaHome = first([
    tree('java'),
    ghidraBase && path.join(ghidraBase, 'java'),
    ghidraBase && path.join(ghidraBase, 'jdk'),
  ]);
  const clangd = (() => {
    if (!clangdBase) return null;
    if (exists(path.join(clangdBase, 'bin', 'clangd.exe'))) return path.join(clangdBase, 'bin', 'clangd.exe');
    try {
      for (const d of fs.readdirSync(clangdBase)) {
        const p = path.join(clangdBase, d, 'bin', 'clangd.exe');
        if (exists(p)) return p;
      }
    } catch {}
    return null;
  })();
  // The engine: a checkout's own build (64-bit first - the 32-bit one has
  // 2 GB, and a busy zoo's guests filled it - then the 32-bit tree), and the
  // installed one when the checkout has no build of its own
  const engines = [
    REPO && path.join(REPO, 'build-x64', 'Release', 'zt1-engine.exe'),
    REPO && path.join(REPO, 'build', 'Release', 'zt1-engine.exe'),
    engineDir && path.join(engineDir, 'zt1-engine.exe'),
  ];

  return {
    repo: REPO,
    repoFrom: found.from,
    app: APP,
    packed: PACKED,
    state,
    resources: unpacked(resources),
    tree: shippedTree,
    data: reDirs[0] || null,
    reDirs,
    at,
    engineDir,
    python,
    pythonDir,
    ghidraHome,
    javaHome,
    engineExe: first(engines),
    engineSources: engines,
    engines,
    clangd,
    clangdBase,
    // (developed in a checkout, the Lab's own clangd/ is still where it is:
    // lab-app/clangd, beside the app)
    clangdInApp: first([path.join(APP, 'clangd')]),
    ghidra: tree('ghidra'),
    // (everything of ours that only a checkout has: src/, vendor/, the
    // build, the engine's flags. A Lab on its own has these null)
    originalExe: first([REPO && path.join(REPO, 'build', 'Release', 'zt1-original.exe'), engineDir && path.join(engineDir, 'zt1-original.exe')]),
    compileFlags: REPO && path.join(REPO, 'compile_flags.txt'),
    remember() {
      // (the State file, in the installer's own format - see savedCheckout.
      //  `app` is kept so an upgrade and the uninstaller know the folder)
      writeTxt(path.join(state, 'lab-roots.json'), `app=${APP}\nrepo=${REPO || ''}\n`);
    },
    // (what to say when something the Lab needs isn't anywhere)
    looked(what) {
      const where = [...(shippedTree ? [shippedTree] : []), ...reDirs];
      if (engineDir) where.push(engineDir);
      return `${what}: not in ${where.join(', ') || 'any Lab folder'}${REPO ? `, nor in ${REPO}` : ' (no checkout is set)'}`;
    },
  };
};
