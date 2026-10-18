// Experiments > Your mods: making mods properly and sharing them (main.js
// "Your mods"; the guide is docs/MODDING.md). The mods in the game's mods
// folder down the left - a new one from a template, or one brought in (an old
// Zoo Tycoon .ztd, a .zip, a mod's folder); the picked one's details, switched
// on or off, checked against the rules the mod repository checks with, and
// uploaded; on the right, the mods you've uploaded (updated by uploading
// again, or taken down), signed in with your CodeRook account.

const MK = { mods: [], folder: '', sel: null, check: null, log: [], mine: null, signedIn: false, busy: false, newOpen: false };

function modkitTabs() {
  const tab = (k, label) => `<div class="${S.modsLeft === k ? 'on' : ''}" data-act="modsLeft" data-arg="${k}">${label}</div>`;
  return `<div class="seg mini lefttabs">${tab('mods', 'Your experiments')}${tab('gamemods', 'Your mods')}${tab('game', 'Game files')}</div>`;
}

async function modkitLoad() {
  const r = await lab.gameModsList();
  MK.folder = r.folder;
  MK.mods = (r.mods || []).filter((m) => !m.folder.startsWith('.'));
  if (!MK.sel || !MK.mods.some((m) => m.folder === MK.sel)) MK.sel = MK.mods.length ? MK.mods[0].folder : null;
  MK.signedIn = (await lab.modkitToken()).signedIn;
}

async function renderModKit() {
  await modkitLoad();
  const m = MK.mods.find((x) => x.folder === MK.sel);
  const field = (k, label, about, long) =>
    `<div style="margin-bottom:10px"><div style="font-size:13px;margin-bottom:3px">${label}</div>${
      long
        ? `<textarea class="mkfield" data-k="${k}" rows="3" style="width:100%;box-sizing:border-box">${esc((m && m[k]) || '')}</textarea>`
        : `<input class="mkfield" data-k="${k}" value="${esc((m && m[k]) || '')}" style="width:100%;box-sizing:border-box"/>`
    }<div class="muted" style="font-size:11px;margin-top:2px">${about}</div></div>`;
  const check = MK.check && MK.check.folder === MK.sel ? MK.check : null;
  $('v-mods').innerHTML = `<div class="modlist" style="width:260px">${modkitTabs()}
      ${MK.mods
        .map(
          (x) => `<div class="item${x.folder === MK.sel ? ' sel' : ''}" data-act="mkPick" data-arg="${esc(x.folder)}"><div class="name">${esc(x.name || x.folder)}</div><div class="sub ${x.enabled ? 'green' : 'dim'}">● ${
            x.enabled ? 'on' : 'off'
          }${x.version ? ' · ' + esc(x.version) : ''}${x.imported ? ' · brought in' : ''}</div></div>`,
        )
        .join('')}
      ${
        MK.newOpen
          ? `<div style="padding:8px 12px"><input id="mk-newname" placeholder="Its name" style="width:100%;box-sizing:border-box;margin-bottom:6px"/><select id="mk-template" style="width:100%;margin-bottom:6px"><option value="ztd">Game files (a .ztd: new or changed animals, objects...)</option><option value="shader">A shader (a look for the art)</option><option value="empty">Empty</option></select><div class="btn small primary" data-act="mkCreate">Make it</div> <div class="btn small ghost" data-act="mkNew">Cancel</div></div>`
          : `<div class="item muted" data-act="mkNew">+ New mod</div>`
      }
      <div class="item muted" data-act="mkImport" title="An old Zoo Tycoon download (.ztd), a .zip, or a mod's mod.json - made a proper mod">+ Bring in a mod (.ztd, .zip, folder)</div>
      <div class="item muted" data-act="mkOpen" data-arg="" title="${esc(MK.folder)}">Open the mods folder</div>
    </div>
    <div style="flex:1;overflow:auto;padding:16px 22px;min-width:0">${
      m
        ? `<div class="label" style="margin-bottom:10px">${esc(m.name || m.folder)} <span class="dim">(${esc(m.folder)})</span></div>
      ${field('name', 'Name', 'What the Mod Menu calls it.')}
      ${field('id', 'Id', 'Its id in the mod repository: lower-case letters, numbers, . _ - (made from the name if left empty). Yours alone once uploaded.')}
      ${field('author', 'Author', 'Who made it (and who it\'s credited to).')}
      ${field('version', 'Version', 'e.g. 1.0 - raise it when you change it, so players see there\'s an update.')}
      ${field('about', 'About', 'What it does, in a sentence or two.', true)}
      <div style="display:flex;gap:8px;flex-wrap:wrap;margin:12px 0">
        <div class="btn primary${MK.busy ? ' disabled' : ''}" data-act="mkSave">Save</div>
        <div class="btn${MK.busy ? ' disabled' : ''}" data-act="mkToggle">${m.enabled ? 'Switch off' : 'Switch on'}</div>
        <div class="btn${MK.busy ? ' disabled' : ''}" data-act="mkCheck" title="The rules the mod repository's own checks use">Check it</div>
        <div class="btn${MK.busy ? ' disabled' : ''}" data-act="mkUpload" title="To the mod repository: checked there, then in every official game's Mod Menu">Upload to the mod repository</div>
        <div class="btn ghost" data-act="mkOpen" data-arg="${esc(m.folder)}">Open its folder</div>
      </div>
      <div class="muted" style="font-size:12px;margin-bottom:8px">Switched on, it's in the Test Zoo and in Play when they next start (the Mod Menu's Restart, or Play's Restart the game).</div>
      ${
        check
          ? `<div class="code" style="font-size:12px;white-space:pre-wrap">${
              check.ok ? '<span class="green">✓ It follows the rules</span>' : '<span class="red">✗ It can\'t be uploaded yet</span>'
            } · ${check.files || 0} files, ${((check.bytes || 0) / 1048576).toFixed(1)} MB${check.errors.map((e) => '\n<span class="red">✗</span> ' + esc(e)).join('')}${check.warnings
              .map((w) => '\n<span class="dim">!</span> ' + esc(w))
              .join('')}</div>`
          : ''
      }
      <div class="code" id="mk-log" style="font-size:12px;white-space:pre-wrap;margin-top:8px;${MK.log.length ? '' : 'display:none'}">${esc(MK.log.join('\n'))}</div>`
        : `<div class="stagemsg"><b>No mods yet</b><span>A mod is a folder in the game's mods folder: a mod.json saying what it is, and its files - a .ztd of game files, shaders, or HD art. Make one from a template, or bring in an old Zoo Tycoon download.</span><div class="btn primary" data-act="mkNew">New mod</div></div>`
    }</div>
    <div class="rightcol"><div class="section"><div class="label">Your uploads</div>${
      MK.signedIn
        ? `<div class="muted" style="font-size:12px;margin-bottom:6px">Signed in to CodeRook. <span class="blue clickable" data-act="mkSignOut">Sign out</span> · <span class="blue clickable" data-act="mkMine">Refresh</span></div>${
            MK.mine === null
              ? '<div class="muted" style="font-size:12px">…</div>'
              : MK.mine.ok === false
              ? `<div class="muted" style="font-size:12px">${esc(MK.mine.message)}</div>`
              : MK.mine.mods.length
              ? MK.mine.mods
                  .map(
                    (u) =>
                      `<div class="willbox"><b>${esc(u.name || u.id)}</b> <span class="dim">${esc(u.version || '')}</span><div class="d">${esc(
                        u.status || (u.live ? 'In the mod repository' : 'Being checked'),
                      )}${u.reason ? ': ' + esc(u.reason) : ''}</div><span class="blue clickable" data-act="mkTakedown" data-arg="${esc(u.id)}">Take it down</span></div>`,
                  )
                  .join('')
              : '<div class="muted" style="font-size:12px">Nothing uploaded yet. Upload one of your mods: the same again updates it.</div>'
          }`
        : `<div class="muted" style="font-size:12px;margin-bottom:6px">Uploading needs a CodeRook account (coderook.com). Sign in with <span class="mono">cbx sign-in</span>, or paste a personal access token:</div><input id="mk-token" type="password" placeholder="CodeRook token" style="width:100%;box-sizing:border-box;margin-bottom:6px"/><div class="btn small primary" data-act="mkSignIn">Sign in</div>`
    }</div>
    <div class="section"><div class="label">Making a mod properly</div><div class="muted" style="font-size:12px;line-height:1.5">
      <b>Game files:</b> new or changed animals, objects, scenery go in a .ztd (a zip) with the game's own paths inside. The Game files tab shows every path; its animation maker makes a recoloured animal as a mod.<br>
      <b>Shaders:</b> a shaders/ folder (mods/zt1-toon is the example).<br>
      <b>HD art:</b> the HD art experiment (Your experiments) makes one.<br>
      <b>Only game data:</b> no programs or scripts - the mod repository turns those down.<br>
      <b>Old downloads:</b> bring them in here or in the game (Mod Menu › Add a mod...): they work as they did.<br>
      The whole guide: <span class="mono">docs/MODDING.md</span></div></div></div>`;
  document.querySelectorAll('.mkfield').forEach((el) => (el.oninput = () => (MK.dirty = true)));
  // (a .ztd, .zip or folder dropped on the page: brought in)
  const page = $('v-mods');
  page.ondragover = (ev) => ev.preventDefault();
  page.ondrop = (ev) => {
    ev.preventDefault();
    const f = ev.dataTransfer && ev.dataTransfer.files && ev.dataTransfer.files[0];
    const p = f && (lab.pathForFile ? lab.pathForFile(f) : f.path);
    if (p) ACTIONS.mkImport(p);
  };
  if (MK.signedIn && MK.mine === null) ACTIONS.mkMine();
}

Object.assign(ACTIONS, {
  mkPick: (folder) => {
    MK.sel = folder;
    MK.log = [];
    renderModKit();
  },
  mkNew: () => {
    MK.newOpen = !MK.newOpen;
    renderModKit().then(() => $('mk-newname') && $('mk-newname').focus());
  },
  mkCreate: async () => {
    const name = ($('mk-newname') || {}).value || '';
    const template = ($('mk-template') || {}).value || 'empty';
    const r = await lab.modkitNew({ name, template });
    MK.newOpen = false;
    if (r.ok) {
      MK.sel = r.folder;
      MK.log = [];
    }
    renderModKit();
  },
  // (a path given - dropped on the list, or from the Lab's MCP - else the picker)
  mkImport: async (file) => {
    const r = await lab.modkitImport(typeof file === 'string' && file ? file : undefined);
    if (r.cancelled) return;
    toast(esc(r.message || (r.ok ? 'Brought in.' : 'Not brought in.')));
    if (r.ok) {
      MK.sel = r.folder;
      MK.log = [];
    }
    renderModKit();
  },
  mkOpen: (folder) => lab.modkitOpen(folder || ''),
  mkSave: async () => {
    const fields = {};
    document.querySelectorAll('.mkfield').forEach((el) => (fields[el.dataset.k] = el.value));
    await lab.modkitSave(MK.sel, fields);
    MK.dirty = false;
    toast('Saved.');
    renderModKit();
  },
  mkToggle: async () => {
    const m = MK.mods.find((x) => x.folder === MK.sel);
    if (m) await lab.gameModsEnable(m.folder, !m.enabled);
    renderModKit();
  },
  mkCheck: async () => {
    if (MK.dirty) await ACTIONS.mkSave();
    MK.check = { folder: MK.sel, ...(await lab.modkitCheck(MK.sel)) };
    renderModKit();
  },
  mkUpload: async () => {
    if (MK.busy) return;
    if (MK.dirty) await ACTIONS.mkSave();
    MK.busy = true;
    MK.log = ['Uploading...'];
    renderModKit();
    const r = await lab.modkitUpload(MK.sel);
    MK.busy = false;
    if (r.check) MK.check = { folder: MK.sel, ...r.check };
    MK.log.push(r.message || (r.ok ? 'Uploaded.' : 'Not uploaded.'));
    MK.mine = null;
    renderModKit();
  },
  mkSignIn: async () => {
    const t = ($('mk-token') || {}).value || '';
    if (!t.trim()) return toast('Paste your CodeRook token first.');
    await lab.modkitToken(t);
    MK.mine = null;
    renderModKit();
  },
  mkSignOut: async () => {
    await lab.modkitToken('');
    MK.mine = null;
    renderModKit();
  },
  mkMine: async () => {
    MK.mine = await lab.modkitMine();
    if (S.tab === 'mods' && S.modsLeft === 'gamemods') renderModKit();
  },
  // ("<id>!": already confirmed - the Lab's MCP)
  mkTakedown: async (arg) => {
    const sure = String(arg).endsWith('!');
    const id = sure ? String(arg).slice(0, -1) : arg;
    if (!sure && !confirm(`Take ${id} down from the mod repository? Players won't be offered it any more.`)) return;
    const r = await lab.modkitTakedown(id);
    toast(esc(r.message));
    MK.mine = null;
    renderModKit();
  },
});

lab.onModkit((s) => {
  MK.log.push(s.line);
  const el = $('mk-log');
  if (el) {
    el.style.display = '';
    el.textContent = MK.log.join('\n');
  }
});
