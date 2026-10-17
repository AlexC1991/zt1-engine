// ZT1 Lab app: the main process. It starts our engine twice over:
//  - the Lab engine (zt1-engine.exe --lab-host): the lab zoo, mods (scripts)
//    built and stepped, its view drawn off screen and sent here as frames
//  - the game (zt1-engine.exe --embed <this window>): the whole game, its
//    window put inside this one (the Play view)
// and talks to Ghidra (the ZT1_LabBridge script, 127.0.0.1:18489), the
// knowledge base (re/knowledge.json, re/kb.py) and the mods' files.
const { app, BrowserWindow, ipcMain, shell, screen, nativeImage, dialog } = require('electron');
const path = require('path');
const fs = require('fs');
const net = require('net');
const http = require('http');
const cp = require('child_process');
const labUpdates = require('./update.js');

// Where everything is: the checkout (if there is one), and what the app
// carries itself. This file is the only one for that (runtime.js says how).
const roots = require('./runtime.js')({ appData: app.getPath('userData'), resources: process.resourcesPath });
const REPO = roots.repo;      // the engine's checkout, or null: installed alone
const DATA = roots.data;      // our files: the checkout's re/, else the install's
const TREE = roots.tree;      // what the app carries: re/, engine/, clangd/, ghidra/
// (the Lab's Python tools and the knowledge base, wherever they are: one
// path either way - a checkout's re/, or the released copy in the install)
const at = (...parts) => roots.at(...parts);
// (a state folder in the app's own data: recordings, Ghidra's project, what
// the Lab learns about the original game). Always there, checkout or not
// Everything of ours that only exists in the source tree: built there, read
// by the Lab. A Lab with no checkout has these empty (the views that need
// them say so)
function inRepo(...parts) {
  return REPO ? path.join(REPO, ...parts) : null;
}
// (a file of our own to read: wherever it is, else what it was given, so a
// failure reads as "no such file" rather than a crash)
const readMaybe = (p, fallback) => {
  try {
    return fs.readFileSync(p, 'utf8');
  } catch {
    return fallback;
  }
};
const keep = (p, make) => {
  try {
    make(p);
  } catch {}
  return p;
};
// (the Lab's Python tools and the knowledge base: the checkout's re/ first,
// then the released copy. One path works whether or not there's a checkout)
const RELEASE = REPO ? path.join(REPO, 'build', 'Release') : roots.engineDir;
const SCRIPTS = at('lab', 'scripts');
// (the shared files a checkout has and an installed Lab reads: what's found
// nowhere is null, and the views that need it say so rather than break)
const KNOWLEDGE = at('knowledge.json');
const REFS = at('engine_refs.csv');
const PY = at('lab', 'zoorun.py') && path.dirname(at('lab', 'zoorun.py'));
// (what a view says when the Lab has no checkout: only the things that edit
// or build the engine, or read its source, need one)
const NO_CHECKOUT =
  'The Lab isn’t pointed at an engine checkout: set its folder in %LocalAppData%\\zt1-engine\\lab-roots.json, or re-run the installer, to open one.';
// (of our own: recordings, Ghidra's project, what the Lab learns about the
// original game. A checkout, when there is one, keeps them in re/private/
// as it always did; a Lab of its own keeps them in its own data folder, so
// one is there either way)
const PRIVATE = REPO ? keep(path.join(REPO, 're', 'private'), (p) => fs.mkdirSync(p, { recursive: true })) : roots.state;
const GHIDRA_CACHE = PRIVATE && path.join(PRIVATE, 'ghidra');
// (the 64-bit build, when there's a checkout with one)
const BUILD64 = REPO ? path.join(REPO, 'build-x64') : null;
const HOST_PORT = 18490;
const GHIDRA_PORT = 18489;

// Which Python runs the Lab's tools: the one the Lab carries (tree/python,
// put there by the installer's build), else whatever 'python' the machine
// has. Carrying it means an install works on a PC with no Python at all - and
// with capstone, pefile and unicorn beside it, which the Scene view, Test Zoo
// and the function traces need. Kept as a path here and used at each call site
// (they pass 'python' today).
function labPython() {
  const carried = roots.python;
  if (carried && fs.existsSync(carried)) return carried;
  return 'python';
}

let win = null;

// The engine the Lab runs. From a checkout: the 64-bit build (build-x64/,
// as a release is built) when it's there - the 32-bit one has 2 GB, and a
// busy zoo's guests filled it (the view froze) - else the 32-bit one in
// build/Release, where the game's files are. Installed: the one the
// installer put in engine/, and its own folder holds the copy beside it.
function engineExe(role = 'lab') {
  // (the engine runs where it is, with the game's files beside it: the
  // checkout's build, or the install's engine/. The engines run from a copy -
  // zt1-engine-lab.exe, next to the real one so it finds its files - so a
  // build can replace zt1-engine.exe while they run; the copy is refreshed
  // whenever the build is newer. Of a checkout's, the 64-bit build first -
  // as a release is built - then the 32-bit one in build/Release, where the
  // game's files are)
  const sources = REPO
    ? [
        BUILD64 && path.join(BUILD64, 'Release', 'zt1-engine.exe'),
        RELEASE && path.join(RELEASE, 'zt1-engine.exe'),
        RELEASE && path.join(RELEASE, 'zt1-engine-dev.exe'),
        // A checkout can have the source without a built engine. The installer
        // carries its own engine, so keep that as the final fallback.
        roots.engineDir && path.join(roots.engineDir, 'zt1-engine.exe'),
      ]
    : roots.engineSources;
  const build64 = sources[0] && fs.existsSync(sources[0]) ? path.dirname(sources[0]) : null;
  const dir = build64 || roots.engineDir || RELEASE;
  if (!dir) return null;
  for (const p of sources) {
    if (!p || !fs.existsSync(p)) continue;
    try {
      const run = path.join(dir, `zt1-engine-${role}.exe`);
      const a = fs.existsSync(run) && fs.statSync(run), b = fs.statSync(p);
      if (a && a.mtimeMs >= b.mtimeMs && a.size === b.size) return run;
      // (the install's engine is run as it is - but only an installed Lab's:
      // a checkout's own build must run from the copy, or the next Build
      // can't replace the exe while the Lab's engine has it open - LNK1104)
      if (!REPO && p === roots.engineExe && !a) return p;
      fs.copyFileSync(p, run);
      return run;
    } catch {
      return fs.existsSync(path.join(dir, `zt1-engine-${role}.exe`)) ? path.join(dir, `zt1-engine-${role}.exe`) : p;
    }
  }
  return null;
}

function send(channel, data) {
  if (win && !win.isDestroyed()) win.webContents.send(channel, data);
}

// ------------------------------------------------------------- Lab engine
const host = { proc: null, sock: null, buf: Buffer.alloc(0), pieces: [], have: 0, frame: null, status: 'starting', starting: false };

function hostStatus(status, detail) {
  host.status = status;
  send('engine:status', { status, detail: detail || '' });
}

function startHost() {
  if (host.starting) return;
  host.starting = true;
  // (one already there - left from before - is used as it is)
  tryConnect(() => {
    const exe = engineExe();
    const where = roots.engineDir || RELEASE;
    if (!exe) {
      host.starting = false;
      return hostStatus('missing', `No zt1-engine.exe: ${roots.looked('the engine')}`);
    }
    hostStatus('starting', 'Starting the engine...');
    const proc = cp.spawn(exe, ['--lab-host', String(HOST_PORT)], { cwd: where, stdio: ['pipe', 'pipe', 'pipe'] });
    host.proc = proc;
    let out = '';
    proc.stdout.on('data', (d) => {
      out += d.toString();
      if (out.includes('ZT1LAB READY')) {
        out = '';
        tryConnect(() => hostStatus('failed', "The engine started but didn't answer."));
      }
    });
    proc.stderr.on('data', () => {});
    proc.on('exit', (code) => {
      // (an engine that's already been replaced - the old one after a
      // build, slow to quit - leaves the new one's connection alone)
      if (host.proc !== proc) return;
      host.proc = null;
      host.starting = false;
      if (host.sock) host.sock.destroy();
      host.sock = null;
      hostStatus('stopped', `The engine stopped (code ${code}).`);
    });
  });
}

function tryConnect(onFail) {
  const sock = net.createConnection({ host: '127.0.0.1', port: HOST_PORT });
  sock.setNoDelay(true);
  sock.once('connect', () => {
    host.sock = sock;
    host.buf = Buffer.alloc(0);
    host.pieces = [];
    host.have = 0;
    host.frame = null;
    host.starting = false;
    hostStatus('connected');
  });
  sock.once('error', () => {
    if (host.sock !== sock) onFail && onFail();
  });
  sock.on('data', (d) => onHostData(d));
  sock.on('close', () => {
    if (host.sock === sock) {
      host.sock = null;
      if (host.status === 'connected') hostStatus('stopped', 'Lost the engine.');
    }
  });
}

function onHostData(d) {
  // A frame's bytes (megabytes, in 64 KB pieces) are kept as pieces and
  // joined once, when they're all in: joining at every piece copied the
  // frame over and over, and the engine waited on it
  if (host.frame) {
    host.pieces.push(d);
    host.have += d.length;
    if (host.have < host.frame.n) return;
    d = Buffer.concat(host.pieces, host.have);
    host.pieces = [];
    host.have = 0;
  }
  host.buf = host.buf.length ? Buffer.concat([host.buf, d]) : d;
  for (;;) {
    if (host.frame) {
      if (host.buf.length < host.frame.n) {
        // (the rest of it still to come: in pieces again)
        host.pieces = [host.buf];
        host.have = host.buf.length;
        host.buf = Buffer.alloc(0);
        return;
      }
      const data = host.buf.subarray(0, host.frame.n);
      send('engine:frame', { w: host.frame.w, h: host.frame.h, fmt: host.frame.fmt || 'jpeg', data });
      host.buf = host.buf.subarray(host.frame.n);
      host.frame = null;
      continue;
    }
    const nl = host.buf.indexOf(10);
    if (nl < 0) return;
    const line = host.buf.subarray(0, nl).toString('utf8');
    host.buf = host.buf.subarray(nl + 1);
    if (!line) continue;
    let msg;
    try {
      msg = JSON.parse(line);
    } catch {
      continue;
    }
    if (msg.t === 'frame') host.frame = msg;
    else send('engine:msg', msg);
  }
}

ipcMain.on('engine:send', (_e, cmd) => {
  if (host.sock) host.sock.write(JSON.stringify(cmd) + '\n');
});
ipcMain.handle('lab:update', async () => {
  if (!app.isPackaged) return { status: 'unavailable', message: 'Updates are available in the installed release.' };
  let manifest;
  try {
    manifest = labUpdates.validate(await labUpdates.readManifest());
  } catch (e) {
    return { status: 'error', message: `Could not verify the update: ${e.message}` };
  }
  const current = app.getVersion();
  if (labUpdates.compareVersions(manifest.version, current) <= 0)
    return { status: 'current', message: `ZT1 Lab ${current} is up to date.` };
  const choice = await dialog.showMessageBox(win, {
    type: 'info', buttons: ['Download and install', 'Cancel'], defaultId: 0, cancelId: 1,
    title: 'ZT1 Lab update available',
    message: `ZT1 Lab ${manifest.version} is available (you have ${current}).`,
    detail: 'The installer is signed and will be checked before it runs. The Lab will close during installation.'
  });
  if (choice.response !== 0) return { status: 'cancelled', message: 'Update cancelled.' };
  const dir = fs.mkdtempSync(path.join(app.getPath('temp'), 'zt1-lab-update-'));
  const installer = path.join(dir, manifest.file);
  try {
    await labUpdates.download(manifest, installer);
    const proc = cp.spawn(installer, ['/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/RELAUNCHLAB=1'], { detached: false, stdio: 'ignore', windowsHide: true });
    await new Promise((resolve, reject) => {
      proc.once('error', reject);
      proc.once('spawn', resolve);
    });
    app.quit();
    return { status: 'installing', message: 'The update installer has started.' };
  } catch (e) {
    try { fs.rmSync(dir, {recursive:true, force:true}); } catch {}
    return { status: 'error', message: `Update failed: ${e.message}` };
  }
});
ipcMain.handle('engine:start', () => {
  if (!host.sock) startHost();
  // (a reloaded page asks again: told how things are, and the engine's
  // list of scripts and a frame come again)
  else host.sock.write('{"cmd":"scripts"}\n');
  return host.status;
});
ipcMain.handle('engine:restart', () => restartHost());

// The engine stopped and started again. The old one is waited for - gone,
// not just asked to go - before the new one starts: connecting in that gap
// reached the old one as it closed (a big zoo takes a while to quit), and
// the Lab said "stopped"
function restartHost() {
  const old = host.proc;
  const sock = host.sock;
  host.proc = null;
  host.sock = null;
  host.starting = false;
  if (sock) sock.write('{"cmd":"quit"}\n', () => sock.end());
  if (!old) return setTimeout(startHost, sock ? 1200 : 0);
  let started = false;
  const go = () => !started && ((started = true), setTimeout(startHost, 200));
  old.once('exit', go);
  // (one that won't go is stopped)
  setTimeout(() => old.exitCode === null && old.kill(), 6000);
  setTimeout(go, 8000);
}

// ------------------------------------------------------------------ Play
const play = { proc: null };

ipcMain.handle('play:start', () => {
  if (play.proc) return play.kind === 'engine' ? { ok: true } : { error: 'The original is running in Play: stop it first.' };
  const exe = engineExe('play');
  if (!exe) return { error: roots.looked('the engine') };
  const handle = win.getNativeWindowHandle();
  const hwnd = handle.length >= 8 ? handle.readBigUInt64LE(0) : BigInt(handle.readUInt32LE(0));
  const proc = cp.spawn(exe, ['--embed', hwnd.toString()], { cwd: roots.engineDir || RELEASE, stdio: ['pipe', 'pipe', 'pipe'] });
  play.proc = proc;
  play.kind = 'engine';
  // (what the game says: re/private/play.log)
  fs.mkdirSync(PRIVATE, { recursive: true });
  const log = fs.createWriteStream(path.join(PRIVATE, 'play.log'));
  proc.stdout.pipe(log);
  proc.stderr.pipe(log);
  proc.on('exit', (code) => {
    if (play.proc !== proc) return;
    play.proc = null;
    send('play:exit', { code, kind: 'engine' });
  });
  return { ok: true };
});
// The original game (Play Game > Play original): zoo.exe with our hook
// loaded into it (zt1-original.exe and zt1-hook.dll, src/original/), so it
// runs windowed, drawn at the Play view's size and held over it like our
// engine is - the same Play slot, the same lines on stdin. It reads the
// Marine Mania disc from the drive its CDPath names: the disc image is
// mounted first if no drive has it. Where they are: re/private/original.json
// { exe, iso } if it's there, else the usual install and the Complete
// Collection image. Its hook's log: re/private/original-hook.log
function originalConfig() {
  let c = {};
  try {
    c = JSON.parse(fs.readFileSync(path.join(PRIVATE, 'original.json'), 'utf8'));
  } catch {}
  return {
    exe: c.exe || 'C:\\Program Files (x86)\\Microsoft Games\\Zoo Tycoon\\zoo.exe',
    // (the Marine Mania disc image the original asks for, if any: set in
    // re/private/original.json)
    iso: c.iso || '',
  };
}
// (the zoo.exe to work on: the one Play uses, so every view and the shared
// base agree on it - the ZT1_EXE override, then the configured original, then
// the usual install. This used to ignore originalConfig() and only ever look
// at the default path, so a player whose game is installed elsewhere could
// Play but not run code or contribute)
function zooExePath() {
  return process.env.ZT1_EXE || originalConfig().exe;
}
function psRun(script) {
  const r = cp.spawnSync('powershell.exe', ['-NoProfile', '-NonInteractive', '-Command', script], { encoding: 'utf8', windowsHide: true, timeout: 30000 });
  return { ok: r.status === 0, out: ((r.stdout || '') + (r.stderr || '')).trim() };
}
// The Play view in screen pixels (null when it isn't up)
function playRect() {
  if (!play.area || !win) return null;
  const c = win.getContentBounds();
  return screen.dipToScreenRect(win, {
    x: Math.round(c.x + play.area.x),
    y: Math.round(c.y + play.area.y),
    width: Math.round(play.area.w),
    height: Math.round(play.area.h),
  });
}
ipcMain.handle('play:original', () => {
  if (play.proc) return play.kind === 'original' ? { ok: true } : { error: 'Our engine is running in Play: stop it first.' };
  const c = originalConfig();
  if (!fs.existsSync(c.exe)) return { error: `The original isn't at ${c.exe} (re/private/original.json says where it is).` };
  const launcher = roots.originalExe;
  if (!fs.existsSync(launcher)) return { error: roots.looked('zt1-original.exe (built with the engine: build first)') };
  // (its disc: mounted unless it already is)
  if (c.iso && fs.existsSync(c.iso)) {
    const q = c.iso.replace(/'/g, "''");
    const r = psRun(`$i = Get-DiskImage -ImagePath '${q}'; if (-not $i.Attached) { Mount-DiskImage -ImagePath '${q}' | Out-Null }; (Get-DiskImage -ImagePath '${q}' | Get-Volume).DriveLetter`);
    if (!r.ok) return { error: 'Couldn’t mount its disc image: ' + r.out.slice(0, 200) };
  }
  const handle = win.getNativeWindowHandle();
  const hwnd = handle.length >= 8 ? handle.readBigUInt64LE(0) : BigInt(handle.readUInt32LE(0));
  const args = ['--zoo', c.exe, '--embed', hwnd.toString(), '--log', path.join(PRIVATE, 'original-hook.log')];
  // (drawn at the Play view's size, at least the 800x600 it was made for)
  const r = playRect();
  if (r) args.push('--size', `${Math.max(800, r.width)}x${Math.max(600, r.height)}`);
  const proc = cp.spawn(launcher, args, { cwd: path.dirname(launcher), stdio: ['pipe', 'pipe', 'pipe'], windowsHide: true });
  play.proc = proc;
  play.kind = 'original';
  fs.mkdirSync(PRIVATE, { recursive: true });
  const log = fs.createWriteStream(path.join(PRIVATE, 'original.log'));
  // (its hook's lines: the recorder's hits, answers - the rest to the log)
  let buf = '';
  proc.stdout.on('data', (d) => {
    buf += d.toString();
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl).replace(/\r$/, '');
      buf = buf.slice(nl + 1);
      if (line.startsWith('hit ')) recHit(line);
      else if (line.startsWith('wrote ')) recWrote(line);
      else {
        log.write(line + '\n');
        for (const w of hookWaits.splice(0)) {
          if (line.startsWith(w.prefix)) w.done(line);
          else hookWaits.push(w);
        }
      }
    }
  });
  proc.stderr.pipe(log);
  proc.on('exit', (code) => {
    rec.active = rec.learning = false;
    if (play.proc !== proc) return;
    play.proc = null;
    send('play:exit', { code, kind: 'original' });
  });
  proc.on('error', () => play.proc === proc && (play.proc = null));
  return { ok: true };
});
function playWrite(line) {
  if (play.proc && play.proc.stdin.writable) play.proc.stdin.write(line + '\n');
}
// The Play area (page pixels in the window's content) to screen pixels: the
// game's window is put there, and kept there as this one moves
function placeGame() {
  if (!play.proc || !win) return;
  if (!play.visible || !play.area || win.isMinimized()) return playWrite('hide');
  const r = playRect();
  playWrite(`rect ${r.x} ${r.y} ${r.width} ${r.height}`);
}
ipcMain.on('play:area', (_e, area) => {
  play.area = area;
  play.visible = !!area && area.w > 0;
  placeGame();
});
ipcMain.on('play:cmd', (_e, line) => playWrite(line));
// (what's running in Play: the page asks when it's loaded - a reload while
// a game runs keeps it)
ipcMain.handle('play:state', () => ({ running: !!play.proc, kind: play.proc ? play.kind : null }));
// A picture of the original game in Play (its hook's "shot": what it draws,
// as a .bmp) saved as a .png at file
// The original's own pause button, pressed (the Steps panel's Pause): it's at
// the bottom left, 186 across and 18 up from its window's bottom - the
// window's size read from a frame of it. The log stops with it (no re-arming
// while paused) and starts again with the game.
async function gameSize() {
  const bmp = path.join(PRIVATE, 'original-size.bmp');
  try {
    fs.unlinkSync(bmp);
  } catch {}
  playWrite('shot ' + bmp);
  for (let i = 0; i < 40 && !fs.existsSync(bmp); i++) await new Promise((r) => setTimeout(r, 100));
  await new Promise((r) => setTimeout(r, 150));
  try {
    const b = fs.readFileSync(bmp);
    return { w: b.readInt32LE(18), h: b.readInt32LE(22) };
  } catch {
    return null;
  }
}
ipcMain.handle('orig:pauseGame', async (_e, pause) => {
  if (!originalUp()) return { error: 'The original isn’t running.' };
  const size = await gameSize();
  if (!size) return { error: 'Couldn’t see the game’s window.' };
  if (pause && rec.watching) clearInterval(rec.watchTimer);
  const done = hookWait('done click', 5000);
  playWrite(`click 186 ${size.h - 18}`);
  await done;
  if (!pause && rec.watching) {
    clearInterval(rec.watchTimer);
    rec.watchTimer = setInterval(() => {
      if (!rec.watching || !originalUp()) return clearInterval(rec.watchTimer);
      playWrite('rearm');
    }, 1000);
  }
  rec.frozen = !!pause;
  return { ok: true, paused: !!pause };
});
// A function named from the steps: its name and what it does, how sure
// (sure, likely, guess; a guess stays provisional), by you
ipcMain.handle('orig:nameFn', (_e, address, name, summary, confidence, cls, by) => {
  const args = [at('kb.py'), 'func', address, name, summary || '', '--confidence', confidence || 'likely', '--by', by || me() || 'unknown'];
  if (confidence === 'guess') args.push('--provisional');
  if (cls) args.push('--class', cls);
  const r = cp.spawnSync(labPython(), args, { cwd: REPO || roots.state, encoding: 'utf8', windowsHide: true, timeout: 60000 });
  return r.status === 0 ? { ok: true } : { error: ((r.stdout || '') + (r.stderr || '')).trim() };
});
ipcMain.handle('play:shot', async (_e, file) => {
  if (!play.proc || play.kind !== 'original') return { error: 'The original isn’t running in Play.' };
  const bmp = path.join(PRIVATE, 'original-shot.bmp');
  try {
    fs.unlinkSync(bmp);
  } catch {}
  playWrite('shot ' + bmp);
  for (let i = 0; i < 40 && !fs.existsSync(bmp); i++) await new Promise((r) => setTimeout(r, 100));
  await new Promise((r) => setTimeout(r, 150));
  let b;
  try {
    b = fs.readFileSync(bmp);
  } catch {
    return { error: 'No picture: it hasn’t drawn a frame yet.' };
  }
  // (24-bit, bottom-up: to BGRA rows top-down)
  const off = b.readUInt32LE(10), w = b.readInt32LE(18), h = b.readInt32LE(22), row = (w * 3 + 3) & ~3;
  const px = Buffer.alloc(w * h * 4);
  for (let y = 0; y < h; y++)
    for (let x = 0; x < w; x++) {
      const s = off + (h - 1 - y) * row + x * 3, d = (y * w + x) * 4;
      px[d] = b[s];
      px[d + 1] = b[s + 1];
      px[d + 2] = b[s + 2];
      px[d + 3] = 255;
    }
  const out = file || path.join(PRIVATE, 'original-shot.png');
  fs.mkdirSync(path.dirname(out), { recursive: true });
  fs.writeFileSync(out, nativeImage.createFromBitmap(px, { width: w, height: h }).toPNG());
  return { file: out, width: w, height: h };
});
ipcMain.handle('play:stop', () => {
  const p = play.proc;
  if (p && p.stdin.writable) p.stdin.write('quit\n');
  // (the original, in a zoo, asks whether to quit rather than closing: what's
  // still running 4 s on is ended, the game with its launcher)
  if (p && p.pid)
    setTimeout(() => {
      if (p.exitCode === null && p.signalCode === null) cp.spawnSync('taskkill', ['/PID', String(p.pid), '/T', '/F'], { windowsHide: true });
    }, 4000);
  return true;
});

// --------------------------------------------------- the original's recorder
// Which of zoo.exe's functions run while something's done in the original
// (Play original): its hook puts a one-time breakpoint on every function
// Ghidra knows (src/original/Hook.cpp, "arm"), and reports each the first
// time it runs, with where it was called from. Learn idle first: what runs
// with nothing being done is "always running" (re/private/original-quiet.json)
// and left out after. A recording - "Placing a fence" - is then everything
// else that ran: each matched with what's known (the knowledge base, our
// engine's code citing it, Ghidra's name, and the game's own debug text in it,
// "GXVidMan::setupWindowed - ...", which names it), saved in
// re/private/recordings/, and put in the knowledge base's "seen" (kb.py
// seen-file) for ZT1_Apply to show in Ghidra.
const RECORDINGS = path.join(PRIVATE, 'recordings');
const rec = { active: false, learning: false, watching: false, watchTimer: null, label: '', hits: [], seen: new Set(), quiet: null, coverage: null, fns: null, debug: null };
const hookWaits = [];
function hookWait(prefix, ms = 15000) {
  return new Promise((resolve) => {
    const w = { prefix, done: resolve };
    hookWaits.push(w);
    setTimeout(() => {
      const i = hookWaits.indexOf(w);
      if (i >= 0) {
        hookWaits.splice(i, 1);
        resolve(null);
      }
    }, ms);
  });
}
function jsonFile(p, d) {
  try {
    return JSON.parse(fs.readFileSync(p, 'utf8'));
  } catch {
    return d;
  }
}
const hex6 = (a) => '0x' + a.toString(16).padStart(6, '0');
function recSets() {
  if (!rec.quiet) rec.quiet = new Set(jsonFile(path.join(PRIVATE, 'original-quiet.json'), []));
  if (!rec.coverage) rec.coverage = new Set(jsonFile(path.join(PRIVATE, 'original-coverage.json'), []));
}
function recKeep() {
  fs.writeFileSync(path.join(PRIVATE, 'original-quiet.json'), JSON.stringify([...rec.quiet].sort()));
  fs.writeFileSync(path.join(PRIVATE, 'original-coverage.json'), JSON.stringify([...rec.coverage].sort()));
}
function recHit(line) {
  const [, a, from, t] = line.split(' ');
  const address = hex6(parseInt(a, 16));
  recSets();
  rec.coverage.add(address);
  // (watching: every time it runs - the hook re-arms each second - the
  // last few thousand kept; recording: the first time only)
  if (rec.watching) {
    rec.hits.push({ address, from: parseInt(from, 16), t: +t });
    if (rec.hits.length > 4000) rec.hits.splice(0, rec.hits.length - 3000);
    return;
  }
  if (!(rec.active || rec.learning) || rec.seen.has(address)) return;
  rec.seen.add(address);
  rec.hits.push({ address, from: parseInt(from, 16), t: +t });
}
// Ghidra's functions (address, size, name), sorted - kept from the last time
// it was up if it isn't now
async function zooFunctions() {
  if (rec.fns) return rec.fns;
  let r = await ghidraGet('/functions', 120000);
  if (!r) r = { json: jsonFile(cacheFile('/functions'), null) };
  if (!r.json || !r.json.functions) return null;
  rec.fns = r.json.functions.map((f) => ({ a: parseInt(f.address, 16), size: f.size, name: f.name })).sort((x, y) => x.a - y.a);
  return rec.fns;
}
function fnContaining(a) {
  const fns = rec.fns || [];
  let lo = 0;
  let hi = fns.length - 1;
  let best = null;
  while (lo <= hi) {
    const m = (lo + hi) >> 1;
    if (fns[m].a <= a) {
      best = fns[m];
      lo = m + 1;
    } else hi = m - 1;
  }
  return best && a < best.a + Math.max(best.size, 1) ? best : null;
}
async function fnStrings() {
  if (rec.texts) return rec.texts;
  let r = await ghidraGet('/fnstrings', 180000);
  if (!r) r = { json: jsonFile(cacheFile('/fnstrings'), null) };
  rec.texts = (r.json && r.json.functions) || {};
  return rec.texts;
}
async function debugNames() {
  if (rec.debug) return rec.debug;
  let r = await ghidraGet('/debugnames', 180000);
  if (!r) r = { json: jsonFile(cacheFile('/debugnames'), null) };
  rec.debug = (r.json && r.json.functions) || {};
  return rec.debug;
}
function engineRefs() {
  const out = {};
  try {
    for (const line of readMaybe(REFS, '').split(/\r?\n/).slice(1)) {
      const m = line.match(/^(0x[0-9a-f]+),([^,]+),/i);
      if (m) (out[m[1].toLowerCase()] = out[m[1].toLowerCase()] || []).push(m[2]);
    }
  } catch {}
  return out;
}
// Every function (but those left out) armed in the hook
async function recArm(leaveOut) {
  const fns = await zooFunctions();
  if (!fns) return { error: 'No function list: Ghidra isn’t up yet (the pill at the top), and none was kept from before.' };
  const list = fns.filter((f) => !leaveOut.has(hex6(f.a))).map((f) => f.a.toString(16));
  const file = path.join(PRIVATE, 'original-arm.txt');
  fs.writeFileSync(file, list.join('\n') + '\n');
  rec.hits = [];
  rec.seen = new Set();
  const answer = hookWait('armed ');
  playWrite('arm ' + file);
  const line = await answer;
  return line ? { armed: +line.split(' ')[1] } : { error: 'The game didn’t answer (is it still starting?).' };
}
async function recDisarm() {
  const answer = hookWait('disarmed', 5000);
  playWrite('disarm');
  await answer;
  await new Promise((r) => setTimeout(r, 250)); // (its last hits)
}
function originalUp() {
  return !!(play.proc && play.kind === 'original');
}
ipcMain.handle('orig:recStatus', () => {
  recSets();
  return {
    up: originalUp(),
    active: rec.active,
    learning: rec.learning,
    label: rec.label,
    count: rec.hits.length,
    quiet: rec.quiet.size,
    coverage: rec.coverage.size,
    total: rec.fns ? rec.fns.length : null,
  };
});
// Idle: everything that runs for that long with nothing being done
ipcMain.handle('orig:learnIdle', async (_e, seconds = 10) => {
  await watchStop();
  if (!originalUp()) return { error: 'Start the original first (Play original).' };
  if (rec.active || rec.learning) return { error: 'Already recording.' };
  recSets();
  rec.learning = true;
  const r = await recArm(rec.quiet);
  if (r.error) {
    rec.learning = false;
    return r;
  }
  await new Promise((res) => setTimeout(res, Math.max(2, Math.min(120, +seconds || 10)) * 1000));
  await recDisarm();
  rec.learning = false;
  for (const h of rec.hits) rec.quiet.add(h.address);
  recKeep();
  return { added: rec.hits.length, quiet: rec.quiet.size };
});
// Watching (the Steps panel open, the original running, nothing being
// recorded): every function but the idle ones armed, and each that's run
// armed again every second - a live stream of what the game is doing, as the
// Scene shows our engine's. Recording or learning idle stops it first.
async function watchStop() {
  if (!rec.watching) return;
  clearInterval(rec.watchTimer);
  rec.watching = false;
  await recDisarm();
}
ipcMain.handle('orig:watch', async (_e, on) => {
  if (!on) {
    await watchStop();
    return { watching: false };
  }
  if (!originalUp()) return { watching: false, error: 'The original isn’t running.' };
  if (rec.active || rec.learning) return { watching: false, busy: true };
  if (rec.watching) return { watching: true };
  recSets();
  rec.watching = true;
  rec.label = '';
  const r = await recArm(rec.quiet);
  if (r.error) {
    rec.watching = false;
    return { watching: false, error: r.error };
  }
  rec.watchT0 = Date.now();
  rec.watchTimer = setInterval(() => {
    if (!rec.watching || !originalUp()) return clearInterval(rec.watchTimer);
    playWrite('rearm');
  }, 1000);
  return { watching: true, armed: r.armed };
});
ipcMain.handle('orig:recStart', async (_e, label) => {
  if (!originalUp()) return { error: 'Start the original first (Play original).' };
  if (rec.active || rec.learning) return { error: 'Already recording.' };
  await watchStop();
  recSets();
  rec.label = String(label || 'Something').trim();
  rec.active = true;
  const r = await recArm(rec.quiet);
  if (r.error) rec.active = false;
  return r;
});
// What ran (address, called from, ms), each matched with what's known now:
// its name (ours, Ghidra's), our engine's code citing it, the game's own
// debug text in it, the text it uses, where else it was seen running
// The knowledge base, read again only when it's changed (the live steps ask
// twice a second)
const kbMemo = { at: 0, kb: null };
function kbCached() {
  const f = KNOWLEDGE;
  let m = 0;
  try {
    m = fs.statSync(f).mtimeMs;
  } catch {}
  if (!kbMemo.kb || m !== kbMemo.at) kbMemo.kb = jsonFile(f, { functions: {}, seen: {} });
  kbMemo.at = m;
  return kbMemo.kb;
}
// What our engine's comments say each zoo.exe address is (engine_refs.csv's
// third column - re/scan_engine.py)
const notesMemo = { at: 0, notes: null };
function engineNotes() {
  const f = REFS;
  let m = 0;
  try {
    m = fs.statSync(f).mtimeMs;
  } catch {}
  if (notesMemo.notes && m === notesMemo.at) return notesMemo.notes;
  const out = {};
  try {
    for (const line of fs.readFileSync(f, 'utf8').split(/\r?\n/).slice(1)) {
      const mm = line.match(/^(0x[0-9a-f]+),([^,]+),"?(.*?)"?$/i);
      if (!mm) continue;
      const text = mm[3].replace(/""/g, '"').replace(/^-+\s*/, '').trim();
      (out[mm[1].toLowerCase()] = out[mm[1].toLowerCase()] || []).push(text.slice(0, 220));
    }
  } catch {}
  notesMemo.at = m;
  notesMemo.notes = out;
  return out;
}
// What an unknown function probably is, from what's around it - a guess, and
// said to be one, with its evidence. Strongest clue first:
//   1 a known caller that's specific (not code that runs for everything:
//     drawing, sound, the UI's plumbing - in more than 8 recordings)  high
//   2 the text it uses: a dialog's art (ui/sharedui/question: "are you
//     sure?"), a key, a file                                         medium
//   3 it ran for nothing else recorded: it's this action's own        medium
//   4 the few actions it ran in, and the words their labels share
//     (bulldozing, undo, selling: all give money back)                 medium
//   5 the specific known functions it calls                          medium
//   6 an unknown caller's guess, inherited a step weaker
//   7 the specific known ones that ran just before and after          low
// The class tables as an index: where each function sits (class, slot), and
// for a class and slot, the name the same slot has in a related class -
// one sharing a good part of its table (the same base: ZTFoodType and
// ZTPathType, not ZTFoodType and a UI panel). Built once a knowledge base.
const slotMemo = { kb: null, idx: null };
const TRIVIAL = /^(returns\d+|returnTrue|nop)$/;
// (getters and setters too, as clues: they run for everything)
const ACCESSOR = /^(returns\d+|returnTrue|nop|(get|set)(_[0-9a-f]+|[A-Z]\w*))$/;
function slotIndex(kb) {
  if (slotMemo.kb === kb) return slotMemo.idx;
  const classes = kb.classes || {};
  const at = new Map();
  for (const [cls, c] of Object.entries(classes))
    for (const [slot, fa] of Object.entries(c.slots || {})) {
      const k = hex6(parseInt(fa, 16));
      if (!at.has(k)) at.set(k, []);
      at.get(k).push({ cls, slot });
    }
  const related = new Map();
  const rel = (a, b) => {
    const key = a < b ? a + '|' + b : b + '|' + a;
    if (!related.has(key)) {
      const sa = classes[a].slots || {};
      const sb = classes[b].slots || {};
      const both = Object.keys(sa).filter((k) => sb[k] === sa[k]).length;
      related.set(key, both >= 0.3 * Math.min(Object.keys(sa).length, Object.keys(sb).length));
    }
    return related.get(key);
  };
  const named = (cls, slot) => {
    for (const [other, c] of Object.entries(classes)) {
      if (other === cls) continue;
      const fa = (c.slots || {})[slot];
      if (!fa) continue;
      const e = kb.functions[hex6(parseInt(fa, 16))];
      if (!e || !e.name || e.provisional || !rel(cls, other)) continue;
      // (not a trivial one many slots share - "returns1", a getter, nop: its
      // name says nothing about this slot)
      if (TRIVIAL.test(e.name) || new Set((at.get(hex6(parseInt(fa, 16))) || []).map((m) => m.slot)).size > 1) continue;
      return { cls: other, method: e.name.split('::').pop(), name: (e.class ? e.class + '::' : '') + e.name };
    }
    return null;
  };
  slotMemo.kb = kb;
  slotMemo.idx = { at, named };
  return slotMemo.idx;
}
function guessLines(lines, label) {
  const kb = kbCached();
  const seenIn = (a) => ((kb.seen || {})[a] || []).length;
  // (Ghidra's own labels - caseD_1, switchD_, LAB_, thunk_FUN_ - aren't names)
  const autoName = (n) => !n || /^(FUN_|caseD_|switchD_|LAB_|thunk_FUN_|SUB_)/.test(n) || /::caseD_/.test(n);
  const named = (l) => !!(l.known || !autoName(l.ghidra));
  const isKnown = (l) => named(l) || !!(l.engine && l.engine.length);
  // (a known one worth naming as a clue: not everywhere, not a name guessed
  // only from its source file)
  // (not a helper many functions call either: isKindOf next to something
  // says nothing about it)
  // (nor a method every type has - className, isKindOf: called through the
  // class table, Ghidra counts no callers, but they run for everything)
  const methodIn = new Map();
  for (const e of Object.values(kb.functions || {}))
    if (e.name && e.class) methodIn.set(e.name.split('::').pop(), (methodIn.get(e.name.split('::').pop()) || 0) + 1);
  const everyType = (l) => !!(l.known && (methodIn.get(l.known.name.split('::').pop()) || 0) >= 3);
  // (nor one in three or more class tables, or a getter / setter: they run
  // for everything)
  const inTables = (l) => (slotIndex(kb).at.get(l.address) || []).length >= 3 || !!(l.known && ACCESSOR.test(l.known.name.split('::').pop()));
  const specific = (l) => isKnown(l) && seenIn(l.address) <= 8 && !(l.callers >= 4) && !everyType(l) && !inTables(l) && !(l.known && /^[a-z0-9]+_[0-9a-f]{6}$/.test(l.known.name));
  const nameOf = (l) => (l.known ? l.known.name : !autoName(l.ghidra) ? l.ghidra : l.address);
  const core = (w) => w.split('(')[0].trim();
  const STOP = new Set('a an the of to in on and or one its it his her by with into from for at as'.split(' '));
  const words = (w) => new Set(core(w).toLowerCase().split(/[^a-z]+/).filter((x) => x.length > 2 && !STOP.has(x)).map((x) => x.replace(/(ing|ed|s)$/, '').replace(/([^aeiou])\1$/, '$1')));
  const callees = new Map();
  for (const l of lines) {
    if (!l.fromFunction || l.fromFunction.address === l.address) continue;
    if (!callees.has(l.fromFunction.address)) callees.set(l.fromFunction.address, []);
    callees.get(l.fromFunction.address).push(l);
  }
  const out = [];
  const step = { high: 'medium', medium: 'low', low: 'low' };
  const slots = slotIndex(kb);
  // (callers seen for each function in these lines: called from many
  // places, it's a shared helper, not part of any one of them)
  const callersOf = new Map();
  for (const l of lines) {
    if (!l.fromFunction || l.fromFunction.address === l.address) continue;
    if (!callersOf.has(l.address)) callersOf.set(l.address, new Map());
    callersOf.get(l.address).set(l.fromFunction.address, l.fromFunction.name);
  }
  // (how many actions are recorded: code running in half of them or more is
  // the game's general machinery - input, drawing, sound - not this action)
  const nRec = new Set(Object.values(kb.seen || {}).flatMap((rows) => rows.map((r) => r.when))).size || 1;
  lines.forEach((l, i) => {
    if (isKnown(l)) return out.push({ ...l, isKnown: true });
    const why = [];
    let guess = '';
    let conf = 'low';
    let name = '';
    const set = (g, c, n) => {
      if (!guess) [guess, conf, name] = [g, c, n];
    };
    const ad = l.address.slice(2);
    if (seenIn(l.address) >= Math.max(6, nRec / 2)) {
      why.push(`runs for nearly everything (${seenIn(l.address)} of ${nRec} recordings)`);
      return out.push({ ...l, isKnown: false, general: true, guess: 'General code: input, drawing, sound or the like', conf: 'low', why, suggestName: `general_${ad}` });
    }
    // 0 its place in the class tables, and the same slot named in a related class
    const mine = slots.at.get(l.address) || [];
    if (mine.length) {
      why.push(`slot ${mine.slice(0, 3).map((m) => `${m.slot} of ${m.cls}`).join(', ')}${mine.length > 3 ? ' (+' + (mine.length - 3) + ')' : ''}`);
      for (const m of mine) {
        const same = slots.named(m.cls, m.slot);
        if (same) {
          why.push(`the same slot is ${same.name} in ${same.cls} (a related class)`);
          set(`${m.cls}::${same.method}`, 'high', `${m.cls}::${same.method}`);
          break;
        }
      }
    }
    const many = callersOf.get(l.address);
    if (many && many.size >= 3) {
      why.push(`called from ${many.size} places: ${[...many.values()].slice(0, 4).join(', ')}`);
      set(`A helper used by ${[...many.values()].slice(0, 3).join(', ')}`, 'medium', `helper_${l.address.slice(2)}`);
    }
    // 1 its caller
    const ca = l.fromFunction && l.fromFunction.address !== l.address ? l.fromFunction.address : '';
    const callerKb = ca ? kb.functions[ca] : null;
    const callerName = callerKb ? callerKb.name : ca && l.fromFunction && !autoName(l.fromFunction.name) ? l.fromFunction.name : '';
    const callerLine = ca ? out.find((x) => x.address === ca) || lines.find((x) => x.address === ca) : null;
    if (callerName) {
      why.push(`called by ${callerName}${callerKb && callerKb.summary ? ' (' + callerKb.summary.slice(0, 80) + ')' : ''}`);
      // (part of it only when few things call it, program-wide; more and
      // it's a helper that caller uses too)
      if (l.callers >= 4) set(`A helper (${l.callers} functions call it, ${callerName} among them)`, 'medium', `helper_${ad}`);
      else if (seenIn(ca) <= 8) set(`Part of ${callerName}`, l.callers <= 1 ? 'high' : 'medium', `${callerName.replace(/\W/g, '')}_part_${ad}`);
    } else if (callerLine && !callerLine.isKnown) why.push(`called by ${ca} (unknown too)`);
    else if (ca) why.push(`called from ${ca}, not mapped`);
    // 2 its text
    const texts = l.texts || [];
    if (texts.length) {
      why.push(`uses the text ${texts.slice(0, 3).map((t) => '"' + t.slice(0, 48) + '"').join(', ')}`);
      const ui = texts.find((t) => /^ui\//i.test(t));
      if (ui) set(`Shows ${ui.split('/').pop()} (${ui})`, 'medium', `show_${ui.split('/').pop().replace(/\W/g, '')}_${ad}`);
      else set(`Uses "${texts[0].slice(0, 40)}"`, 'medium', `${texts[0].replace(/[^A-Za-z]/g, '').slice(0, 20) || 'text'}_${ad}`);
    }
    if (l.source) {
      why.push(`its text names the original's source file ${l.source}`);
      set(`In ${l.source}`, 'medium', `${l.source.split('/').pop().replace(/\.\w+$/, '')}_${ad}`);
    }
    // 3, 4 the actions it ran in
    const others = (l.seenBefore || []).filter((w) => w !== label);
    if (!others.length && label) {
      why.push('ran for nothing else recorded: this action\'s own');
      set(`Part of ${core(label).toLowerCase()}`, 'medium', `${core(label).toLowerCase().replace(/[^a-z]+/g, '_').replace(/^_|_$/g, '').slice(0, 24)}_${ad}`);
    } else if (others.length && others.length <= 4) {
      const all = [label, ...others].filter(Boolean);
      let common = null;
      for (const w of all) common = common ? new Set([...common].filter((x) => words(w).has(x))) : words(w);
      why.push(`also ran when: ${others.map(core).join('; ')}`);
      // (one recording: just that; several: what their labels share)
      const only = `Runs only for: ${all.map((w) => core(w).toLowerCase()).join('; ')}`;
      if (all.length > 1 && common && common.size) set(`${only} (all: ${[...common].join(', ')})`, 'medium', `${[...common][0]}_${ad}`);
      else set(only, 'medium', `${all.length === 1 ? (words(all[0]).values().next().value || 'shared') : 'shared'}_${ad}`);
    } else if (others.length > 8) why.push(`runs for many things (${others.length} recordings)`);
    // 5 what it calls
    const kids = (callees.get(l.address) || []).filter(specific);
    if (kids.length) {
      why.push(`calls ${kids.slice(0, 4).map(nameOf).join(', ')}`);
      set(`Uses ${kids.slice(0, 2).map(nameOf).join(' and ')}`, 'medium', `uses_${nameOf(kids[0]).replace(/\W/g, '')}_${ad}`);
    }
    // 6 an unknown caller's guess
    if (callerLine && !callerLine.isKnown && callerLine.guess) set(callerLine.guess, step[callerLine.conf], `${callerLine.suggestName.replace(/_[0-9a-f]{6}$/, '')}_part_${ad}`);
    // 7 the specific known ones either side
    let before = null;
    let after = null;
    for (let j = i - 1; j >= 0 && l.ms - lines[j].ms < 500; j--)
      if (specific(lines[j])) {
        before = lines[j];
        break;
      }
    for (let j = i + 1; j < lines.length && lines[j].ms - l.ms < 500; j++)
      if (specific(lines[j])) {
        after = lines[j];
        break;
      }
    if (before || after) {
      why.push(`ran ${before ? 'after ' + nameOf(before) : ''}${before && after ? ', ' : ''}${after ? 'before ' + nameOf(after) : ''}`);
      set(`Goes with ${nameOf(before || after)}`, 'low', `with_${nameOf(before || after).replace(/\W/g, '')}_${ad}`);
    }
    if (label) set(`Ran while ${core(label).toLowerCase()}`, 'low', `unknown_${ad}`);
    set('No clue yet: nothing known calls it, it calls nothing known, it uses no text', 'low', `unknown_${ad}`);
    out.push({ ...l, isKnown: false, guess, conf, why, suggestName: name || `unknown_${ad}` });
  });
  // (a second pass: one whose only guess is "ran while" takes its unknown
  // caller's better guess, a step weaker - callers can come after callees)
  for (const l of out) {
    if (l.isKnown || l.general || l.conf !== 'low' || !l.fromFunction) continue;
    const c = out.find((x) => x.address === l.fromFunction.address && !x.isKnown && !x.general && x.conf !== 'low');
    if (c) Object.assign(l, { guess: c.guess, conf: step[c.conf], suggestName: `${c.suggestName.replace(/_[0-9a-f]{6}$/, '')}_part_${l.address.slice(2)}` });
  }
  return out;
}
// How many functions call each (Ghidra, once - kept): a shared helper is
// told apart from one function's own part
async function fnCallers() {
  if (rec.callers) return rec.callers;
  let r = await ghidraGet('/fncallers', 180000);
  if (!r) r = { json: jsonFile(cacheFile('/fncallers'), null) };
  rec.callers = (r && r.json && r.json.functions) || {};
  return rec.callers;
}
async function annotate(hits, label) {
  await zooFunctions();
  const callerCount = await fnCallers();
  const debug = await debugNames();
  const texts = await fnStrings();
  const kb = kbCached();
  const refs = engineRefs();
  const notes = engineNotes();
  const t0 = hits.length ? hits[0].t : 0;
  return hits.map((h, i) => {
    const a = parseInt(h.address, 16);
    const key = hex6(a);
    const f = fnContaining(a);
    const caller = fnContaining(h.from);
    const known = kb.functions[key] || null;
    const names = debug[key] || [];
    const ghidraNamed = f && !/^FUN_/.test(f.name);
    const callerKey = caller ? hex6(caller.a) : '';
    return {
      order: i + 1,
      address: key,
      ms: h.t - t0,
      ghidra: f ? f.name : '',
      known: known ? { name: (known.class ? known.class + '::' : '') + known.name, summary: known.summary || '', provisional: !!known.provisional } : null,
      engine: refs[key] || (known && known.engine) || [],
      // (what our engine's comment there says it is: the Scene's words)
      engineSays: (notes[key] || [])[0] || '',
      callers: callerCount[key] || 0,
      debug: names,
      // (the original's source file it's from: the developers' paths were
      // left in its text - C:\aqua\src\snd\dx8sound.cpp)
      source: ((texts[key] || []).map((t) => t.match(/\\aqua\\src\\(.+\.(?:cpp|c|h))$/i)).find(Boolean) || [])[1]?.replace(/\\/g, '/') || '',
      // (the rest of the text it uses: what it's about, when nothing names it)
      texts: (texts[key] || []).filter((t) => !/\.(cpp|c|h)$/i.test(t) && !names.some((d) => t.startsWith(d))).slice(0, 4),
      // (what it'd be called: its own debug text's name, when nothing's named
      // it - only when there's one: several are other functions' text built
      // into it, GXCanvas::lockWrite in a drawing loop)
      suggest: !known && !ghidraNamed && names.length === 1 ? names[0] : '',
      from: hex6(h.from),
      fromFunction: caller ? { address: callerKey, name: (kb.functions[callerKey] || {}).name || caller.name } : null,
      seenBefore: ((kb.seen || {})[key] || []).map((x) => x.when).filter((w) => w !== label),
    };
  });
}
// Stopped: what ran, matched - saved
ipcMain.handle('orig:recStop', async () => {
  if (!rec.active) return { error: 'Not recording.' };
  await recDisarm();
  rec.active = false;
  recKeep();
  const functions = await annotate(rec.hits, rec.label);
  const out = { label: rec.label, on: new Date().toISOString(), functions };
  fs.mkdirSync(RECORDINGS, { recursive: true });
  const slug = rec.label.toLowerCase().replace(/[^a-z0-9]+/g, '-').replace(/^-|-$/g, '').slice(0, 40) || 'recording';
  const file = path.join(RECORDINGS, `${out.on.slice(0, 19).replace(/[:T]/g, '-')}-${slug}.json`);
  fs.writeFileSync(file, JSON.stringify(out, null, 1));
  return { ...out, file };
});
// The original's steps, live (Play original's Steps panel): what's run since
// the recording started, each matched, unknowns guessed - from line "from"
ipcMain.handle('orig:steps', async (_e, from) => {
  if (rec.watching) {
    // (the last 250 calls, the newest last)
    const lines = guessLines(await annotate(rec.hits.slice(-250), ''), '');
    return { active: true, watching: true, label: 'live', total: rec.hits.length, lines };
  }
  if (!rec.active) return { active: false, total: 0, lines: [] };
  const lines = guessLines(await annotate(rec.hits, rec.label), rec.label);
  return { active: true, label: rec.label, total: lines.length, lines: lines.slice(from || 0) };
});
// A saved recording's steps, the same way
ipcMain.handle('orig:stepsOf', async (_e, file) => {
  const r = jsonFile(file, {});
  const hits = (r.functions || []).map((f) => ({ address: f.address, from: parseInt(f.from, 16), t: f.ms }));
  return { label: r.label, file, lines: guessLines(await annotate(hits, r.label), r.label) };
});
// A guess accepted: a provisional name, confidence "guess", by you, with why
ipcMain.handle('orig:acceptGuess', (_e, address, name, summary, by) => {
  const r = cp.spawnSync(labPython(), [at('kb.py'), 'func', address, name, summary, '--provisional', '--confidence', 'guess', '--by', by || me() || 'unknown'], {
    cwd: REPO || roots.state,
    encoding: 'utf8',
    windowsHide: true,
    timeout: 60000,
  });
  return r.status === 0 ? { ok: true } : { error: ((r.stdout || '') + (r.stderr || '')).trim() };
});
ipcMain.handle('orig:recList', () => {
  try {
    return fs
      .readdirSync(RECORDINGS)
      .filter((f) => f.endsWith('.json'))
      .sort()
      .reverse()
      .map((f) => {
        const r = jsonFile(path.join(RECORDINGS, f), {});
        return { file: path.join(RECORDINGS, f), label: r.label, on: r.on, count: (r.functions || []).length };
      });
  } catch {
    return [];
  }
});
// (a recording that went wrong - the game refused, a month ended in it - set
// aside in recordings/discarded/, not saved: kept to look at, never in the base)
ipcMain.handle('orig:recDiscard', (_e, file) => {
  if (!file || path.dirname(path.resolve(file)) !== path.resolve(RECORDINGS)) return { error: 'Not a recording: ' + file };
  const to = path.join(RECORDINGS, 'discarded');
  fs.mkdirSync(to, { recursive: true });
  fs.renameSync(file, path.join(to, path.basename(file)));
  return { ok: true, moved: path.join(to, path.basename(file)) };
});
// (one from before: matched again with what's known now)
ipcMain.handle('orig:recLoad', async (_e, file) => {
  const r = jsonFile(file, {});
  const hits = (r.functions || []).map((f) => ({ address: f.address, from: parseInt(f.from, 16), t: f.ms }));
  return { ...r, functions: await annotate(hits, r.label), file };
});
// Into the knowledge base: every function in it seen running then (kb.py
// seen-file), and the names picked (from their debug text)
ipcMain.handle('orig:recSave', (_e, file, names, by) => {
  const kb = at('kb.py');
  const who = by || 'Lab recorder';
  const said = [];
  const run = (args) => {
    const r = cp.spawnSync(labPython(), [kb, ...args, '--by', who], { cwd: REPO || roots.state, encoding: 'utf8', windowsHide: true, timeout: 60000 });
    said.push(((r.stdout || '') + (r.stderr || '')).trim());
    return r.status === 0;
  };
  if (!run(['seen-file', file])) return { error: said.join('\n') };
  for (const n of names || []) {
    const parts = n.name.split('::');
    const method = parts.pop();
    const args = ['func', n.address, method, `From its own debug text ("${n.name}"); seen running in the original`, '--confidence', 'likely', '--provisional'];
    if (parts.length) args.push('--class', parts.join('::'));
    run(args);
  }
  return { ok: true, said: said.filter(Boolean).join('\n') };
});

// --------------------------------------------------- the original's memory
// Where the original keeps a value (its hook's scan/rescan/peek/watch,
// src/original/Hook.cpp): a line to it and its answer. Each place found is
// told apart - zoo.exe's own data (a global: its address is the same every
// run) or the heap (inside some object) - and each instruction a watch caught
// writing it is put in its function (Ghidra's)
const watchLog = [];
ipcMain.handle('orig:ask', async (_e, line, prefix) => {
  if (!originalUp()) return { error: 'Start the original first (Play original).' };
  const answer = hookWait(prefix, 60000);
  playWrite(line);
  const got = await answer;
  return got ? { line: got } : { error: 'The game didn’t answer.' };
});
function hookWhere(a) {
  // (zoo.exe's image: 0x400000 up to its end, from Ghidra's last function)
  const fns = rec.fns || [];
  const last = fns.length ? fns[fns.length - 1].a : 0x630000;
  if (a >= 0x400000 && a < 0x700000) return a > last ? 'zoo.exe data (a global)' : 'zoo.exe code';
  return 'heap';
}
ipcMain.handle('orig:find', async (_e, line) => {
  if (!originalUp()) return { error: 'Start the original first (Play original).' };
  await zooFunctions();
  const answer = hookWait('found ', 120000);
  playWrite(line);
  const got = await answer;
  if (!got) return { error: 'The game didn’t answer.' };
  const [, n, ...addrs] = got.split(' ');
  return { count: +n, places: addrs.map((a) => ({ address: hex6(parseInt(a, 16)), where: hookWhere(parseInt(a, 16)) })) };
});
function recWrote(line) {
  const f = line.split(' ');
  const get = (k) => parseInt(f[f.indexOf(k) + 1], 16);
  const after = get('after');
  const fn = fnContaining(after);
  const caller = fnContaining(get('from'));
  const kb = jsonFile(KNOWLEDGE, { functions: {} });
  const nameOf = (x) => (x ? (kb.functions[hex6(x.a)] || {}).name || x.name : '');
  watchLog.push({
    address: hex6(get('wrote')),
    after: hex6(after),
    value: get('now'),
    regs: Object.fromEntries(['eax', 'ebx', 'ecx', 'edx', 'esi', 'edi', 'ebp', 'esp'].map((r) => [r, hex6(get(r))])),
    function: fn ? { address: hex6(fn.a), name: nameOf(fn) } : null,
    from: caller ? { address: hex6(caller.a), name: nameOf(caller) } : null,
  });
}
ipcMain.handle('orig:writes', (_e, clear) => {
  const out = watchLog.slice();
  if (clear) watchLog.length = 0;
  return out;
});

// ---------------------------------------------------------------- Ghidra
function cacheFile(p) {
  return path.join(GHIDRA_CACHE, p.replace(/[^A-Za-z0-9]/g, '_') + '.json');
}

function ghidraGet(p, timeout = 60000) {
  return new Promise((resolve) => {
    const req = http.get({ host: '127.0.0.1', port: GHIDRA_PORT, path: p, timeout }, (res) => {
      let body = '';
      res.setEncoding('utf8');
      res.on('data', (c) => (body += c));
      res.on('end', () => {
        try {
          const json = JSON.parse(body);
          // (kept for when Ghidra's shut - not a long ask's, whose file name
          // would be too long: the answer either way)
          if (res.statusCode === 200 && p.length < 180)
            try {
              fs.mkdirSync(GHIDRA_CACHE, { recursive: true });
              fs.writeFileSync(cacheFile(p), body);
            } catch {}
          resolve({ json, cached: false });
        } catch {
          resolve(null);
        }
      });
    });
    req.on('timeout', () => req.destroy());
    req.on('error', () => resolve(null));
  });
}

ipcMain.handle('ghidra:get', async (_e, p, fresh) => {
  const live = await ghidraGet(p);
  if (live) return live;
  // (Ghidra shut: what was seen before)
  try {
    return { json: JSON.parse(fs.readFileSync(cacheFile(p), 'utf8')), cached: true };
  } catch {
    return { error: 'not connected' };
  }
});
ipcMain.handle('ghidra:status', async () => {
  const r = await ghidraGet('/status', 1500);
  if (r && !r.cached) return r.json;
  // (building its own project is the long job before Ghidra can start at all:
  //  the pill says so rather than looking like nothing is happening)
  if (ghidraOwn.building) return { building: true, since: ghidraOwn.started, said: ghidraOwn.said };
  if (ghidraOwn.proc) return { starting: true, since: ghidraOwn.started, said: ghidraOwn.said };
  return ghidraOwn.said ? { said: ghidraOwn.said } : null;
});

// The bridge started by the Lab itself: when nothing answers on its port at
// start (no Ghidra of yours with ZT1_LabBridge on), a headless Ghidra opens
// the project read only and runs it (re/ghidra_scripts/
// ZT1_LabBridgeHeadless.py); closed with the Lab. Where Ghidra and the
// project are: re/private/ghidra.json (below). The Lab can work from a copy
// of your own project, so your own Ghidra keeps it unlocked: "projects"
// lists the copy first, "own" is yours
const ghidraOwn = { proc: null, started: 0, building: false, said: '', project: '' };
// What the Lab made for itself, when nothing else points it at a project:
// a Ghidra project of the player's own zoo.exe, in the Lab's data folder.
// (The Lab carries Ghidra and the JDK it runs on, but it does not carry a
//  project - a project is built out of zoo.exe, which is the player's file
//  and is never shipped. Without one there is nothing for the headless Ghidra
//  to open, so a first-time machine was told to start the bridge by hand in a
//  Ghidra it had never opened, while the Ghidra that could do it sat in its
//  own install. On the first run with no project, the Lab imports zoo.exe and
//  analyses it - a few minutes, once - and starts its own bridge on that)
const OWN_PROJECT = PRIVATE && path.join(PRIVATE, 'ghidra-project');
// Ghidra's own log, kept: the import's progress is the only way to tell a long
// build from a stuck one, and it is what to read when it fails
const GHIDRA_BRIDGE_LOG = PRIVATE && path.join(PRIVATE, 'ghidra-bridge.log');
// (the zoo.exe this project is of, so a player whose game moves - or a second
//  build of it - is noticed and built again rather than quietly read wrong)
const OWN_PROJECT_MARK = PRIVATE && path.join(PRIVATE, 'ghidra-project.json');
function sameZoo(paths) {
  try {
    if (!fs.existsSync(OWN_PROJECT_MARK)) return false;
    const m = JSON.parse(fs.readFileSync(OWN_PROJECT_MARK, 'utf8'));
    return !!m && !!m.zoo && (m.zoos || [m.zoo]).some((z) => paths.includes(z)) && !!m.built && !!m.project;
  } catch {
    return false;
  }
}
function ourProjectDir() {
  try {
    return JSON.parse(fs.readFileSync(OWN_PROJECT_MARK, 'utf8')).project || '';
  } catch {
    return '';
  }
}
function ghidraConfig() {
  let c = {};
  try {
    c = JSON.parse(fs.readFileSync(path.join(PRIVATE, 'ghidra.json'), 'utf8'));
  } catch {}
  // (re/private/ghidra.json: home, the Ghidra install - else
  // GHIDRA_INSTALL_DIR; project, or projects to try in turn - the first with
  // <name>.gpr in it; own, your own project when the Lab works from a copy of
  // it; name; settings, Ghidra's settings folder for headless runs)
  const name = c.name || 'zt1';
  // (the Ghidra to run: what the player named, then GHIDRA_INSTALL_DIR, then
  //  the one the Lab itself carries - tree/ghidra. A Lab on its own has
  //  Ghidra without the player installing anything)
  const home = c.home || process.env.GHIDRA_INSTALL_DIR || roots.ghidraHome || '';
  // (no project of yours, and none named: the Lab's own import of the player's
  //  zoo.exe, when it has made one that is still of that same file)
  const zoo = zooExePath();
  // (both the project of ours and a build that is finished-but-not-started-yet)
  const ourMark = OWN_PROJECT && sameZoo([zoo]) ? ourProjectDir() : '';
  const ourProject = ourMark && fs.existsSync(path.join(ourMark, name + '.gpr')) ? ourMark : (ghidraOwn.project || '');
  const project = c.project || (c.projects || []).find((p) => fs.existsSync(path.join(p, name + '.gpr'))) || ourProject;
  // (Java, when the Lab carries one beside its Ghidra: Ghidra is a Java
  //  program, so a machine with no JDK cannot start it at all)
  const java = roots.javaHome || '';
  return { home, project, own: c.own || project, name, settings: c.settings || (project ? path.join(project, 'headless-settings') : ''), java };
}
async function startOwnGhidra() {
  if (ghidraOwn.proc || ghidraOwn.building || (await ghidraGet('/status', 1500))) return;
  const c = ghidraConfig();
  const bat = path.join(c.home, 'support', 'analyzeHeadless.bat');
  const scripts = at('ghidra_scripts');
  if (!scripts || !fs.existsSync(bat)) return; // (no Ghidra here: the Lab works from its saved copies)
  // (no project to open, and Ghidra is here: make one out of the player's own
  //  zoo.exe, then start on it. This is the first run on a machine that has
  //  only the Lab - there is no project because there was nothing to make one
  //  out of until now)
  if (!c.project) {
    if (!(await buildOwnProject(c, bat, scripts))) return;
    // buildOwnProject records the result on ghidraOwn; this config object was
    // created before the import, so pass the new project path to the bridge.
    c.project = ghidraOwn.project;
  }
  fs.mkdirSync(PRIVATE, { recursive: true });
  const log = fs.openSync(path.join(PRIVATE, 'ghidra-bridge.log'), 'w');
  const proc = cp.spawn(
    'cmd.exe',
    ['/c', bat, c.project, c.name, '-process', 'zoo.exe', '-noanalysis', '-readOnly', '-scriptPath', scripts, '-postScript', 'ZT1_LabBridgeHeadless.py'],
    {
      // (the JDK the Lab carries, when there is one: Ghidra's own launcher reads
      //  JAVA_HOME, so this is what lets a first-time machine run Ghidra at all)
      env: {
        ...process.env,
        ...(c.java ? { JAVA_HOME: c.java } : {}),
        JAVA_TOOL_OPTIONS: `-Dapplication.settingsdir=${c.settings}`,
      },
      stdio: ['ignore', log, log],
      windowsHide: true,
    },
  );
  ghidraOwn.proc = proc;
  ghidraOwn.started = Date.now();
  ghidraOwn.said = '';
  ghidraOwn.project = c.project;
  proc.on('exit', () => {
    if (ghidraOwn.proc === proc) ghidraOwn.proc = null;
  });
}
// Build the Lab its own Ghidra project out of the player's zoo.exe: import the
// exe and analyse it (analyzeHeadless, which is the headless Ghidra that came
// with the Lab), then start on the result. Run once per machine - the project
// is kept, so later starts open it. A few minutes for a 2.4 MB exe; the import
// is synchronous because what it produces is needed before Ghidra can start,
// and the truth is on disk either way - the project is not named as built
// until its .gpr is actually there.
//
// (zoo.exe is imported from a copy, not from where the game is installed.
//  Ghidra's analyzeHeadless.bat passes its arguments through cmd, which reads
//  parentheses as syntax: 'C:\Program Files (x86)\Microsoft Games\Zoo Tycoon\
//  zoo.exe' - which is where the game installs by default - fails with
//  '\zoo.exe was unexpected at this time' before Java even starts. Measured:
//  a path with spaces is fine, a path with () is not. The copy is the player's
//  own file, kept in the Lab's own folder and never shipped, and the bytes -
//  so every address in it - are identical to the ones Ghidra would have read)
const PAREN = /[()]/;
async function buildOwnProject(c, bat, scripts) {
  if (!OWN_PROJECT) return false;
  const zoo = zooExePath();
  if (!zoo || !fs.existsSync(zoo)) {
    ghidraOwn.said = `No zoo.exe to build a Ghidra project from (looked at ${zoo || 'nothing'}). Set where the game is in the Lab's settings, or point Ghidra at your own project in re/private/ghidra.json.`;
    return false;
  }
  // (both the project and the file imported into it have to be somewhere cmd
  //  will not choke on: the Lab's data folder, or a plain temporary one when
  //  even that has parentheses in it - a checkout under 'My Stuff (old)', say)
  const dir = PAREN.test(OWN_PROJECT) ? path.join(os.tmpdir(), 'zt1-lab-ghidra-project') : OWN_PROJECT;
  ghidraOwn.building = true;
  ghidraOwn.started = Date.now();
  ghidraOwn.said = 'Building a Ghidra project from zoo.exe (once: a few minutes)…';
  let log;
  try {
    fs.mkdirSync(dir, { recursive: true });
    fs.mkdirSync(PRIVATE, { recursive: true });
    log = fs.openSync(GHIDRA_BRIDGE_LOG, 'w');
    // (the project is made fresh: a half-built one from an earlier try would
    //  otherwise be opened as if it were complete)
    for (const f of [c.name + '.gpr', c.name + '.rep', c.name + '.lock', c.name + '.lock~']) {
      fs.rmSync(path.join(dir, f), { recursive: true, force: true });
    }
    // (named zoo.exe, so the bridge's -process zoo.exe still finds it)
    const copied = path.join(dir, 'zoo.exe');
    fs.copyFileSync(zoo, copied);
    // (analyzed, so the project has zoo.exe's functions in it: 'analyzeHeadless'
    //  runs the analysers by default, and only skips them with -noanalysis.
    //  No postScript: the Lab's own scripts are for naming, which ZT1_Apply and
    //  the bridge do later, against the knowledge base)
    const proc = cp.spawn('cmd.exe', ['/c', bat, dir, c.name, '-import', copied, '-overwrite'],
      { env: { ...process.env, ...(c.java ? { JAVA_HOME: c.java } : {}) }, stdio: ['ignore', log, log], windowsHide: true });
    ghidraOwn.buildProc = proc;
    const result = await new Promise((resolve, reject) => {
      proc.once('error', reject);
      proc.once('close', (code, signal) => resolve({ code, signal }));
    });
    if (ghidraOwn.buildProc === proc) ghidraOwn.buildProc = null;
    fs.closeSync(log);
    log = null;
    if (result.code !== 0) {
      ghidraOwn.said = `Ghidra project import failed (exit ${result.code === null ? result.signal : result.code}). What it said is in ${GHIDRA_BRIDGE_LOG}.`;
      ghidraOwn.building = false;
      return false;
    }
  } catch (e) {
    if (log !== undefined) {
      try { fs.closeSync(log); } catch {}
    }
    ghidraOwn.buildProc = null;
    ghidraOwn.said = 'Couldn’t build the Ghidra project: ' + e.message;
    ghidraOwn.building = false;
    return false;
  }
  ghidraOwn.building = false;
  // (named as built only when Ghidra really made it: a failed import leaves a
  //  folder behind, and it must be tried again rather than opened empty)
  if (!fs.existsSync(path.join(dir, c.name + '.gpr'))) {
    ghidraOwn.said = `Ghidra didn’t finish the project. What it said is in ${GHIDRA_BRIDGE_LOG}.`;
    return false;
  }
  ghidraOwn.project = dir;
  try {
    fs.writeFileSync(OWN_PROJECT_MARK, JSON.stringify({ zoo, zoos: [zoo], built: new Date().toISOString(), project: dir }, null, 1) + '\n');
  } catch {}
  ghidraOwn.said = 'The Lab made its own Ghidra project from zoo.exe. Starting on it…';
  return true;
}
function stopOwnGhidra() {
  const build = ghidraOwn.buildProc;
  ghidraOwn.buildProc = null;
  if (build && build.pid) cp.spawnSync('taskkill', ['/PID', String(build.pid), '/T', '/F'], { windowsHide: true });
  const p = ghidraOwn.proc;  ghidraOwn.proc = null;
  // (the .bat's Java with it: the whole tree)
  if (p && p.pid) cp.spawnSync('taskkill', ['/PID', String(p.pid), '/T', '/F'], { windowsHide: true });
}
app.on('will-quit', stopOwnGhidra);
// The Atlas (Explore): everything known joined into one searchable map -
// re/lab/atlas.py with Ghidra's answers (every function, the text each uses,
// the names their debug text gives) saved beside it; Ghidra's last answers
// are used if it isn't up
const ATLAS = path.join(PRIVATE, 'atlas.json');
// The game's UI elements by number (re/lab/uiids.py, from the layouts): what
// uiFind(0x11cb) is - "infocom:ItemCost_Cost". Made the first time it's asked
const UI_IDS = path.join(PRIVATE, 'ui_ids.json');
ipcMain.handle('ui:ids', (_e, nums) => {
  if (!fs.existsSync(UI_IDS)) {
    const r = cp.spawnSync(labPython(), [at('lab', 'uiids.py')], { cwd: REPO || roots.state, encoding: 'utf8', windowsHide: true, timeout: 120000 });
    if (r.status !== 0) return { error: ((r.stdout || '') + (r.stderr || '')).trim() || 'couldn’t read the layouts' };
  }
  const ids = JSON.parse(fs.readFileSync(UI_IDS, 'utf8'));
  const out = {};
  for (const n of nums || []) out[n] = ids[String(n)] || null;
  return { elements: out, total: Object.keys(ids).length };
});
// How close our engine is to zoo.exe (re/lab/fidelity.py): our functions and
// where each came from, the original's named gameplay and which we cite.
// Run fresh each time - it reads src/ and the knowledge base, under a second
const FIDELITY = path.join(PRIVATE, 'fidelity.json');
ipcMain.handle('fidelity:load', () => {
  fs.mkdirSync(PRIVATE, { recursive: true });
  const r = cp.spawnSync(labPython(), [at('lab', 'fidelity.py'), '--quiet', '--out', FIDELITY], { cwd: REPO || roots.state, encoding: 'utf8', windowsHide: true, timeout: 60000 });
  if (r.status !== 0) return { error: ((r.stderr || '') + (r.stdout || '')).trim().slice(-800) || 'fidelity.py failed' };
  return jsonFile(FIDELITY, null);
});
// Formula checks (re/lab/formulas.json, shared): zoo.exe's function against
// ours on the same inputs; the last results kept in re/private
const FORMULAS = at('lab', 'formulas.json');
const FORMULA_RESULTS = path.join(PRIVATE, 'formula-results.json');
ipcMain.handle('formulas:load', () => ({ checks: (jsonFile(FORMULAS, { checks: [] }).checks || []), results: jsonFile(FORMULA_RESULTS, null) }));
ipcMain.handle('formulas:save', (_e, results) => {
  fs.mkdirSync(PRIVATE, { recursive: true });
  fs.writeFileSync(FORMULA_RESULTS, JSON.stringify(results, null, 1) + '\n');
  return { ok: true };
});
const VERDICTS = at('lab', 'verdicts.json');
ipcMain.handle('atlas:build', async () => {
  fs.mkdirSync(PRIVATE, { recursive: true });
  const args = [at('lab', 'atlas.py'), '--out', ATLAS];
  for (const [p, flag] of [['/functions', '--functions'], ['/fnstrings', '--fnstrings'], ['/debugnames', '--debugnames']]) {
    let r = await ghidraGet(p, 180000);
    const file = path.join(PRIVATE, 'atlas' + p.replace('/', '-') + '.json');
    if (r && r.json) fs.writeFileSync(file, JSON.stringify(r.json));
    if (fs.existsSync(file)) args.push(flag, file);
  }
  const r = cp.spawnSync(labPython(), args, { cwd: REPO || roots.state, encoding: 'utf8', windowsHide: true, timeout: 300000 });
  if (r.status !== 0) return { error: ((r.stderr || '') + (r.stdout || '')).trim().slice(-800) || 'atlas.py failed' };
  try {
    return { ok: true, counts: JSON.parse(r.stdout.trim().split('\n').pop()) };
  } catch {
    return { ok: true };
  }
});
ipcMain.handle('atlas:load', () => {
  const a = jsonFile(ATLAS, null);
  if (!a) return null;
  // (older than what it's built from - the knowledge base, a recording, the
  // checked verdicts: it says so, to be built again)
  const built = fs.statSync(ATLAS).mtimeMs;
  const newer = [KNOWLEDGE, VERDICTS, ...(fs.existsSync(RECORDINGS) ? fs.readdirSync(RECORDINGS).filter((f) => f.endsWith('.json')).map((f) => path.join(RECORDINGS, f)) : [])].some((f) => {
    try {
      return fs.statSync(f).mtimeMs > built + 2000;
    } catch {
      return false;
    }
  });
  return { ...a, stale: newer };
});
// A checked verdict on one action (Explore): re/lab/verdicts.json, shared -
// status null clears it. The verdicts are shared with the checkout when
// there is one (re/lab/verdicts.json): a released copy writes the knowledge
// base it was shipped with, which the installer keeps in the app's data
// folder and an upgrade replaces - so those are read-only there.
ipcMain.handle('atlas:verdict', (_e, label, status, at, note, by) => {
  if (!REPO && VERDICTS && shipped(VERDICTS)) return { error: 'A verdict is kept in the checkout (re/lab/verdicts.json): point the Lab at one to record it.' };
  const v = jsonFile(VERDICTS, { actions: {} });
  v.actions = v.actions || {};
  if (!status) delete v.actions[label];
  else v.actions[label] = { status, at: at || '', note: note || '', by: by || me() || 'unknown', on: new Date().toISOString().slice(0, 10) };
  fs.writeFileSync(VERDICTS, JSON.stringify(v, null, 2) + '\n');
  return { ok: true, verdict: v.actions[label] || null };
});

// The Lab's copy of the project made again from your own ("own" in
// re/private/ghidra.json), after ZT1_Apply or your own naming there: its bridge
// stopped, the copy replaced, the bridge started again. Not while your
// project's open in Ghidra (it's locked then)
ipcMain.handle('ghidra:refresh', async () => {
  const c = ghidraConfig();
  const own = c.own;
  if (!c.project || !own || path.resolve(c.project) === path.resolve(own)) return { error: 'The Lab reads your own project directly: nothing to refresh.' };
  if (fs.existsSync(path.join(own, 'zt1.lock'))) return { error: 'Your project is open in Ghidra: close it (or save and close) first.' };
  stopOwnGhidra();
  await new Promise((r) => setTimeout(r, 2500));
  try {
    for (const f of ['zt1.lock', 'zt1.lock~']) fs.rmSync(path.join(c.project, f), { force: true });
    const old = path.join(c.project, 'zt1.rep.old');
    fs.rmSync(old, { recursive: true, force: true });
    fs.renameSync(path.join(c.project, 'zt1.rep'), old);
    fs.cpSync(path.join(own, 'zt1.rep'), path.join(c.project, 'zt1.rep'), { recursive: true });
    fs.copyFileSync(path.join(own, 'zt1.gpr'), path.join(c.project, 'zt1.gpr'));
    fs.rmSync(old, { recursive: true, force: true });
  } catch (e) {
    startOwnGhidra();
    return { error: 'Couldn’t copy it: ' + e.message };
  }
  // (what Ghidra said before is out of date now)
  fs.rmSync(GHIDRA_CACHE, { recursive: true, force: true });
  startOwnGhidra();
  return { ok: true, said: 'The Lab’s copy is your project again; Ghidra’s starting on it.' };
});

// ------------------------------------------------- the knowledge base
function me() {
  try {
    return fs.readFileSync(path.join(PRIVATE, 'me.txt'), 'utf8').trim();
  } catch {
    return '';
  }
}

ipcMain.handle('kb:load', () => {
  // (the knowledge base the Lab reads: the checkout's, or the released copy
  // it was installed with. With neither, the views say what's missing)
  if (!KNOWLEDGE) return { error: roots.looked('the knowledge base (re/knowledge.json)'), kb: { functions: {} }, refs: {}, me: '' };
  const kb = JSON.parse(readMaybe(KNOWLEDGE, '{"functions":{}}'));
  // engine_refs.csv: address,engine,"comment"
  const refs = {};
  try {
    const lines = readMaybe(REFS, '').split(/\r?\n/).slice(1);
    for (const l of lines) {
      const m = l.match(/^(0x[0-9a-f]+),([^,]+),(.*)$/i);
      if (!m) continue;
      let c = m[3];
      if (c.startsWith('"') && c.endsWith('"')) c = c.slice(1, -1).replace(/""/g, '"');
      (refs[m[1].toLowerCase()] ||= []).push({ where: m[2], comment: c });
    }
  } catch {}
  return { kb, refs, me: me() };
});
ipcMain.handle('kb:run', (_e, args) => {
  // re/kb.py, as you (re/private/me.txt)
  return new Promise((resolve) => {
    const kbpy = at('kb.py');
    if (!kbpy) return resolve({ ok: false, out: roots.looked('kb.py') });
    // (a run of ours saves the knowledge base where the tools live: the
    // checkout's re/, or the app's own copy. Only a checkout's is writable
    // for long - one of the app's own goes with the next upgrade)
    const by = me();
    const full = [kbpy, ...args];
    // (a save that says who keeps it)
    if (by && !args.includes('--by')) full.push('--by', by);
    cp.execFile(labPython(), full, { cwd: REPO || roots.state }, (err, stdout, stderr) =>
      resolve({ ok: !err, out: (stdout || '') + (stderr || '') }),
    );
  });
});
// ------------------------------------------- zoo.exe's own code, run
// re/lab/zoorun.py (Unicorn): a function of zoo.exe run on objects the page
// lays out (the selected animal as ZTAnimal, ...); it says back all it did.
// Started the first time it's asked for, kept for the next
const zr = { proc: null, buf: '', waiting: new Map(), id: 0, ready: null };
function zooRunner() {
  if (zr.proc) return zr.ready;
  zr.proc = cp.spawn(labPython(), ['-X', 'utf8', '-u', at('lab', 'zoorun.py'), zooExePath()], { cwd: REPO || roots.state });
  zr.buf = '';
  zr.ready = new Promise((resolve) => {
    zr.onReady = resolve;
  });
  zr.proc.stdout.on('data', (d) => {
    zr.buf += d.toString('utf8');
    let nl;
    while ((nl = zr.buf.indexOf('\n')) >= 0) {
      const line = zr.buf.slice(0, nl);
      zr.buf = zr.buf.slice(nl + 1);
      let m;
      try {
        m = JSON.parse(line);
      } catch {
        continue;
      }
      if (m.ready !== undefined && zr.onReady) {
        zr.onReady(m);
        zr.onReady = null;
      } else if (zr.waiting.has(m.id)) {
        zr.waiting.get(m.id)(m);
        zr.waiting.delete(m.id);
      }
    }
  });
  let err = '';
  zr.proc.stderr.on('data', (d) => (err += d.toString()));
  zr.proc.on('exit', () => {
    const why = err.trim().split('\n').pop() || 'the runner stopped';
    if (zr.onReady) zr.onReady({ ready: false, error: /No module named/.test(err) ? 'Python needs: pip install unicorn capstone' : why });
    for (const done of zr.waiting.values()) done({ ok: false, errors: [{ kind: 'crash', text: why, at: 0 }] });
    zr.waiting.clear();
    zr.proc = null;
  });
  zr.proc.on('error', () => {
    if (zr.onReady) zr.onReady({ ready: false, error: 'Python isn’t on this PC’s PATH' });
  });
  return zr.ready;
}
ipcMain.handle('zoo:run', async (_e, req) => {
  const r = await zooRunner();
  if (!r || !r.ready) return { ok: false, errors: [{ kind: 'crash', text: (r && r.error) || 'the runner didn’t start', at: 0 }] };
  const id = ++zr.id;
  return new Promise((resolve) => {
    zr.waiting.set(id, resolve);
    zr.proc.stdin.write(JSON.stringify({ ...req, id }) + '\n');
  });
});
app.on('will-quit', () => zr.proc && zr.proc.kill());

// re/lab/zoobin.py: zoo.exe's machine code where Ghidra has nothing to show -
// code it never made a function of, and every instruction touching a field
// (Functions: field:0x3d6). Started the first time it's asked for
const zb = { proc: null, buf: '', waiting: new Map(), id: 0, ready: null };
function zooBin() {
  if (zb.proc) return zb.ready;
  zb.proc = cp.spawn(labPython(), ['-X', 'utf8', '-u', at('lab', 'zoobin.py'), zooExePath(), path.join(app.getPath('userData'), 'zoobin')], { cwd: REPO || roots.state });
  zb.buf = '';
  zb.ready = new Promise((resolve) => (zb.onReady = resolve));
  zb.proc.stdout.on('data', (d) => {
    zb.buf += d.toString('utf8');
    let nl;
    while ((nl = zb.buf.indexOf('\n')) >= 0) {
      const line = zb.buf.slice(0, nl);
      zb.buf = zb.buf.slice(nl + 1);
      let m;
      try {
        m = JSON.parse(line);
      } catch {
        continue;
      }
      if (m.ready !== undefined && zb.onReady) {
        zb.onReady(m);
        zb.onReady = null;
      } else if (zb.waiting.has(m.id)) {
        zb.waiting.get(m.id)(m);
        zb.waiting.delete(m.id);
      }
    }
  });
  let err = '';
  zb.proc.stderr.on('data', (d) => (err += d.toString()));
  zb.proc.on('exit', () => {
    const why = /No module named/.test(err) ? 'Python needs: pip install capstone pefile' : err.trim().split('\n').pop() || 'it stopped';
    if (zb.onReady) zb.onReady({ ready: false, error: why });
    for (const done of zb.waiting.values()) done({ error: why });
    zb.waiting.clear();
    zb.proc = null;
  });
  zb.proc.on('error', () => zb.onReady && zb.onReady({ ready: false, error: 'Python isn’t on this PC’s PATH' }));
  return zb.ready;
}
async function zooBinAsk(req) {
  const r = await zooBin();
  if (!r || !r.ready) return { error: (r && r.error) || 'the helper didn’t start' };
  const id = ++zb.id;
  return new Promise((resolve) => {
    zb.waiting.set(id, resolve);
    zb.proc.stdin.write(JSON.stringify({ ...req, id }) + '\n');
  });
}
ipcMain.handle('zoo:bin', (_e, req) => zooBinAsk(req));
app.on('will-quit', () => zb.proc && zb.proc.kill());

// ------------------------------------------- growing the knowledge base
// Tools that fill re/knowledge.json from what zoo.exe says about itself,
// each shown in Original Game and saved through re/kb.py (by whoever's
// named in re/private/me.txt, or who's said):
// - Config loaders: every key a loader reads and where it goes (a field of
//   the struct it fills, or a global) - re/lab/zoobin.py "readers"
// - Class tables: a class's table of functions, slot by slot
// - The game's own text: names its debug text gives, the source file paths
const LOADERS_JSON = at('lab', 'loaders.json');
// The shared knowledge base (re/cloud): what every install of the Lab has
// worked out about zoo.exe, moved between checkouts by re/cloud/sync.py. What
// shares and what doesn't - and why only one build of the game is involved -
// is in re/cloud/README.md. Nothing happens by itself: the Lab only ever runs
// it when it is asked to, and `pull` writes to re/knowledge.json.
const CLOUD_CONF = path.join(PRIVATE, 'cloud.json');
// The endpoint is part of the Lab's public release configuration. A player's
// private setting or environment override can point a checkout elsewhere.
const CLOUD_RELEASE_CONF = at('cloud', 'config.json');
const CLOUD_API = () => (
  process.env.ZT1_KB_API ||
  (jsonFile(CLOUD_CONF, {}) || {}).api ||
  (jsonFile(CLOUD_RELEASE_CONF, {}) || {}).api ||
  ''
).trim();
// (10 minutes: sending a whole base goes up in pieces - 2,059 findings and
// 18,167 situations is 13 requests - so a short timeout would cut it off
// halfway through)
// (the zoo.exe sync.py should hash and send. The script resolves its own when
// it is given none - but an installed Lab's private state is not re/private,
// so the app hands it exactly what Play and the other views use. --zoo is
// optional in the script, so an older command line still works)
function cloudZooArgs() {
  const exe = zooExePath();
  return exe && fs.existsSync(exe) ? ['--zoo', exe] : [];
}
function cloudRun(args, timeout = 600000) {
  const api = CLOUD_API();
  if (!api) {
    return { ok: false, error: 'The Lab has no shared knowledge base configured. Check re/cloud/config.json, ZT1_KB_API, or re/private/cloud.json.' };
  }
  const wantsZoo = args[0] === 'contribute';
  const r = cp.spawnSync(labPython(), [at('cloud', 'sync.py'), ...args, '--api', api, ...(wantsZoo ? cloudZooArgs() : [])], {
    cwd: REPO || roots.state,
    encoding: 'utf8',
    windowsHide: true,
    timeout,
  });
  const said = ((r.stdout || '') + (r.stderr || '')).trim();
  return { ok: r.status === 0, said: said.slice(-4000), api };
}
// (what the shared base holds right now: the front page is written for a
// person to read, so it is fetched rather than run through sync.py)
ipcMain.handle('kb:cloud', () => {
  const api = CLOUD_API();
  if (!api) return { api: '', configured: false };
  return { api, configured: true };
});
ipcMain.handle('kb:cloudSync', (_e, what, dry) => {
  const known = ['pull', 'contribute', 'pending'];
  if (!known.includes(what)) return { ok: false, error: 'not a shared-base action: ' + what };
  return cloudRun([what, ...(dry ? ['--dry-run'] : [])]);
});

function kbRun(args, by) {
  const r = cp.spawnSync(labPython(), [at('kb.py'), ...args, ...(by ? ['--by', by] : [])], {
    cwd: REPO || roots.state,
    encoding: 'utf8',
    windowsHide: true,
    timeout: 120000,
  });
  return { ok: r.status === 0, said: ((r.stdout || '') + (r.stderr || '')).trim() };
}
async function ownersOf(addrs) {
  const out = {};
  for (let i = 0; i < addrs.length; i += 120) {
    const r = await ghidraGet('/owners?addrs=' + addrs.slice(i, i + 120).map((a) => a.replace(/^0x/, '')).join(','), 120000);
    Object.assign(out, (r && r.json && r.json.owners) || {});
  }
  return out;
}
const kbHex = (a) => '0x' + parseInt(a, 16).toString(16).padStart(6, '0');
// Every loader: its reads grouped by the function they're in (or the code
// they're in, where Ghidra made none), with what the knowledge base has
async function loadersList() {
  const cfg = jsonFile(LOADERS_JSON, { readers: [], structs: {} });
  const r = await zooBinAsk({ op: 'readers', readers: cfg.readers });
  if (r.error) return r;
  const owners = await ownersOf(r.reads.map((x) => x.at));
  const kb = jsonFile(KNOWLEDGE, { structs: {}, globals: {}, loaders: {} });
  const groups = new Map();
  for (const x of r.reads) {
    const o = owners[x.at.replace(/^0x/, '')] || owners[x.at];
    // (Ghidra's function, or the code it's in where it made none)
    const loader = o ? kbHex(o.address) : x.block || 'code ' + x.at;
    const g = groups.get(loader) || { loader, name: o ? o.name : '', reads: [] };
    g.reads.push(x);
    groups.set(loader, g);
  }
  const list = [...groups.values()].map((g) => {
    const known = (kb.loaders || {})[g.loader] || {};
    const struct = known.struct || cfg.structs[g.loader] || '';
    const fields = (struct && kb.structs[struct] && kb.structs[struct].fields) || {};
    let mapped = 0;
    const reads = g.reads.map((x) => {
      let have = null;
      if (x.dest && x.dest.field) {
        const f = fields['0x' + parseInt(x.dest.field, 16).toString(16).padStart(3, '0')];
        if (f) have = f.name;
      } else if (x.dest && x.dest.global) {
        const gl = (kb.globals || {})[kbHex(x.dest.global)];
        if (gl) have = gl.name;
      }
      mapped += have ? 1 : 0;
      return { ...x, have };
    });
    return {
      loader: g.loader,
      name: g.name,
      struct,
      reads,
      count: reads.length,
      fields: reads.filter((x) => x.dest && x.dest.field).length,
      globals: reads.filter((x) => x.dest && x.dest.global).length,
      mapped,
    };
  });
  list.sort((a, b) => b.count - a.count);
  return { loaders: list, reads: r.reads.length };
}
ipcMain.handle('kb:loaders', () => loadersList());
// Engine references where Ghidra made no function (code only reached through
// a table): each with where its code starts - zoobin's guess, just after the
// code before it ends - its first instructions, and what before it (a return,
// padding: a real start), to make a function there (Make it a function)
ipcMain.handle('kb:orphans', async () => {
  const refs = engineRefs();
  const addrs = Object.keys(refs);
  const owners = await ownersOf(addrs);
  if (!Object.keys(owners).length) return { error: 'Ghidra isn’t up (the pill at the top).' };
  const out = [];
  for (const a of addrs) {
    if (owners[a.replace(/^0x/, '')] || owners[a]) continue;
    const c = await zooBinAsk({ op: 'code', addr: a, count: 1 });
    if (c.error) return c;
    const i = c.lines.findIndex((l) => l.at === c.start);
    const before = await zooBinAsk({ op: 'code', addr: '0x' + (parseInt(c.start, 16) - 1).toString(16), count: 1 });
    out.push({
      addr: kbHex(a),
      start: c.start,
      bytesIn: parseInt(a, 16) - parseInt(c.start, 16),
      first: c.lines.slice(Math.max(0, i), Math.max(0, i) + 3).map((l) => l.text),
      before: (before.lines || []).slice(-1).map((l) => l.text)[0] || '',
      engine: refs[a],
    });
  }
  return { refs: addrs.length, orphans: out };
});
// A loader's keys into the knowledge base: its fields (where nothing's
// named) as the struct named, its globals by their keys
ipcMain.handle('kb:loaderSave', async (_e, loader, struct, by, confidence) => {
  const all = await loadersList();
  if (all.error) return all;
  const g = all.loaders.find((x) => x.loader === loader);
  if (!g) return { error: 'No loader ' + loader };
  const seen = new Set();
  const fields = [];
  const globals = [];
  for (const x of g.reads) {
    if (!x.dest) continue;
    if (x.dest.field && struct) {
      const off = '0x' + parseInt(x.dest.field, 16).toString(16);
      if (seen.has(off)) continue;
      seen.add(off);
      // (a key read twice into different fields: attenuation, attenuation2)
      let name = x.key;
      for (let n = 2; fields.some((f) => f.name === name); n++) name = x.key + n;
      fields.push({ off, name, type: x.kind === 'string' ? 'char *' : x.kind === 'float' ? 'float' : 'int', summary: `Its config key ${x.key} (read at ${x.at})` });
    } else if (x.dest.global) {
      globals.push({ addr: x.dest.global, name: x.key, summary: `Config key ${x.key} (read at ${x.at})` });
    }
  }
  fs.mkdirSync(PRIVATE, { recursive: true });
  const file = path.join(PRIVATE, 'loader-' + loader.replace(/^0x|^code /g, '').replace(/\W/g, '') + '.json');
  fs.writeFileSync(file, JSON.stringify({ loader, struct, fields, globals, keys: [...new Set(g.reads.map((x) => x.key))] }, null, 1));
  const r = kbRun(['loader-file', file, '--confidence', confidence || 'sure'], by);
  return r.ok ? { ok: true, said: r.said, fields: fields.length, globals: globals.length } : { error: r.said };
});

// Class tables: every table of functions zoo.exe's constructors store, the
// known ones named (re/lab/classes.json, then the knowledge base's own),
// each slot with the other classes having the same function there (what
// they share: the class they come from)
const CLASSES_JSON = at('lab', 'classes.json');
async function classTables() {
  const r = await zooBinAsk({ op: 'vtables' });
  if (r.error) return r;
  const cfg = jsonFile(CLASSES_JSON, { classes: {} }).classes;
  const kb = jsonFile(KNOWLEDGE, { classes: {}, functions: {} });
  const kbByTable = {};
  for (const [name, c] of Object.entries(kb.classes || {})) kbByTable[kbHex(c.vtable)] = name;
  const tables = r.tables.map((t) => ({ ...t, name: kbByTable[t.addr] || (cfg[t.addr] && cfg[t.addr].name) || '', how: (cfg[t.addr] && cfg[t.addr].how) || '', saved: !!kbByTable[t.addr] }));
  return { tables, named: tables.filter((t) => t.name).length };
}
ipcMain.handle('kb:classes', () => classTables());
// One table, slot by slot: each slot's function, what's known of it, and the
// named classes having it at the same slot
ipcMain.handle('kb:class', async (_e, addr) => {
  const all = await classTables();
  if (all.error) return all;
  const me = await zooBinAsk({ op: 'vtable', addr });
  if (me.error) return me;
  const named = all.tables.filter((t) => t.name && t.addr !== kbHex(addr));
  const others = {};
  for (const t of named) others[t.name] = (await zooBinAsk({ op: 'vtable', addr: t.addr })).slots || [];
  const kb = jsonFile(KNOWLEDGE, { functions: {} });
  const slots = me.slots.map((fa, i) => {
    const off = '0x' + (i * 4).toString(16);
    const k = kbHex(fa);
    const f = kb.functions[k];
    return {
      slot: off,
      function: k,
      known: f ? (f.class ? f.class + '::' : '') + f.name : '',
      shared: Object.entries(others)
        .filter(([, s]) => s[i] === fa)
        .map(([n]) => n),
    };
  });
  return { addr: kbHex(addr), slots };
});
// A class into the knowledge base: its name and every slot
ipcMain.handle('kb:classSave', async (_e, addr, name, by) => {
  const me = await zooBinAsk({ op: 'vtable', addr });
  if (me.error) return me;
  const slots = {};
  me.slots.forEach((fa, i) => (slots['0x' + (i * 4).toString(16)] = fa));
  fs.mkdirSync(PRIVATE, { recursive: true });
  const file = path.join(PRIVATE, 'class-' + name.replace(/\W/g, '') + '.json');
  const how = (jsonFile(CLASSES_JSON, { classes: {} }).classes[kbHex(addr)] || {}).how || '';
  fs.writeFileSync(file, JSON.stringify({ name, vtable: kbHex(addr), slots, summary: how }, null, 1));
  const r = kbRun(['class-file', file], by);
  return r.ok ? { ok: true, said: r.said, slots: me.slots.length } : { error: r.said };
});
// The game's own text: functions its debug text names (one name only - more
// are other functions' text built in), and the original's source files its
// paths give - into the knowledge base as provisional names and sources
async function textRows() {
  const dn = await ghidraGet('/debugnames', 180000);
  const fs2 = await ghidraGet('/fnstrings', 180000);
  if (!dn || !fs2) return { error: 'Ghidra isn’t up (the pill at the top).' };
  const kb = jsonFile(KNOWLEDGE, { functions: {} });
  const rows = new Map();
  for (const [a, names] of Object.entries(dn.json.functions || {})) {
    if (names.length !== 1) continue;
    const parts = names[0].split('::');
    rows.set(a, { address: a, name: parts.pop(), cls: parts.join('::'), summary: `Named by its own debug text ("${names[0]} - ...")` });
  }
  for (const [a, texts] of Object.entries(fs2.json.functions || {})) {
    const m = texts.map((t) => t.match(/\\aqua\\src\\(.+\.(?:cpp|c|h))$/i)).find(Boolean);
    if (!m) continue;
    const row = rows.get(a) || { address: a };
    row.source = m[1].replace(/\\/g, '/');
    rows.set(a, row);
  }
  // (one debug text in several functions - overloads, or copies built in -
  // numbered: unlockSurface, unlockSurface2... and said so)
  const byName = {};
  for (const r of [...rows.values()].sort((a, b) => parseInt(a.address, 16) - parseInt(b.address, 16)))
    if (r.name) (byName[r.cls + '::' + r.name] = byName[r.cls + '::' + r.name] || []).push(r);
  for (const same of Object.values(byName))
    if (same.length > 1)
      same.forEach((r, i) => {
        r.summary += ` - one of ${same.length} functions with this text (overloads, or copies built in)`;
        if (i) r.name += i + 1;
      });
  const list = [...rows.values()].map((r) => ({ ...r, known: !!kb.functions[kbHex(r.address)] }));
  return { rows: list, names: list.filter((r) => r.name && !r.known).length, sources: list.filter((r) => r.source).length };
}
ipcMain.handle('kb:text', () => textRows());
ipcMain.handle('kb:textSave', async (_e, by) => {
  const t = await textRows();
  if (t.error) return t;
  fs.mkdirSync(PRIVATE, { recursive: true });
  const file = path.join(PRIVATE, 'from-text.json');
  fs.writeFileSync(file, JSON.stringify(t.rows, null, 1));
  const r = kbRun(['text-file', file, '--confidence', 'likely'], by);
  return r.ok ? { ok: true, said: r.said } : { error: r.said };
});

ipcMain.handle('me:set', (_e, name) => {
  fs.mkdirSync(PRIVATE, { recursive: true });
  fs.writeFileSync(path.join(PRIVATE, 'me.txt'), name.trim() + '\n');
  return true;
});

// ------------------------------------------------------ mods and source
// (a path the page named, kept inside dir - and null when there's no dir to
// keep it in: a Lab with no checkout has no source to read or write)
function inside(dir, p) {
  if (!dir) return null;
  const full = path.resolve(dir, p);
  return full.startsWith(dir + path.sep) ? full : null;
}

ipcMain.handle('mods:list', () => {
  if (!SCRIPTS) return [];
  fs.mkdirSync(SCRIPTS, { recursive: true });
  return fs
    .readdirSync(SCRIPTS)
    .filter((f) => f.endsWith('.cpp'))
    .map((f) => {
      const stem = f.slice(0, -4);
      const text = fs.readFileSync(path.join(SCRIPTS, f), 'utf8');
      let meta = {};
      try {
        meta = JSON.parse(fs.readFileSync(path.join(SCRIPTS, stem + '.json'), 'utf8'));
      } catch {}
      const about = (text.match(/lab_about\(\)\s*\{\s*return\s*"([^"]*)"/) || [])[1] || '';
      const addresses = [...text.matchAll(/zoo\.exe\s+(0x[0-9a-fA-F]{6})/g)].map((m) => m[1].toLowerCase());
      return { stem, about, addresses, meta, mtime: fs.statSync(path.join(SCRIPTS, f)).mtimeMs };
    });
});
ipcMain.handle('mods:read', (_e, name) => {
  const p = inside(SCRIPTS, name);
  if (!p || !fs.existsSync(p)) return null;
  try {
    return fs.readFileSync(p, 'utf8');
  } catch {
    return null;
  }
});
ipcMain.handle('mods:write', (_e, name, text) => {
  const p = inside(SCRIPTS, name);
  if (!p) return false;
  fs.writeFileSync(p, text);
  return true;
});
ipcMain.handle('mods:exists', (_e, name) => {
  const p = inside(SCRIPTS, name);
  return !!p && fs.existsSync(p);
});
ipcMain.handle('src:read', (_e, rel, line, before = 3, after = 30) => {
  const p = inside(REPO, rel);
  if (!p) return null;
  try {
    const lines = fs.readFileSync(p, 'utf8').split(/\r?\n/);
    const from = Math.max(0, line - 1 - before);
    return { from: from + 1, lines: lines.slice(from, line - 1 + after) };
  } catch {
    return null;
  }
});
// (a pull request, in the browser: GitHub's pages only)
ipcMain.handle('open:url', (_e, url) => {
  if (/^https:\/\/github\.com\/[\w.-]+\/[\w.-]+\/pull\/\d+$/.test(String(url || ''))) shell.openExternal(url);
});
// (a file shown in its folder: a release's zip)
ipcMain.handle('open:reveal', (_e, rel) => {
  const p = inside(REPO, rel);
  if (p && fs.existsSync(p)) shell.showItemInFolder(p);
});
ipcMain.handle('open:file', (_e, rel, line) => {
  const p = inside(REPO, rel);
  if (!p) return;
  // (VS Code to the line if it's there, else whatever opens the file. This
  // must NOT go through a shell: `code` is found on PATH as code.cmd, and
  // shell:true would hand the path and line to cmd.exe, where a path holding
  // & | ^ > would be run as a command. shell:false runs the shim directly, and
  // the line is a number or nothing)
  const n = Math.floor(Number(line));
  const at = Number.isFinite(n) && n > 0 ? n : 1;
  const code = cp.spawn('code', ['-g', `${p}:${at}`], { shell: false, detached: true, stdio: 'ignore' });
  code.on('error', () => shell.openPath(p));
  code.on('exit', (c) => c && shell.openPath(p));
});

// ------------------------------------------------------- engine source
const SRC = inRepo('src');
function srcFiles(dir = SRC, out = []) {
  if (!dir) return out;
  let entries = [];
  try {
    entries = fs.readdirSync(dir, { withFileTypes: true });
  } catch {
    return out; // (no checkout: the views that list the source show nothing)
  }
  for (const e of entries) {
    const p = path.join(dir, e.name);
    if (e.isDirectory()) srcFiles(p, out);
    else if (/\.(cpp|hpp|h|c)$/i.test(e.name)) out.push(path.relative(REPO, p).replace(/\\/g, '/'));
  }
  return out;
}
// (which 'vendor': the libraries built with it - SDL, libzip, the PE loader -
// whose fixes are the engine's too)
const VENDOR = inRepo('vendor');
ipcMain.handle('src:list', (_e, which) => {
  if (which !== 'vendor') return srcFiles().sort();
  try {
    return srcFiles(VENDOR).sort();
  } catch {
    return [];
  }
});
// Any file, read only (a header clangd's go to definition leads to)
ipcMain.handle('file:readAny', (_e, abs) => {
  try {
    if (!/\.(h|hpp|hh|hxx|inl|ipp|c|cc|cpp|cxx|tcc)$/i.test(abs) && !/[\\/]include[\\/]/i.test(abs)) return null;    return { text: fs.readFileSync(abs, 'utf8').replace(/\r\n/g, '\n') };
  } catch {
    return null;
  }
});
ipcMain.handle('src:readAll', (_e, rel) => {
  const p = inside(REPO, rel);
  if (!p) return null;
  try {
    const raw = fs.readFileSync(p, 'utf8');
    return { text: raw.replace(/\r\n/g, '\n'), crlf: raw.includes('\r\n') };
  } catch {
    return null;
  }
});
// A new engine file (src/, .cpp/.hpp/.h, not there yet): Engine code's New
// file, and New engine code for a game file
ipcMain.handle('src:create', (_e, rel, text) => {
  if (!REPO) return { ok: false, error: NO_CHECKOUT };
  const p = inside(REPO, rel);
  if (!p || !p.startsWith(SRC + path.sep) || !/\.(cpp|hpp|h)$/i.test(p)) return { ok: false, error: 'A new file goes in src/, as .cpp, .hpp or .h' };
  if (fs.existsSync(p)) return { ok: false, error: rel + ' is there already' };
  fs.mkdirSync(path.dirname(p), { recursive: true });
  fs.writeFileSync(p, text || '');
  return { ok: true };
});
ipcMain.handle('src:write', (_e, rel, text, crlf) => {
  if (!REPO) return false;
  const p = inside(REPO, rel);
  if (!p || !(p.startsWith(SRC + path.sep) || p.startsWith(VENDOR + path.sep))) return false; // (the engine's source and its libraries' only)
  fs.writeFileSync(p, crlf ? text.replace(/\n/g, '\r\n') : text);
  return true;
});
// Every line of the engine's source containing q (any case)
ipcMain.handle('src:grep', (_e, q) => {
  const needle = String(q || '').toLowerCase();
  const out = [];
  if (needle.length < 2) return out;
  for (const rel of srcFiles()) {
    const lines = readMaybe(path.join(REPO, rel), '').split(/\r?\n/);
    for (let i = 0; i < lines.length; i++)
      if (lines[i].toLowerCase().includes(needle)) {
        out.push({ file: rel, line: i + 1, text: lines[i].trim().slice(0, 200) });
        if (out.length >= 400) return out;
      }
  }
  return out;
});
// Which of a game file's keys our engine reads ("cprey" in quotes, any
// case): a species' .ai is never named - every one goes through the same
// loader - but its keys are. Each key read, with where it's first read
ipcMain.handle('src:keys', (_e, keys) => {
  const want = new Map((keys || []).map((k) => [String(k).toLowerCase(), k]));
  const out = {};
  for (const rel of srcFiles()) {
    const lines = readMaybe(path.join(REPO, rel), '').split(/\r?\n/);
    for (let i = 0; i < lines.length; i++) {
      const l = lines[i].toLowerCase();
      if (!l.includes('"')) continue;
      for (const m of l.matchAll(/"([a-z][a-z0-9_]*)"/g))
        if (want.has(m[1]) && !out[want.get(m[1])]) out[want.get(m[1])] = { file: rel, line: i + 1 };
    }
  }
  return out;
});

// Building the engine (MSBuild on build/zt1-engine.vcxproj, Release): its
// output line by line, then whether it worked and each problem
const MSBUILD_CANDIDATES = [
  'C:/Program Files/Microsoft Visual Studio/18/Community/MSBuild/Current/Bin/amd64/MSBuild.exe',
  'C:/Program Files/Microsoft Visual Studio/18/Professional/MSBuild/Current/Bin/amd64/MSBuild.exe',
  'C:/Program Files/Microsoft Visual Studio/2022/Community/MSBuild/Current/Bin/amd64/MSBuild.exe',
  'C:/Program Files/Microsoft Visual Studio/2022/Professional/MSBuild/Current/Bin/amd64/MSBuild.exe',
];
let engineBuild = null;
ipcMain.handle('engine:build', () => {
  if (engineBuild) return { error: 'Already building.' };
  // (building is the checkout's: there's nothing to build without one)
  if (!REPO) return { error: NO_CHECKOUT };
  const msbuild = process.env.ZT1_MSBUILD || MSBUILD_CANDIDATES.find((p) => fs.existsSync(p));
  if (!msbuild) return { error: 'No MSBuild found (Visual Studio). Set ZT1_MSBUILD to its path.' };
  const t0 = Date.now();
  const problems = [];
  // (the 64-bit build when it's set up - Make release sets it up - else
  // the 32-bit one)
  const proj = fs.existsSync(path.join(BUILD64, 'zt1-engine.vcxproj')) ? path.join(BUILD64, 'zt1-engine.vcxproj') : path.join(REPO, 'build', 'zt1-engine.vcxproj');
  // (CMake's own check first, ZERO_CHECK: a new source file - Engine code's
  // New file, a reader - gets into the project before the engine's built,
  // not one build late: the project MSBuild had loaded didn't have it)
  const zc = path.join(path.dirname(proj), 'ZERO_CHECK.vcxproj');
  if (fs.existsSync(zc)) {
    send('build:line', 'Checking for new source files…');
    cp.spawnSync(msbuild, [zc, '-p:Configuration=Release', '-v:quiet', '-nologo'], { cwd: REPO, timeout: 180000, windowsHide: true });
  }
  const proc = cp.spawn(msbuild, [proj, '-p:Configuration=Release', '-m', '-v:minimal', '-nologo'], {
    cwd: REPO,
  });
  engineBuild = proc;
  let buf = '';
  const onData = (d) => {
    buf += d.toString();
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl).replace(/\r$/, '');
      buf = buf.slice(nl + 1);
      if (!line.trim()) continue;
      // file(line,col): error C1234: message [project]
      const m = line.match(/^\s*(.+?)\((\d+)(?:,(\d+))?\):\s*(fatal error|error|warning)\s+(\w+):\s*(.*?)(?:\s+\[[^\]]+\])?$/);
      if (m && m[4] !== 'warning') {
        const rel = path.relative(REPO, m[1]).replace(/\\/g, '/');
        const p = { file: rel, line: +m[2], col: +(m[3] || 0), kind: m[4], code: m[5], message: m[6] };
        const last = problems[problems.length - 1];
        const same = (x) => x.file === p.file && x.line === p.line && x.col === p.col && x.code === p.code;
        // (one message over several lines - "with [ _Ty=int ]" - is one
        // problem; the summary at the end repeats them all)
        if (last && same(last) && (/^(with|\[|\]|_?\w+=)/.test(p.message) || last.joining)) {
          last.message += ' ' + p.message;
          last.joining = !/^\]/.test(p.message);
          if (!last.joining) delete last.joining;
        } else if (!problems.some((x) => same(x) && x.message.startsWith(p.message))) problems.push(p);
      }
      // (warnings in the vendored libraries: noise)
      if (/warning/.test(line) && !/[\\/]src[\\/]/.test(line)) continue;
      send('build:line', line.trim().replace(/\s+\[[^\]]+\.vcxproj\]$/, ''));
    }
  };
  proc.stdout.on('data', onData);
  proc.stderr.on('data', onData);
  proc.on('exit', async (code) => {
    // (then the original game's hook, zt1-hook.dll and zt1-original.exe:
    // 32-bit like zoo.exe, so from the 32-bit tree in build/ - Play original)
    if (code === 0 && !problems.length) code = await buildOriginal(msbuild, onData);
    engineBuild = null;
    const ok = code === 0 && !problems.length;
    // (built: the zoo.exe addresses the code now cites, gathered again, so
    // Functions shows what's in our engine as soon as it is)
    if (ok) cp.spawnSync(labPython(), [at('scan_engine.py')], { cwd: REPO || roots.state, timeout: 60000 });
    send('build:done', { ok, problems, secs: Math.round((Date.now() - t0) / 100) / 10 });
    if (ok) {
      // (the new engine: the lab zoo's, and the game's when it's next started)
      restartHost();
    }
  });
  return { ok: true };
});

// The original game's hook (src/original/, CMakeLists' part 12), built in
// the 32-bit tree: its output through the same reader as the engine's.
// Resolves to MSBuild's exit code (0 when there's no 32-bit tree)
function buildOriginal(msbuild, onData) {
  const tree = path.join(REPO, 'build');
  const zc = path.join(tree, 'ZERO_CHECK.vcxproj');
  if (!fs.existsSync(zc)) return Promise.resolve(0);
  send('build:line', 'The original game’s hook (32-bit)…');
  cp.spawnSync(msbuild, [zc, '-p:Configuration=Release', '-v:quiet', '-nologo'], { cwd: REPO, timeout: 180000, windowsHide: true });
  const proj = path.join(tree, 'zt1-original.vcxproj');
  if (!fs.existsSync(proj)) return Promise.resolve(0);
  return new Promise((done) => {
    const p = cp.spawn(msbuild, [proj, '-p:Configuration=Release', '-m', '-v:minimal', '-nologo'], { cwd: REPO, windowsHide: true });
    engineBuild = p;
    p.stdout.on('data', onData);
    p.stderr.on('data', onData);
    p.on('exit', (c) => done(c || 0));
  });
}

// Making a release (engine-build-resources/make_release.py): the engine
// built x64 Release in build-x64/, then dist/zt1-engine-<date>-win64.zip and
// its .sha256, without any of the original game's files (checked against the
// game's own archives, in zoo.exe's folder). Its output goes to Build output.
// Nothing is uploaded
let engineRelease = null;
// ------------------------------------------------------------------- Mods
// The game's mods (src/Mods.hpp): folders in the mods folder - mods.cfg's
// "folder=", else mods/ by the engine - each a mod.json and its files;
// mods.cfg's "enabled=" lines are the ones switched on. The Lab engine
// loads them when it starts.
function modsCfg() {
  const cfg = { folder: '', enabled: [] };
  try {
    for (const line of fs.readFileSync(path.join(RELEASE, 'mods.cfg'), 'utf8').split(/\r?\n/)) {
      const at = line.indexOf('=');
      if (at < 0) continue;
      const k = line.slice(0, at), v = line.slice(at + 1);
      if (k === 'folder') cfg.folder = v;
      else if (k === 'enabled' && v) cfg.enabled.push(v);
    }
  } catch {}
  return cfg;
}
function modsFolder() {
  return modsCfg().folder || path.join(RELEASE, 'mods');
}
ipcMain.handle('gamemods:list', () => {
  const cfg = modsCfg();
  const root = modsFolder();
  const out = [];
  try {
    for (const d of fs.readdirSync(root, { withFileTypes: true })) {
      if (!d.isDirectory()) continue;
      let info = {};
      try {
        info = JSON.parse(fs.readFileSync(path.join(root, d.name, 'mod.json'), 'utf8'));
      } catch {}
      out.push({ folder: d.name, path: path.join(root, d.name), ...info, enabled: cfg.enabled.includes(d.name) });
    }
  } catch {}
  return { folder: root, mods: out };
});
ipcMain.handle('gamemods:enable', (_e, folder, on) => {
  const cfg = modsCfg();
  const set = new Set(cfg.enabled);
  if (on) set.add(folder);
  else set.delete(folder);
  const lines = [];
  if (cfg.folder) lines.push('folder=' + cfg.folder);
  for (const e of set) lines.push('enabled=' + e);
  fs.writeFileSync(path.join(RELEASE, 'mods.cfg'), lines.join('\n') + '\n');
  return { ok: true, enabled: [...set] };
});
// The animation maker (re/lab/animaker.py): an animation's colours, or a
// new one made from it as a mod
ipcMain.handle('anim:tool', (_e, req) => {
  return new Promise((resolve) => {
    // (the engine's folder, which is also where the game's files are: the
    // animation it's asked about comes from beside it either way)
    const engineDir = roots.engineDir || RELEASE;
    const tool = PY && path.join(PY, 'animaker.py');
    if (!tool) return resolve({ ok: false, error: roots.looked('animaker.py') });
    const proc = cp.spawn(labPython(), ['-I', '-X', 'utf8', tool], { cwd: engineDir });
    let out = '', err = '';
    proc.stdout.on('data', (d) => (out += d));
    proc.stderr.on('data', (d) => (err += d));
    proc.on('exit', () => {
      try {
        resolve(JSON.parse(out));
      } catch {
        resolve({ ok: false, error: (err || out || 'no answer').slice(0, 400) });
      }
    });
    proc.stdin.end(JSON.stringify({ engineDir, modsFolder: modsFolder(), ...req }));
  });
});

ipcMain.handle('engine:release', (_e, opts = {}) => {
  if (!REPO) return { error: NO_CHECKOUT };
  if (engineRelease || engineBuild) return { error: 'Already building.' };
  const args = ['-X', 'utf8', '-u', path.join(REPO, 'engine-build-resources', 'make_release.py')];
  if (opts.noBuild) args.push('--no-build');
  const t0 = Date.now();
  const r = { zip: '', sha: '', error: '', leftOut: [] };
  const proc = cp.spawn(labPython(), args, { cwd: REPO || roots.state });
  engineRelease = proc;
  let buf = '';
  let inLeft = false;
  const onData = (d) => {
    buf += d.toString();
    let nl;
    while ((nl = buf.indexOf('\n')) >= 0) {
      const line = buf.slice(0, nl).replace(/\r$/, '');
      buf = buf.slice(nl + 1);
      if (!line.trim()) continue;
      let m;
      if ((m = line.match(/^\[OK\] (\S+): (.*)$/))) (r.zip = m[1]), (r.what = m[2]);
      else if ((m = line.match(/sha256 ([0-9a-f]{64})/))) r.sha = m[1];
      else if (/^\[X\]/.test(line)) r.error = line.slice(4);
      if (/^Left out of pc-sync/.test(line)) inLeft = true;
      else if (inLeft && /^\s{3}\S/.test(line)) r.leftOut.push(line.trim());
      else inLeft = false;
      // (the vendored libraries' warnings: noise)
      if (/warning/.test(line) && !/[\\/]src[\\/]/.test(line)) continue;
      send('build:line', line.trim().replace(/\s+\[[^\]]+\.vcxproj\]$/, ''));
    }
  };
  proc.stdout.on('data', onData);
  proc.stderr.on('data', onData);
  proc.on('error', () => {
    engineRelease = null;
    send('release:done', { ok: false, error: 'Python isn’t on this PC’s PATH', secs: 0 });
  });
  proc.on('exit', (code) => {
    engineRelease = null;
    send('release:done', { ok: code === 0 && !!r.zip, ...r, error: code === 0 ? r.error : r.error || `stopped (${code})`, secs: Math.round((Date.now() - t0) / 100) / 10 });
  });
  return { ok: true };
});

// ------------------------------------------------ Sync (sync.js)
// The engine's changes to GitHub (the home: a branch, a pull request) and
// CodeRook (a mirror of main), from Engine code's Sync tab
const sync = require('./sync.js')(REPO, (line) => send('sync:line', line));
for (const [name, fn] of Object.entries({
  status: (a) => sync.status(a || {}),
  pull: () => sync.pull && sync.pull(),
  resolve: (a) => sync.resolve && sync.resolve(a.file, a.how),
  abort: () => sync.abortMerge && sync.abortMerge(),
  commit: (a) => sync.commit && sync.commit(a.message),
  push: () => sync.push && sync.push(),
  propose: (a) => sync.propose && sync.propose(a.title, a.body),
  branch: (a) => sync.newBranch && sync.newBranch(a.name),
  mirror: () => sync.mirror && sync.mirror(),
}))
  ipcMain.handle('sync:' + name, (_e, a) => {
    if (typeof fn(a || {}) === 'undefined') return { ok: false, error: NO_CHECKOUT };
    return fn(a || {}).catch((e) => ({ ok: false, error: String(e && e.message ? e.message : e) }));
  });

// --------------------------------------------- clangd (code intelligence)
// The C++ language server, on the engine's code (compile_flags.txt at the
// repo's root says how it's built): autocomplete, hover, go to definition,
// errors as you type. Its messages (LSP, framed by Content-Length) go to
// and from the page as they are
const lsp = { proc: null, buf: Buffer.alloc(0) };
// (the clangd the app carries, or the one in a checkout's lab-app/clangd/
// while developing. Named rather than found when the shipped one is flat -
// tree/clangd/bin/clangd.exe)
function clangdExe() {
  if (roots.clangd) return roots.clangd;
  const dir = roots.clangdInApp || path.join(__dirname, 'clangd');
  try {
    for (const d of fs.readdirSync(dir)) {
      const p = path.join(dir, d, 'bin', 'clangd.exe');
      if (fs.existsSync(p)) return p;
    }
  } catch {}
  return null;
}
// Each page load starts its own clangd: a reloaded page initialises it
// afresh, and it picks up compile_flags.txt as it is now
// Every engine source with compile_flags.txt's flags, as a compile database:
// with only compile_flags.txt clangd knows just the files open, so "who calls
// this" missed every caller in a file not open. With it, its background
// index covers the whole engine (kept in .cache/clangd)
function writeCompileDb() {
  try {
    const flags = fs.readFileSync(roots.compileFlags, 'utf8').split(/\r?\n/).map((l) => l.trim()).filter(Boolean);
    const files = [];
    const walk = (d) => {
      for (const e of fs.readdirSync(d, { withFileTypes: true })) {
        const p = path.join(d, e.name);
        if (e.isDirectory()) walk(p);
        else if (/\.(cpp|cc|c)$/i.test(e.name)) files.push(p);
      }
    };
    walk(path.join(REPO, 'src'));
    const dir = path.join(PRIVATE, 'lab-clangd');
    fs.mkdirSync(dir, { recursive: true });
    const db = files.map((f) => ({ directory: REPO, file: f, arguments: ['clang++', ...flags, f] }));
    fs.writeFileSync(path.join(dir, 'compile_commands.json'), JSON.stringify(db, null, 1));
    return dir;
  } catch {
    return null;
  }
}
ipcMain.handle('lsp:start', () => {
  if (lsp.proc) lsp.proc.kill();
  // (the code intelligence is the checkout's: it needs the engine's source)
  if (!REPO) return { error: NO_CHECKOUT };
  const exe = clangdExe();
  if (!exe) return { error: 'clangd isn’t here: ' + roots.looked('clangd') };
  const db = writeCompileDb();
  const args = ['--background-index', '--header-insertion=never', '--clang-tidy=false', '-j=4', '--pch-storage=memory'];
  if (db) args.push('--compile-commands-dir=' + db);
  const proc = cp.spawn(exe, args, {
    cwd: REPO,
    stdio: ['pipe', 'pipe', 'pipe'],
  });
  lsp.proc = proc;
  lsp.buf = Buffer.alloc(0);
  proc.stdout.on('data', (d) => {
    if (lsp.proc !== proc) return;
    lsp.buf = Buffer.concat([lsp.buf, d]);
    for (;;) {
      const head = lsp.buf.indexOf('\r\n\r\n');
      if (head < 0) return;
      const m = lsp.buf.subarray(0, head).toString().match(/Content-Length:\s*(\d+)/i);
      const n = m ? +m[1] : 0;
      if (lsp.buf.length < head + 4 + n) return;
      const body = lsp.buf.subarray(head + 4, head + 4 + n).toString('utf8');
      lsp.buf = lsp.buf.subarray(head + 4 + n);
      try {
        send('lsp:msg', JSON.parse(body));
      } catch {}
    }
  });
  proc.stderr.on('data', () => {});
  proc.on('exit', () => {
    if (lsp.proc !== proc) return; // an old one, replaced
    lsp.proc = null;
    send('lsp:exit', {});
  });
  return { ok: true, root: REPO };
});
ipcMain.on('lsp:send', (_e, msg) => {
  if (!lsp.proc || !lsp.proc.stdin.writable) return;
  const body = Buffer.from(JSON.stringify(msg), 'utf8');
  lsp.proc.stdin.write(`Content-Length: ${body.length}\r\n\r\n`);
  lsp.proc.stdin.write(body);
});
app.on('will-quit', () => lsp.proc && lsp.proc.kill());

// ------------------------------------------------- control (for the MCP)
// 127.0.0.1:18492, this PC only: POST /call {"method", "params"}. A call
// runs in the page through the same code a click does (so it's seen
// happening); a screenshot is the window as it is, without needing it in
// front. lab-app/mcp/server.js turns these into an AI agent's tools
const CONTROL_PORT = 18492;
const ctl = { id: 0, pending: new Map() };
ipcMain.on('ctl:result', (_e, { id, result, error }) => {
  const p = ctl.pending.get(id);
  if (!p) return;
  ctl.pending.delete(id);
  p(error ? { error } : { result });
});
function pageCall(method, params, timeout = 120000) {
  return new Promise((resolve) => {
    if (!win || win.isDestroyed()) return resolve({ error: 'The Lab window is closed.' });
    const id = ++ctl.id;
    ctl.pending.set(id, resolve);
    send('ctl:call', { id, method, params });
    setTimeout(() => ctl.pending.has(id) && (ctl.pending.delete(id), resolve({ error: 'The page didn’t answer in time.' })), timeout);
  });
}
http
  .createServer((req, res) => {
    let body = '';
    req.on('data', (c) => (body += c));
    req.on('end', async () => {
      let out;
      try {
        const { method, params } = JSON.parse(body || '{}');
        if (method === 'screenshot') {
          // (a minimised window isn't drawn: shown again first, not in front
          // of what's being used)
          if (win.isMinimized()) {
            win.showInactive();
            await new Promise((r) => setTimeout(r, 400));
          }
          let img = await win.webContents.capturePage();
          if (img.isEmpty()) {
            await new Promise((r) => setTimeout(r, 300));
            img = await win.webContents.capturePage();
          }
          const size = img.getSize();
          const scale = Math.min(1, (params && params.width ? params.width : 1600) / size.width);
          const png = (scale < 1 ? img.resize({ width: Math.round(size.width * scale) }) : img).toPNG();
          out = { result: { png: png.toString('base64'), width: size.width, height: size.height } };
        } else if (method === 'ping') out = { result: { ok: true } };
        else if (method === 'reload') {
          // (the page again from disk: after the Lab's own files change -
          // its unsaved engine edits saved first, not thrown away)
          await pageCall('saveEngine', {}, 8000);
          win.webContents.reloadIgnoringCache();
          await new Promise((r) => win.webContents.once('did-finish-load', r));
          await new Promise((r) => setTimeout(r, 1500));
          out = { result: { ok: true } };
        } else if (method === 'restart') {
          // (the whole app again: after main.js changes. It closes as if the
          // window were closed - engines and clangd stop - then starts again)
          await pageCall('saveEngine', {}, 8000);
          // (and the Test Zoo's set-up kept, to be made again)
          await pageCall('keepRebuild', {}, 5000);
          out = { result: { ok: true } };
          app.relaunch();
          setTimeout(() => win.close(), 200);
        }
        else out = await pageCall(method, params || {});
      } catch (e) {
        out = { error: String(e && e.message ? e.message : e) };
      }
      res.writeHead(200, { 'Content-Type': 'application/json' });
      res.end(JSON.stringify(out));
    });
  })
  .on('error', () => {}) // (another Lab already has the port)
  .listen(CONTROL_PORT, '127.0.0.1');

// ------------------------------------------------------------------ app
const WIN_STATE = path.join(PRIVATE, 'lab-window.json');
function loadWinState() {
  try {
    return JSON.parse(fs.readFileSync(WIN_STATE, 'utf8'));
  } catch {
    return {};
  }
}
function saveWinState(s) {
  try {
    fs.mkdirSync(PRIVATE, { recursive: true });
    fs.writeFileSync(WIN_STATE, JSON.stringify(s));
  } catch {}
}

function createWindow() {
  win = new BrowserWindow({
    icon: path.join(__dirname, 'icon.png'),
    title: 'ZT1 Lab · Engine Workbench',
    width: 1600,
    height: 960,
    minWidth: 1100,
    minHeight: 640,
    backgroundColor: '#0c0e10',
    title: 'ZT1 Lab',
    autoHideMenuBar: true,
    // (it keeps working while out of sight: the MCP drives it from behind)
    webPreferences: { preload: path.join(__dirname, 'preload.js'), contextIsolation: true, nodeIntegration: false, backgroundThrottling: false },
  });
  // (no menu bar at all: Alt would otherwise bring up File/Edit/View and
  // take the keyboard)
  win.removeMenu();
  // (the keys the menu gave: reload the page, the developer tools)
  win.webContents.on('before-input-event', (e, input) => {
    if (input.type !== 'keyDown') return;
    const k = input.key.toLowerCase();
    if (k === 'f5' || ((input.control || input.meta) && k === 'r')) {
      e.preventDefault();
      win.webContents.reloadIgnoringCache();
    } else if (k === 'f12' || ((input.control || input.meta) && input.shift && k === 'i')) {
      e.preventDefault();
      win.webContents.toggleDevTools();
    }
  });
  // (as it was left: maximised, or where and how big)
  const st = loadWinState();
  if (st.bounds) win.setBounds(st.bounds);
  if (st.maximized) win.maximize();
  const save = () => saveWinState({ maximized: win.isMaximized(), bounds: win.isMaximized() ? st.bounds : win.getBounds() });
  win.on('close', save);
  win.loadFile(path.join(__dirname, 'renderer', 'index.html'));
  win.on('closed', () => (win = null));
  // (the game's window follows this one)
  for (const ev of ['move', 'resize', 'maximize', 'unmaximize', 'restore', 'minimize']) win.on(ev, placeGame);
  // (the page's errors: in this process's output, in .cache/lab-page.log, and
  // in the Lab's Log - a page that breaks says why somewhere you can read)
  const pageLog = path.join(PRIVATE, 'lab-page.log');
  try {
    fs.mkdirSync(path.dirname(pageLog), { recursive: true });
    fs.writeFileSync(pageLog, '');
  } catch {}
  win.webContents.on('console-message', (e) => {
    const { level, message, lineNumber, sourceId } = e;
    if (level === 'error' || level === 'warning' || level >= 2) {
      const line = `[page ${level}] ${message} (${path.basename(sourceId || '')}:${lineNumber})`;
      console.log(line);
      try {
        fs.appendFileSync(pageLog, new Date().toISOString().slice(11, 19) + ' ' + line + '\n');
      } catch {}
      send('engine:msg', { t: 'log', text: line });
    }
  });
}

// Testing without a mouse: ZT1_LAB_SHOTS=<dir> goes through the views
// (ZT1_LAB_TOUR: tab[:seconds][@action] ...), saves each, and quits
async function shotTour(dir) {
  const wait = (ms) => new Promise((r) => setTimeout(r, ms));
  fs.mkdirSync(dir, { recursive: true });
  await wait(Number(process.env.ZT1_LAB_WARMUP || 14000));
  const tour = (process.env.ZT1_LAB_TOUR || 'scene functions code mods play').split(/\s+/);
  let i = 0;
  for (const step of tour) {
    const [what, act] = step.split('@');
    const [tab, secs] = what.split(':');
    send('test:do', { tab, act: act || '' });
    await wait(Number(secs || 4) * 1000);
    const img = await win.webContents.capturePage();
    fs.writeFileSync(path.join(dir, `${String(++i).padStart(2, '0')}_${tab}${act ? '_' + act.replace(/\W+/g, '') : ''}.png`), img.toPNG());
  }
  win.close();
}

app.whenReady().then(() => {
  createWindow();
  startHost();
  // (Ghidra's bridge too, when no Ghidra of yours has it on)
  startOwnGhidra();
  if (process.env.ZT1_LAB_SHOTS) shotTour(process.env.ZT1_LAB_SHOTS);
});
app.on('window-all-closed', () => {
  if (host.sock) host.sock.write('{"cmd":"quit"}\n');
  if (play.proc && play.proc.stdin.writable) play.proc.stdin.write('quit\n');
  setTimeout(() => {
    if (host.proc) host.proc.kill();
    if (play.proc) play.proc.kill();
    app.quit();
  }, 600);
});
