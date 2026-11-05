'use strict';
// ours: prepare the native hand-off outside the installed files. No shell,
// script execution or silent exit-time installation. The caller quits only
// after the helper has shown its window and acknowledged readiness.
const fs = require('fs');
const path = require('path');
const crypto = require('crypto');

async function launch(request, { candidates, spawn, timeout = 10000, io = fs, timers = globalThis }) {
  if (request.app !== 'lab' || !path.isAbsolute(request.pendingDir) || !path.isAbsolute(request.installer) ||
      !path.isAbsolute(request.launch) || !Number.isSafeInteger(request.size) || request.size <= 0 ||
      !/^[0-9a-f]{128}$/.test(request.sha512) || !/^\d+\.\d+\.\d+$/.test(request.version) || request.parent !== process.pid)
    return { ok:false, message:'The update hand-off parameters are invalid.' };
  const source = candidates.filter(Boolean).find(file => io.existsSync(file));
  if (!source) return { ok:false, message:'The native update helper is missing. Reinstall the Lab using its official setup.' };
  io.mkdirSync(request.pendingDir, {recursive:true});
  const nonce = crypto.randomBytes(16).toString('hex');
  const helper = path.join(request.pendingDir, `zt1-update-helper-${process.pid}-${nonce}.exe`);
  const ready = path.join(request.pendingDir, `helper-ready-${nonce}.txt`);
  io.copyFileSync(source, helper, fs.constants.COPYFILE_EXCL);
  const args = ['--app','lab','--parent',String(request.parent),'--installer',request.installer,
    '--size',String(request.size),'--sha512',request.sha512,'--launch',request.launch,
    '--version',request.version,'--ready-file',ready];
  return new Promise(resolve => {
    let child, interval, deadline, settled = false;
    function finish(ok, message) {
      if (settled) return;
      settled = true;
      timers.clearInterval(interval); timers.clearTimeout(deadline);
      try { io.rmSync(ready, {force:true}); } catch {}
      if (!ok && child) { try { child.kill(); } catch {} }
      if (ok) child.unref();
      resolve({ok, ...(message?{message}:{})});
    }
    try {
      child = spawn(helper, args, {cwd:request.pendingDir, detached:true, stdio:'ignore', windowsHide:true});
      child.once('error', e => finish(false, `The updater window could not start: ${e.message}`));
      child.once('exit', () => finish(false, 'The updater closed before it was ready. The Lab remains open.'));
      interval = timers.setInterval(() => {
        try { if (io.readFileSync(ready, 'utf8') === 'ready') finish(true); } catch {}
      }, 100);
      deadline = timers.setTimeout(() => finish(false, 'The updater window did not become ready. The Lab remains open.'), timeout);
    } catch (e) { finish(false, `The updater window could not start: ${e.message}`); }
  });
}

// The helper creates only launch-<nonce>.ack in our pending directory. An
// arbitrary command-line path must never become a general-purpose file writer.
function acknowledge(argv, version, pendingDir, io = fs) {
  const at = argv.indexOf('--update-ack'), nonceAt = argv.indexOf('--update-token');
  if (at < 0 || nonceAt < 0) return false;
  const file = argv[at+1], nonce = argv[nonceAt+1];
  if (typeof file !== 'string' || !path.isAbsolute(file) || !/^[0-9a-f]{32}$/.test(nonce || '')) return false;
  try {
    const root = io.realpathSync(pendingDir), parent = io.realpathSync(path.dirname(file));
    if (root.toLowerCase() !== parent.toLowerCase() || path.basename(file) !== `launch-${nonce}.ack`) return false;
    const temp = file + '.tmp';
    io.writeFileSync(temp, `${nonce}\n${version}\n`, {encoding:'utf8',flag:'wx'});
    io.renameSync(temp, file);
    return true;
  } catch { return false; }
}
module.exports = {launch, acknowledge};
