// Sharing: the header's control for the shared knowledge base (re/cloud).
// On, what this Lab works out about zoo.exe goes up and what other Labs
// found comes down, in the background (main.js shareTick: at start, then
// every half hour, sending only what changed). Off by default - the
// player's choice. What goes and what never does: re/cloud/README.md.

const SHARE = { state: null };

// (an ISO time as "18 Oct, 00:34", or "never")
function shareWhen(iso) {
  if (!iso) return 'never';
  const d = new Date(iso);
  return isNaN(d) ? 'never' : d.toLocaleString(undefined, { day: 'numeric', month: 'short', hour: '2-digit', minute: '2-digit' });
}

function renderSharePill() {
  const p = $('share-pill');
  if (!p) return;
  const s = SHARE.state || {};
  const label = !s.api ? 'Sharing unavailable' : s.busy === 'sending' ? 'Sharing: sending…' : s.busy === 'pulling' ? 'Sharing: receiving…' : s.on ? 'Sharing on' : 'Sharing off';
  p.className = 'pill clickable ' + (!s.api ? 'bad' : s.on ? 'ok' : '');
  p.querySelector('span').textContent = label;
  const failed = (s.lastSend && !s.lastSend.ok) || (s.lastPull && !s.lastPull.ok);
  p.title = s.on
    ? `Sharing what this Lab finds about zoo.exe with every Lab. Last sent ${shareWhen(s.lastSend && s.lastSend.at)}, last received ${shareWhen(s.lastPull && s.lastPull.at)}${failed ? ' - the last try failed (click for details)' : ''}`
    : 'Click to share what this Lab finds about zoo.exe with every Lab, and receive theirs';
  if (document.getElementById('share-dialog')) renderShareDialog();
}

function renderShareDialog() {
  const box = document.getElementById('share-dialog');
  if (!box) return;
  const s = SHARE.state || {};
  const line = (what, r) =>
    `<div class="row"><span class="muted">${what}</span> ${esc(shareWhen(r && r.at))}${r && r.said ? ` <span class="${r.ok ? 'muted' : 'bad-text'}">- ${esc(r.said)}</span>` : ''}</div>`;
  box.innerHTML = `<div class="h">Sharing</div>
    <div>Every Lab works out the same things about the original <code>zoo.exe</code>: what a function does, a struct's fields, which code ran when. With sharing on, what this Lab finds goes up to the shared knowledge base and what other Labs found comes down into yours, in the background.</div>
    <div class="muted">Shared: findings (names, summaries, fields, constants) and which functions ran in which situation. Never shared: your recordings, your zoos, anything in <code>re/private</code>, or who you are. What arrives stays marked as unreviewed until it's checked against the game.</div>
    <div class="section">${line('Last sent', s.lastSend)}${line('Last received', s.lastPull)}${s.busy ? `<div class="row">Now ${esc(s.busy)}…</div>` : ''}</div>
    <div class="muted">${s.api ? `Shared base: ${esc(s.api)}` : 'No shared base is configured (re/cloud/config.json).'}</div>
    <div class="btns">
      <div class="btn${s.on && !s.busy && s.api ? '' : ' disabled'}" data-x="now" title="Send and receive now">Share now</div>
      <div class="btn${s.api ? '' : ' disabled'}" data-x="toggle">${s.on ? 'Turn sharing off' : 'Turn sharing on'}</div>
      <div class="btn primary" data-x="close">Close</div>
    </div>`;
}

Object.assign(ACTIONS, {
  shareOpen: () => {
    const m = $('modal');
    m.innerHTML = '<div class="dialog" id="share-dialog" style="width:520px"></div>';
    renderShareDialog();
    m.classList.remove('hidden');
    sendPlayRect();
    const close = () => {
      m.classList.add('hidden');
      m.innerHTML = '';
      sendPlayRect();
    };
    m.onclick = async (e) => {
      const x = e.target.dataset.x;
      if (e.target === m || x === 'close') return close();
      if (e.target.classList.contains('disabled')) return;
      if (x === 'toggle') SHARE.state = await lab.shareSet(!(SHARE.state && SHARE.state.on));
      if (x === 'now') SHARE.state = await lab.shareNow();
      renderSharePill();
    };
    m.onkeydown = (e) => e.key === 'Escape' && close();
  },
});

lab.onShare((s) => {
  SHARE.state = s;
  renderSharePill();
});
lab.shareGet().then((s) => {
  SHARE.state = s;
  renderSharePill();
});
