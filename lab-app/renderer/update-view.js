// ours: the small updater menu uses the Lab's existing theme and button styles.
// Keep formatting separate from DOM updates so stream progress and restart
// deferral can be checked without opening the shared Lab window.
(function(root) {
  'use strict';
  const escape = value => String(value ?? '').replace(/[&<>"']/g, c => ({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
  const bytes = n => !Number.isFinite(n) || n < 0 ? '—' : n < 1000000 ? `${(n / 1000).toFixed(0)} KB` : `${(n / 1000000).toFixed(1).replace(/\.0$/, '')} MB`;
  const eta = n => !Number.isFinite(n) || n < 0 ? 'Estimating…' : n >= 60 ? `~${Math.ceil(n / 60)} min left` : `~${Math.max(1, Math.ceil(n))} sec left`;
  function busyReason(s) {
    const st = s?.engine?.state;
    // A paused HD supervisor can still own a running external worker. Its
    // script remains live until the worker exits, irrespective of auto-play.
    if (st?.live && /hd[_ -]?(?:art|batch)|hd.*batch/i.test(st.script || '')) return 'HD generation is running.';
    if (s?.play?.running || s?.play?.starting) return 'Close the game in Play before restarting the Lab.';
    if (st?.build === 'building' || s?.eng?.build?.state === 'building' || Object.values(s?.builds || {}).some(b => b.state === 'building')) return 'A build is running.';
    if (s?.editor?.stem && s.editor.text !== s.editor.saved) return 'Save your Lab script changes before restarting.';
    return '';
  }
  function menu(st, {queued = false, reason = '', details = false} = {}) {
    const state = st.state || 'idle';
    const titles = {idle:'Updates',checking:'Checking for updates…',downloading:'Downloading',verifying:'Verifying update…',ready:'Ready to update',current:'All updated',off:'Updates unavailable',error:'Update interrupted',installing:'Opening update window…'};
    const title = queued ? 'Restart queued' : titles[state] || 'Updates';
    const pct = state === 'downloading' && st.total > 0 ? Math.max(0, Math.min(100, Math.floor((st.bytes || 0) * 100 / st.total))) : null;
    const working = ['checking','downloading','verifying','installing'].includes(state);
    const progress = pct === null ? '' : `<div class="lab-update-progress" role="progressbar" aria-label="Update download" aria-valuenow="${pct}" aria-valuemin="0" aria-valuemax="100"><span style="width:${pct}%"></span></div><div class="lab-update-metrics"><span>${bytes(st.bytes)} / ${bytes(st.total)}</span><span>${eta(st.etaSeconds)}</span></div>`;
    const message = queued ? `The Lab stays open. ${reason || 'Waiting for active work to finish.'}` : state === 'ready' ? reason || 'Download verified. Restart when you’re ready.' : ['error','off','current','verifying','installing'].includes(state) ? st.message : '';
    const buttons = state === 'ready' ? queued ? '<button type="button" class="btn ghost small" data-act="cancelLabUpdateRestart">Cancel restart</button>' : `<button type="button" class="btn primary small" data-act="restartLabUpdate">${reason ? 'Restart when finished' : 'Restart to update'}</button>` : state === 'off' ? '' : `<button type="button" class="btn ghost small" data-act="runLabUpdateCheck" ${working?'disabled':''}>${state === 'error'?'Try again':'Check now'}</button>`;
    let source = '';
    try { source = new URL(st.source).hostname; } catch {}
    return `<div class="lab-update-heading"><span id="lab-update-title" role="status">${escape(title)}</span><span class="lab-update-heading-right">${pct === null?'':`<span>${pct}%</span>`}<button type="button" class="lab-update-close" data-act="closeLabUpdate" aria-label="Close update details">×</button></span></div>${progress}${message?`<p class="lab-update-message${state==='error'?' error':''}">${escape(message)}</p>`:''}<details${details?' open':''}><summary>Details</summary><p>Installed: ${escape(st.current || 'unknown')}${st.version?`<br>Available: ${escape(st.version)}`:''}${source?`<br>Source: ${escape(source)}`:''}</p>${state==='downloading'&&st.bytesPerSecond>0?`<p>${bytes(st.bytesPerSecond)}/s</p>`:''}${st.test?'<p>Update test mode: no installation will run.</p>':''}<p>${escape(st.message || 'Updates are checked before installation.')}</p></details>${buttons?`<div class="lab-update-actions">${buttons}</div>`:''}`;
  }
  const api = { bytes, eta, busyReason, menu };
  if (typeof module !== 'undefined' && module.exports) module.exports = api;
  else root.LabUpdateView = api;
})(globalThis);
