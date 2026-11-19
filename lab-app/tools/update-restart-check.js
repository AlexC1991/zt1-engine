'use strict';
// ours: a local check of the Lab's "Restart to update" path - no release, no
// network. It builds a small Electron app out of the Lab's own electron
// folder, with the REAL update-helper-launch.js and zt1-update-helper.exe,
// and a harmless exe in place of the installer (any exe that exits 0). The
// app starts the helper, quits, and the helper must wait for it to go, run the
// "installer", start the app again with --update-ack/--update-token, and see
// the app's acknowledgement (#107). Prints PASS or FAIL; takes about 30 s.
//
//   node lab-app/tools/update-restart-check.js B:/zt1-shots/restart-check
//
// (the folder, on B:, is made if it isn't there; the electron folder is ~250 MB)
const fs = require('fs');
const path = require('path');
const cp = require('child_process');
const crypto = require('crypto');

const out = path.resolve(process.argv[2] || '');
if (!process.argv[2]) {
  console.log('usage: node update-restart-check.js <folder to build the check in (not on C:)>');
  process.exit(2);
}
const labApp = path.resolve(__dirname, '..');
const electronDist = path.join(labApp, 'node_modules', 'electron', 'dist');
const helper = [
  path.join(labApp, '..', 'build-x64', 'Release', 'zt1-update-helper.exe'),
  path.join(labApp, '..', '..', '..', '..', 'build-x64', 'Release', 'zt1-update-helper.exe'),
].find((f) => fs.existsSync(f));
if (!fs.existsSync(electronDist) || !helper) {
  console.log('FAIL: needs lab-app/node_modules/electron/dist and build-x64/Release/zt1-update-helper.exe');
  process.exit(1);
}
const app = path.join(out, 'app');
const res = path.join(app, 'resources', 'app');
const pending = path.join(out, 'pending');
const logFile = path.join(out, 'harness.log');
fs.mkdirSync(pending, { recursive: true });
if (!fs.existsSync(path.join(app, 'electron.exe'))) fs.cpSync(electronDist, app, { recursive: true });
fs.rmSync(path.join(app, 'resources', 'default_app.asar'), { force: true });
fs.mkdirSync(res, { recursive: true });
fs.copyFileSync(path.join(labApp, 'update-helper-launch.js'), path.join(res, 'update-helper-launch.js'));
fs.writeFileSync(path.join(res, 'package.json'), '{"name":"restartcheck","version":"9.9.9","main":"main.js"}');
// (doskey.exe: a system exe that ignores the installer's switches and exits 0)
const installer = path.join(out, 'fake-installer.exe');
fs.copyFileSync(path.join(process.env.SystemRoot || 'C:\\Windows', 'System32', 'doskey.exe'), installer);
const data = fs.readFileSync(installer);
const cfg = { log: logFile, pending, installer, size: data.length, helper,
  sha512: crypto.createHash('sha512').update(data).digest('hex') };
fs.writeFileSync(path.join(res, 'main.js'), `
const { app } = require('electron');
const fs = require('fs'), cp = require('child_process');
const handoff = require('./update-helper-launch.js');
const C = ${JSON.stringify(cfg)};
const log = (s) => fs.appendFileSync(C.log, new Date().toISOString() + ' pid' + process.pid + ' ' + s + '\\n');
app.disableHardwareAcceleration();
app.whenReady().then(async () => {
  log('start ' + JSON.stringify(process.argv.slice(1)));
  if (process.argv.includes('--update-ack')) {
    log('acknowledge -> ' + handoff.acknowledge(process.argv, '9.9.9', C.pending));
    return setTimeout(() => app.quit(), 3000);
  }
  const r = await handoff.launch({ app: 'lab', parent: process.pid, installer: C.installer, size: C.size,
    sha512: C.sha512, launch: process.execPath, version: '9.9.9', pendingDir: C.pending },
    { candidates: [C.helper], spawn: (f, a, o) => cp.spawn(f, a, o) });
  log('launch -> ' + JSON.stringify(r));
  setTimeout(() => app.quit(), 300);
});
`);
fs.rmSync(logFile, { force: true });
const child = cp.spawn(path.join(app, 'electron.exe'), [], { stdio: 'ignore', detached: true, env: { ...process.env } });
child.unref();
const t0 = Date.now();
const timer = setInterval(() => {
  const text = fs.existsSync(logFile) ? fs.readFileSync(logFile, 'utf8') : '';
  const ok = /launch -> \{"ok":true\}/.test(text) && /will-quit|start \["--update-ack"/.test(text) && /acknowledge -> true/.test(text);
  if (ok || Date.now() - t0 > 90000) {
    clearInterval(timer);
    console.log(text.trim());
    console.log(ok ? 'PASS: the app was started again and acknowledged the update' : 'FAIL: no acknowledgement from the reopened app within 90 s');
    process.exit(ok ? 0 : 1);
  }
}, 1000);
