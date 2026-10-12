// The Lab's Sync: the engine's changes to GitHub (the home) and CodeRook
// (a mirror of main), from Engine code's Sync tab instead of by hand.
//
//   Get latest   git fetch, then main merged into this branch; any conflicts
//                listed, each taken as mine or theirs, or fixed in the editor
//   Commit       everything changed (what .gitignore leaves out stays out)
//   Push         to this branch on GitHub - or, without push rights there,
//                to the person's own fork
//   Propose      a pull request into main, always on this repo (it's a fork
//                of sharkwouter's, and gh would aim at that one otherwise)
//   Mirror       (owner only) main as it is on GitHub, sent to CodeRook with
//                cbx, from a worktree of origin/main in .cache/mirror-main
//
// Nothing is ever force-pushed. Every command is a program and its
// arguments, no shell.
const cp = require('child_process');
const fs = require('fs');
const path = require('path');

const HOME_REPO = 'AlexC1991/zt1-engine'; // where contributions land
const BASE = 'main';
const MIRROR_PROJECT = 'zt1-engine'; // on CodeRook

function run(cmd, args, cwd, timeout = 120000) {
  return new Promise((resolve) => {
    cp.execFile(cmd, args, { cwd, timeout, windowsHide: true, maxBuffer: 32 * 1024 * 1024 }, (err, stdout, stderr) => {
      // (only the end trimmed: git status's first column can be a space)
      resolve({ ok: !err, code: err ? (typeof err.code === 'number' ? err.code : 1) : 0, out: String(stdout || '').trimEnd(), err: String(stderr || (err && !stderr ? err.message : '') || '').trim() });
    });
  });
}

// cbx: on Windows npm's cbx.cmd, which Node won't start without a shell
// (and a shell would need every commit message quoted): its script, run by
// node, instead
function cbxCommand() {
  if (process.platform !== 'win32') return ['cbx', []];
  const npm = path.join(process.env.APPDATA || '', 'npm');
  const js = path.join(npm, 'node_modules', '@coderook', 'cli', 'dist', 'cli', 'src', 'cli.js');
  if (fs.existsSync(js)) return ['node', [js]];
  return ['cbx', []];
}

module.exports = function makeSync(REPO, say) {
  const git = (...a) => run('git', a, REPO);
  const gh = (...a) => run('gh', a, REPO, 60000);
  const [CBX, CBX_PRE] = cbxCommand();
  const cbx = (args, cwd = REPO, t = 600000) => run(CBX, [...CBX_PRE, ...args], cwd, t);
  const MIRROR_DIR = path.join(REPO, '.cache', 'mirror-main');
  const log = (s) => say && say(s);

  // Who's running it, on GitHub: admin, write, or read (fork and propose)
  let who = null;
  async function whoami() {
    if (who) return who;
    const u = await gh('api', 'user', '--jq', '.login');
    const p = await gh('api', `repos/${HOME_REPO}`, '--jq', '.permissions');
    let perm = {};
    try {
      perm = JSON.parse(p.out || '{}');
    } catch {}
    who = {
      gh: u.ok ? u.out : null,
      ghError: u.ok ? '' : u.err || 'gh isn’t signed in (gh auth login)',
      role: perm.admin ? 'admin' : perm.maintain ? 'maintain' : perm.push ? 'write' : u.ok ? 'read' : 'none',
    };
    return who;
  }

  async function status({ fetch = false } = {}) {
    if (fetch) await git('fetch', 'origin', '--quiet');
    const [br, st, conf, ab, up, me] = await Promise.all([
      git('rev-parse', '--abbrev-ref', 'HEAD'),
      git('status', '--porcelain=v1', '--untracked-files=all'),
      git('diff', '--name-only', '--diff-filter=U'),
      git('rev-list', '--left-right', '--count', `origin/${BASE}...HEAD`),
      git('rev-list', '--left-right', '--count', '@{u}...HEAD'),
      whoami(),
    ]);
    const files = (st.out ? st.out.split('\n') : []).map((l) => ({ x: l[0], y: l[1], path: l.slice(3).replace(/^"|"$/g, '') }));
    const [behind, ahead] = (ab.out || '0 0').split(/\s+/).map(Number);
    const [upBehind, upAhead] = up.ok ? up.out.split(/\s+/).map(Number) : [null, null];
    const branch = br.out;
    let pr = null;
    if (me.gh && branch !== BASE) {
      const r = await gh('pr', 'list', '--repo', HOME_REPO, '--head', branch, '--state', 'open', '--json', 'number,url,title', '--limit', '1');
      try {
        pr = JSON.parse(r.out || '[]')[0] || null;
      } catch {}
    }
    const cb = await cbx(['whoami'], REPO, 20000);
    return {
      branch,
      base: BASE,
      home: HOME_REPO,
      behind,
      ahead,
      upstream: up.ok,
      upBehind,
      upAhead,
      merging: fs.existsSync(path.join(REPO, '.git', 'MERGE_HEAD')),
      conflicts: conf.out ? conf.out.split('\n') : [],
      files,
      who: me,
      pr,
      coderook: { signedIn: cb.ok && !/not signed/i.test(cb.out), as: cb.ok ? cb.out.replace(/\u001b\[[0-9;]*m/g, '') : '', mirrorLinked: fs.existsSync(MIRROR_DIR), project: MIRROR_PROJECT },
    };
  }

  // Main merged into this branch (a merge, not a rebase: nothing rewritten)
  async function pull() {
    const f = await git('fetch', 'origin');
    if (!f.ok) return { ok: false, error: f.err };
    const m = await git('merge', '--no-edit', `origin/${BASE}`);
    const conf = await git('diff', '--name-only', '--diff-filter=U');
    const conflicts = conf.out ? conf.out.split('\n') : [];
    // (this branch's own upstream too, if it has one: a helper's pushes)
    if (!conflicts.length) {
      const up = await git('rev-parse', '--abbrev-ref', '@{u}');
      if (up.ok && up.out !== `origin/${BASE}`) await git('merge', '--no-edit', up.out);
    }
    const conf2 = await git('diff', '--name-only', '--diff-filter=U');
    return { ok: m.ok || conflicts.length > 0, conflicts: conf2.out ? conf2.out.split('\n') : [], out: (m.out + '\n' + m.err).trim() };
  }

  async function resolve(file, how) {
    if (how === 'ours' || how === 'theirs') {
      const c = await git('checkout', `--${how}`, '--', file);
      if (!c.ok) return { ok: false, error: c.err };
    } else {
      // (fixed by hand: no conflict markers may be left)
      const text = fs.readFileSync(path.join(REPO, file), 'utf8');
      if (/^(<{7}|={7}|>{7})( |$)/m.test(text)) return { ok: false, error: `${file} still has conflict markers (<<<<<<< ======= >>>>>>>) in it.` };
    }
    const a = await git('add', '--', file);
    return { ok: a.ok, error: a.err };
  }

  async function abortMerge() {
    const r = await git('merge', '--abort');
    return { ok: r.ok, error: r.err };
  }

  async function commit(message) {
    if (!message || !message.trim()) return { ok: false, error: 'A commit needs a message.' };
    const conf = await git('diff', '--name-only', '--diff-filter=U');
    if (conf.out) return { ok: false, error: 'Settle the conflicts first: ' + conf.out.split('\n').join(', ') };
    await git('add', '-A');
    const r = await git('commit', '-m', message);
    if (!r.ok && /nothing to commit/i.test(r.out + r.err)) return { ok: true, nothing: true };
    return { ok: r.ok, out: r.out, error: r.ok ? '' : r.err || r.out };
  }

  // To GitHub: this branch on the home repo with push rights, else on the
  // person's fork (made the first time)
  async function push() {
    const me = await whoami();
    const branch = (await git('rev-parse', '--abbrev-ref', 'HEAD')).out;
    if (branch === BASE && me.role !== 'admin' && me.role !== 'maintain')
      return { ok: false, error: `Changes go into ${BASE} through a pull request: start a branch of your own first (Sync › New branch).` };
    let remote = 'origin';
    if (me.role === 'read' || me.role === 'none') {
      if (!me.gh) return { ok: false, error: me.ghError };
      const has = await git('remote', 'get-url', 'fork');
      if (!has.ok) {
        log('Making your fork on GitHub…');
        const f = await gh('repo', 'fork', HOME_REPO, '--remote', '--remote-name', 'fork', '--clone=false');
        if (!f.ok) return { ok: false, error: f.err };
      }
      remote = 'fork';
    }
    log(`Pushing ${branch} to ${remote}…`);
    const r = await run('git', ['push', '-u', remote, branch], REPO, 600000);
    return { ok: r.ok, remote, branch, out: (r.out + '\n' + r.err).trim(), error: r.ok ? '' : r.err };
  }

  async function propose(title, body) {
    const me = await whoami();
    if (!me.gh) return { ok: false, error: me.ghError };
    const branch = (await git('rev-parse', '--abbrev-ref', 'HEAD')).out;
    if (branch === BASE) return { ok: false, error: `You're on ${BASE}: a proposal comes from a branch.` };
    const head = me.role === 'read' || me.role === 'none' ? `${me.gh}:${branch}` : branch;
    const r = await gh('pr', 'create', '--repo', HOME_REPO, '--base', BASE, '--head', head, '--title', title || branch, '--body', body || '');
    const url = (r.out.match(/https:\/\/github\.com\/\S+/) || [])[0] || '';
    return { ok: r.ok, url, error: r.ok ? '' : r.err };
  }

  async function newBranch(name) {
    if (!/^[A-Za-z0-9._\/-]{1,80}$/.test(name || '')) return { ok: false, error: 'A branch name: letters, numbers, - _ . /' };
    const r = await git('switch', '-c', name);
    return { ok: r.ok, error: r.err };
  }

  // Main, as it is on GitHub, to CodeRook (the owner's job: after a merge)
  async function mirror() {
    const me = await whoami();
    if (me.role !== 'admin') return { ok: false, error: 'Only the owner mirrors main to CodeRook.' };
    await git('fetch', 'origin', BASE);
    let first = false;
    if (!fs.existsSync(MIRROR_DIR)) {
      log('Setting up the mirror’s copy of main (.cache/mirror-main)…');
      const w = await git('worktree', 'add', '--detach', MIRROR_DIR, `origin/${BASE}`);
      if (!w.ok) return { ok: false, error: w.err };
      first = !fs.existsSync(path.join(MIRROR_DIR, '.cbx'));
    } else {
      const c = await run('git', ['checkout', '--detach', '--force', `origin/${BASE}`], MIRROR_DIR);
      if (!c.ok) return { ok: false, error: c.err };
    }
    const subject = (await git('log', '-1', '--format=%h %s', `origin/${BASE}`)).out;
    log(`Sending ${BASE} (${subject}) to CodeRook…`);
    const args = ['submit', MIRROR_DIR, '-m', `${BASE}: ${subject}`, '--sync'];
    if (first) args.push('--new', '--name', MIRROR_PROJECT, '--no-licence');
    const r = await cbx(args, REPO);
    let vis = null;
    if (r.ok && first) vis = await cbx(['visibility', 'public', MIRROR_PROJECT, '--yes'], REPO, 60000);
    const clean = (s) => String(s || '').replace(/\u001b\[[0-9;]*m/g, '');
    return { ok: r.ok, out: clean(r.out + '\n' + r.err).trim(), visibility: vis ? clean(vis.out || vis.err) : '', error: r.ok ? '' : clean(r.err || r.out) };
  }

  return { status, pull, resolve, abortMerge, commit, push, propose, newBranch, mirror, forget: () => (who = null) };
};
