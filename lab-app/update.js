'use strict';

const https = require('https');
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

// Where the Lab's releases are looked for, in turn: CodeRook's zt1-engine
// project (its release's downloads), then GitHub's latest release. The
// manifest is signed either way, and the first that's valid is taken.
const SOURCES = [
  'https://api.coderook.com/v1/public/projects/batty-251/zt1-engine/updates/',
  'https://github.com/AlexC1991/zt1-engine/releases/latest/download/',
];
const PUBLIC_KEY = `-----BEGIN PUBLIC KEY-----\nMCowBQYDK2VwAyEABGQRg9tkIg53GGoBPFrdqdi1DV5/6sAa3Np2bYdA+hE=\n-----END PUBLIC KEY-----\n`;
const APP_ID = 'zt1-lab';

function compareVersions(a, b) {
  const aa = a.split('.').map(Number), bb = b.split('.').map(Number);
  for (let i = 0; i < 3; i++) if (aa[i] !== bb[i]) return aa[i] - bb[i];
  return 0;
}

function request(url, onResponse, redirects = 0) {
  return new Promise((resolve, reject) => {
    if (redirects > 5) return reject(new Error('Too many redirects'));
    // (https only - but a test feed on this PC, ZT1_LAB_UPDATE_FEED at
    // 127.0.0.1 or localhost, may be plain http: what it serves is checked
    // against the signature all the same)
    const u = new URL(url);
    const local = u.protocol === 'http:' && (u.hostname === '127.0.0.1' || u.hostname === 'localhost');
    if (u.protocol !== 'https:' && !local) return reject(new Error(`Updates come over https only (${u.protocol})`));
    const req = (local ? require('http') : https).get(url, {headers:{'User-Agent':'ZT1-Lab-Updater/1'}}, (res) => {
      if ([301,302,303,307,308].includes(res.statusCode) && res.headers.location) {
        res.resume();
        return resolve(request(new URL(res.headers.location, url).toString(), onResponse, redirects + 1));
      }
      if (res.statusCode !== 200) {
        res.resume();
        return reject(new Error(`Update server returned HTTP ${res.statusCode}`));
      }
      Promise.resolve(onResponse(res)).then(resolve, reject);
    });
    req.setTimeout(30000, () => req.destroy(new Error('Update request timed out')));
    req.on('error', reject);
  });
}

function readFrom(base) {
  return request(base + 'ZT1-Lab-update.json', (res) => new Promise((resolve, reject) => {
    const chunks = []; let size = 0;
    res.on('data', (b) => { size += b.length; if (size > 64 * 1024) res.destroy(new Error('Update manifest is too large')); else chunks.push(b); });
    res.on('end', () => { try { resolve(JSON.parse(Buffer.concat(chunks).toString('utf8'))); } catch { reject(new Error('Update manifest is invalid JSON')); } });
    res.on('error', reject);
  }));
}

// The first source whose manifest is valid (and where its installer is)
async function readManifest() {
  let last = new Error('No update source answered');
  for (const base of sources()) {
    try {
      const m = validate(await readFrom(base));
      m.base = base;
      return m;
    } catch (e) {
      last = e;
    }
  }
  throw last;
}

function validate(m) {
  if (!m || m.app !== APP_ID || !/^\d+\.\d+\.\d+$/.test(m.version) ||
      m.file !== `ZT1-Lab-Setup-${m.version}.exe` || !Number.isSafeInteger(m.size) || m.size < 1024 * 1024 || m.size > 2 * 1024 * 1024 * 1024 ||
      !/^[a-f0-9]{128}$/.test(m.sha512) || typeof m.signature !== 'string') throw new Error('Update manifest fields are invalid');
  const message = [m.app, m.version, m.file, String(m.size), m.sha512].join('\n');
  let sig;
  try { sig = Buffer.from(m.signature, 'base64'); } catch { throw new Error('Update signature is malformed'); }
  if (sig.length !== 64 || !crypto.verify(null, Buffer.from(message), PUBLIC_KEY, sig)) throw new Error('Update signature is not trusted');
  return m;
}

function download(m, destination, onProgress = () => {}, onVerifying = () => {}, transport = request) {
  const url = `${sources().includes(m.base) ? m.base : SOURCES[SOURCES.length - 1]}${encodeURIComponent(m.file)}`;
  return transport(url, (res) => new Promise((resolve, reject) => {
    const hash = crypto.createHash('sha512'); let bytes = 0, settled = false, owned = false;
    const started = Date.now(); let lastProgress = 0;
    const out = fs.createWriteStream(destination, {flags:'wx'});
    out.once('open', () => { owned = true; });
    const fail = (err) => {
      if (settled) return;
      settled = true;
      res.unpipe(out); res.destroy();
      const remove = () => owned ? fs.rm(destination, {force:true}, () => reject(err)) : reject(err);
      if (out.closed) remove(); else { out.once('close', remove); out.destroy(); }
    };
    res.on('data', (chunk) => {
      if (settled) return;
      bytes += chunk.length;
      if (bytes > m.size) return fail(new Error('Downloaded installer is larger than its signed size'));
      hash.update(chunk);
      const now = Date.now();
      if (now - lastProgress >= 100 || bytes === m.size) {
        const bytesPerSecond = bytes * 1000 / Math.max(1, now - started);
        onProgress({bytes, total:m.size, bytesPerSecond, etaSeconds:bytesPerSecond > 0 ? (m.size - bytes) / bytesPerSecond : null});
        lastProgress = now;
      }
    });
    res.on('error', fail); out.on('error', fail);
    res.on('aborted', () => fail(new Error('The update download was interrupted')));
    res.on('close', () => { if (!res.complete && !res.readableEnded) fail(new Error('The update download was interrupted')); });
    out.on('finish', () => {
      if (settled) return;
      onVerifying();
      if (bytes !== m.size || hash.digest('hex') !== m.sha512) return fail(new Error('Downloaded installer did not match its signed size and SHA-512'));
      settled = true;
      resolve();
    });
    res.pipe(out);
  }));
}

// Where the manifest is looked for: ZT1_LAB_UPDATE_FEED (a folder URL)
// takes the sources' place, for trying an update out
function sources() {
  const f = process.env.ZT1_LAB_UPDATE_FEED;
  return f ? [f.endsWith('/') ? f : f + '/'] : SOURCES;
}

// A file's size and SHA-512 against the signed manifest's
function matches(file, m) {
  try {
    if (fs.statSync(file).size !== m.size) return false;
    return crypto.createHash('sha512').update(fs.readFileSync(file)).digest('hex') === m.sha512;
  } catch {
    return false;
  }
}

// The Lab's updates, as VoxAI Coder does them: looked for 15 s after start,
// every 30 minutes, when the window comes back to the front (at most every
// 10 minutes), 5 minutes after a look that failed, and on Check updates. A
// newer signed release is downloaded into %LOCALAPPDATA%\zt1-lab-updater\
// pending and its size and SHA-512 checked. A small menu explains each stage.
// Installation is explicit: closing the Lab or dismissing its menu never
// installs an update. The external helper stays visible after the Lab quits.
// ZT1_LAB_UPDATE_TEST=1 looks, verifies and downloads - even in a checkout -
// but never installs.
// (off: why this Lab never updates - a test release - else '')
function controller({ app, send, spawn, launchHelper, log = () => {}, off = '', services = {} }) {
  const TEST = !!process.env.ZT1_LAB_UPDATE_TEST;
  const pendingDir = path.join(process.env.LOCALAPPDATA || app.getPath('temp'), 'zt1-lab-updater', 'pending');
  const st = { state: 'idle', message: '', version: null, file: null, manifest: null, installing: false,
    bytes:0, total:0, bytesPerSecond:0, etaSeconds:null, source:'' };
  const readRelease = services.readManifest || readManifest;
  const fetchRelease = services.download || download;
  const fileMatches = services.matches || matches;
  let timer = null, lastCheck = 0, busy = false;

  const publicState = () => ({ state: st.state, message: st.message, version: st.version, current: app.getVersion(), test: TEST,
    bytes:st.bytes, total:st.total, bytesPerSecond:st.bytesPerSecond, etaSeconds:st.etaSeconds, source:st.source });
  function set(state, message, extra = {}) {
    Object.assign(st, { state, message }, extra);
    log(`[update] ${message}`);
    send(publicState());
  }
  const why = () => off || (TEST || app.isPackaged ? '' : 'Updates come with the installed Lab.');
  function schedule(ms) {
    clearTimeout(timer);
    timer = setTimeout(() => check(false), ms);
  }
  // Installers from earlier updates (this version or older, or half done) go
  function tidy() {
    let names = [];
    try {
      names = fs.readdirSync(pendingDir);
    } catch {
      return;
    }
    for (const n of names) {
      const v = (/^ZT1-Lab-Setup-(\d+\.\d+\.\d+)\.exe$/.exec(n) || [])[1];
      // The running helper and its readiness/ack files share this folder.
      // They are not installer cache entries and may still be open on start.
      if (v && compareVersions(v, app.getVersion()) <= 0) {
        try { fs.rmSync(path.join(pendingDir, n), { force: true }); } catch {}
      }
    }
  }

  async function check(manual) {
    const off = why();
    if (off) {
      set('off', off);
      return publicState();
    }
    if (busy || st.installing) return publicState();
    busy = true;
    lastCheck = Date.now();
    try {
      const kept = { state:st.state, message:st.message, version:st.version, file:st.file };
      set('checking', 'Checking for updates…', {bytes:0,total:0,bytesPerSecond:0,etaSeconds:null});
      const m = validate(await readRelease());
      const current = app.getVersion();
      if (compareVersions(m.version, current) <= 0) {
        set('current', 'All updated.', { version: null, file:null, manifest:null, source:m.base });
      } else if (kept.state === 'ready' && kept.version === m.version && fileMatches(kept.file, m)) {
        set('ready', kept.message, {bytes:m.size,total:m.size,source:m.base});
      } else {
        set('downloading', `Downloading ZT1 Lab ${m.version}…`, { version: m.version, total:m.size, source:m.base, file:null, manifest:null });
        fs.mkdirSync(pendingDir, { recursive: true });
        const file = path.join(pendingDir, m.file);
        if (!fileMatches(file, m)) {
          fs.rmSync(file, { force: true });
          await fetchRelease(m, file, progress => {
            Object.assign(st, progress); send(publicState());
          }, () => set('verifying', 'Verifying the downloaded update…', {bytes:m.size,total:m.size,etaSeconds:null}));
        }
        set('verifying', 'Verifying the downloaded update…', {bytes:m.size,total:m.size,etaSeconds:null});
        if (!fileMatches(file, m)) throw new Error('Downloaded installer did not match its signed size and SHA-512');
        set('ready', `ZT1 Lab ${m.version} is downloaded and checked: restart to update.`, { version: m.version, file, manifest: m });
      }
      schedule(30 * 60 * 1000);
    } catch (e) {
      set('error', `Couldn't update: ${e.message} (trying again in 5 minutes)`);
      schedule(5 * 60 * 1000);
    } finally {
      busy = false;
    }
    return publicState();
  }

  // The ready installer, checked again, run silently (relaunch: the Lab
  // starts again after it)
  async function install(relaunch) {
    if (!relaunch) return { ok:false, message:'Choose Restart to update from the update menu.' };
    if (st.state !== 'ready' || st.installing) return { ok: false, message: 'No update is ready.' };
    if (TEST) return { ok: false, message: `Test mode: ZT1 Lab ${st.version} was downloaded and checked, not installed.` };
    if (!fileMatches(st.file, st.manifest)) {
      fs.rmSync(st.file, { force: true });
      set('error', 'The downloaded update no longer matches its signed size and SHA-512; it was thrown away.');
      schedule(5 * 60 * 1000);
      return { ok: false, message: st.message };
    }
    if (typeof launchHelper !== 'function') return {ok:false,message:'The update helper is unavailable. Reinstall the Lab using its official setup.'};
    st.installing = true;
    set('installing', 'Starting the update window…');
    try {
      const result = await launchHelper({app:'lab',parent:process.pid,installer:st.file,size:st.manifest.size,
        sha512:st.manifest.sha512,launch:process.execPath,version:st.version,pendingDir});
      if (!result || !result.ok) throw new Error(result?.message || 'The update window could not be started.');
      log(`[update] helper ready for ${st.version}; the Lab can close`);
      return {ok:true};
    } catch (e) {
      st.installing = false;
      set('ready', `Update not started: ${e.message}`);
      return {ok:false,message:st.message};
    }
  }

  function start(win) {
    tidy();
    if (why()) return set('off', why());
    set('idle', 'Checks for updates shortly.');
    schedule(15 * 1000);
    win.on('focus', () => {
      if (!busy && st.state !== 'ready' && Date.now() - lastCheck > 10 * 60 * 1000) check(false);
    });
  }

  return { start, check, install, state: publicState, ready: () => st.state === 'ready' && !st.installing && !TEST,
    dispose: () => clearTimeout(timer) };
}

module.exports = { compareVersions, readManifest, validate, download, controller };

