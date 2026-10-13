// The shared knowledge base: what every install of the ZT1 Lab has worked out
// about the original's zoo.exe.
//
// Every fact here is about an ADDRESS, and an address only means something for
// one build of the game - so every row carries that build's SHA-256, and a
// contribution from any other build is refused outright. That is the same rule
// the Ghidra scripts already enforce (re/ghidra_scripts/zt1re.py): other
// builds' addresses don't line up.
//
// A contribution is never the answer on arrival: it lands 'provisional' and is
// only settled when it is reviewed. Nothing is deleted - a refused finding is
// kept and marked.
//
//   GET  /                     what this is, and the numbers
//   GET  /pull?sha=<64 hex>    the shared base for that build
//   GET  /pending?sha=...      what is waiting to be reviewed (needs the token)
//   POST /contribute           { sha, bytes?, by?, client?, entries[], seen[] }
//   POST /review               { take:[{kind,key}], refuse:[{kind,key}] } (token)

const THE_BUILD = '61fcc6d18bdd9339b8ce1ed00ed5a9f1bffca00781266396826f6aa2d00124d5';
const THE_BYTES = 2449432;
const THE_LABEL = '2002 Complete Collection';
const KINDS = new Set(['function', 'field', 'global', 'class', 'slot', 'struct', 'const', 'note']);
const CONFIDENCE = new Set(['sure', 'likely', 'guess']);
const MAX_ENTRIES = 500;
const MAX_SEEN = 2000;
const MAX_PULL = 10000;

const json = (body, status = 200) =>
  new Response(JSON.stringify(body, null, 1) + '\n', {
    status,
    headers: { 'content-type': 'application/json; charset=utf-8' },
  });
const fail = (message, status = 400) => json({ error: message }, status);

// (a string, trimmed and cut to length - what arrives is not trusted)
const clean = (v, n) => String(v == null ? '' : v).trim().slice(0, n);

// (an address as this base writes them: '0x4378df', lower case, no padding)
function addr(v) {
  const m = /^(?:0x)?([0-9a-fA-F]{1,8})$/.exec(String(v == null ? '' : v).trim());
  if (!m) return '';
  return '0x' + m[1].toLowerCase().replace(/^0+/, '');
}

// (a secret compared without leaking how far it matched)
function sameSecret(a, b) {
  const enc = new TextEncoder();
  const x = enc.encode(a);
  const y = enc.encode(b);
  if (!a || !b || x.byteLength !== y.byteLength) return false;
  return crypto.subtle.timingSafeEqual(x, y);
}

// (D1 takes a batch at a time; a big contribution goes in pieces)
async function run(env, stmts) {
  for (let i = 0; i < stmts.length; i += 50) await env.DB.batch(stmts.slice(i, i + 50));
}

async function home(env) {
  const [builds, byStatus, seen, contrib] = await env.DB.batch([
    env.DB.prepare('SELECT sha256, bytes, label, seen_at, facts FROM builds ORDER BY seen_at DESC LIMIT 10'),
    env.DB.prepare('SELECT kind, status, COUNT(*) n FROM entries GROUP BY kind, status'),
    env.DB.prepare('SELECT COUNT(*) n, COALESCE(SUM(runs), 0) runs FROM seen'),
    env.DB.prepare('SELECT COUNT(*) n, MAX(sent_at) last FROM contributions'),
  ]);
  const s = (seen.results || [])[0] || { n: 0, runs: 0 };
  const c = (contrib.results || [])[0] || { n: 0, last: null };
  return json({
    what: 'The shared knowledge base for the ZT1 Lab: what installs have worked out about zoo.exe.',
    build: THE_BUILD,
    bytes: THE_BYTES,
    label: THE_LABEL,
    builds: builds.results || [],
    entries: byStatus.results || [],
    seen: { situations: s.n, runs: s.runs },
    contributions: c.n,
    last_contribution: c.last,
    // (what the base is and is not. Stated here because this is what a caller
    //  reads first, and two of these are things the Lab must not pretend to)
    trust: {
      entries: 'A finding is quarantined until a reviewer accepts it (POST /review).',
      seen: 'An observation is published at once, unreviewed and unverified: it says this install saw this function run, nothing more.',
      build: 'The build a contribution claims is a SHA the caller chooses, checked only against this base\u2019s build. Nothing makes a caller prove which file it read, so treat it as a claim: what keeps the base sound is that findings wait for review.',
      identity: 'None is kept. Evidence of an installation is never stored, and nothing in a contribution is attributed to an install.',
    },
  });
}

async function pull(env, url) {
  const sha = String(url.searchParams.get('sha') || '').toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(sha)) return fail('sha: zoo.exe\u2019s SHA-256 is needed (64 hex)');
  // (the page size: at least 1 - a negative limit would reach SQLite as
  //  LIMIT -1, which removes the cap entirely and would let one public request
  //  ask for the whole table - and never more than MAX_PULL. `limit=-1` is
  //  clamped rather than refused, so an old caller cannot ask for unlimited)
  const asked = Number(url.searchParams.get('limit'));
  const limit = Math.max(1, Math.min(Number.isFinite(asked) && asked > 0 ? asked : MAX_PULL, MAX_PULL));
  // (where the page starts. Keys are ordered, so this is a stable cursor: a
  //  caller that repeats it gets the same page, and the next one is
  //  `next`/`next_seen`. Without this a client could only ever see the first
  //  page of a base that is bigger than one page - which is what the Lab's own
  //  pull did before)
  const after = String(url.searchParams.get('after') || '').slice(0, 160);
  const afterSeen = String(url.searchParams.get('after_seen') || '').slice(0, 320);
  const build = await env.DB.prepare('SELECT sha256, bytes, label, seen_at, facts FROM builds WHERE sha256 = ?')
    .bind(sha)
    .first();
  if (!build) {
    return json({
      sha,
      known: false,
      note: sha === THE_BUILD
        ? 'This is the build the base is for, but nothing has been contributed yet.'
        : 'No findings on file for this build; the shared base is for ' + THE_BUILD + '.',
      entries: [],
      seen: [],
      next: '',
      next_seen: '',
      more: false,
    });
  }
  const entryRows = await env.DB.prepare(
    `SELECT kind, key, addr, name, status, confidence, summary, extra, source, by, on_date
       FROM entries WHERE build_sha = ? AND status <> 'refused' AND key > ?
      ORDER BY key LIMIT ?`
  )
    .bind(sha, after, limit)
    .all();
  // (seen is ordered by addr and then the rest of its key, so that a page
  //  boundary always lands on a row boundary: two rows can share an addr)
  const seenRows = await env.DB.prepare(
    `SELECT addr, from_addr, when_text, by, on_date, runs FROM seen
      WHERE build_sha = ? AND (addr || char(31) || from_addr || char(31) || when_text) > ?
      ORDER BY addr, from_addr, when_text LIMIT ?`
  )
    .bind(sha, afterSeen, limit)
    .all();
  const entries = entryRows.results || [];
  const seen = seenRows.results || [];
  // (each stream carries its own cursor and its own "more". They are separate
  //  because they run out separately: findings may be paged through for
  //  several pages after the situations have all been read, and a single
  //  `more` with per-stream cursors would make the shorter stream start over -
  //  which it did, handing back rows a caller had already taken.
  //
  //  The cursor is always the last row actually returned, so "after this"
  //  yields nothing for a stream that has nothing left. `more_*` says only
  //  whether that page came back full, i.e. whether there is probably another)
  const seenKey = (s) => `${s.addr}\u001f${s.from_addr || ''}\u001f${s.when_text || ''}`;
  return json({
    sha,
    known: true,
    bytes: build.bytes,
    label: build.label,
    entries,
    seen,
    next: entries.length ? entries[entries.length - 1].key : after,
    next_seen: seen.length ? seenKey(seen[seen.length - 1]) : afterSeen,
    more: entries.length === limit || seen.length === limit,
    more_entries: entries.length === limit,
    more_seen: seen.length === limit,
  });
}

async function contribute(request, env) {
  const body = await request.json().catch(() => null);
  if (!body || typeof body !== 'object') return fail('the body must be JSON');

  const sha = String(body.sha || '').toLowerCase();
  if (!/^[0-9a-f]{64}$/.test(sha)) return fail('sha: zoo.exe\u2019s SHA-256 is needed (64 hex)');
  // (the whole point of the guard: another build's addresses would land on the
  // wrong functions, and silently poison the base for everyone)
  if (sha !== THE_BUILD) {
    return fail(
      'This base is for the ' + THE_LABEL + ' zoo.exe:\n  ' + THE_BUILD +
        '\nThat one is:\n  ' + sha +
        "\nIts addresses don't line up, so nothing was taken.",
      409
    );
  }

  const entries = (Array.isArray(body.entries) ? body.entries : []).slice(0, MAX_ENTRIES);
  const seen = (Array.isArray(body.seen) ? body.seen : []).slice(0, MAX_SEEN);
  if (!entries.length && !seen.length) return fail('nothing to take: entries and seen are both empty');

  // (no identity is kept. `client` arrives from older builds and is accepted so
  //  they keep working, but it is not written anywhere: the base is a combined
  //  database, not a record of which computer sent what)

  const id = crypto.randomUUID();
  const by = clean(body.by, 80);
  const client = clean(body.client, 80);
  const now = new Date().toISOString();
  const stmts = [];

  stmts.push(
    env.DB.prepare(
      `INSERT INTO builds (sha256, bytes, label, seen_at, facts) VALUES (?, ?, ?, ?, 0)
       ON CONFLICT(sha256) DO NOTHING`
    ).bind(sha, THE_BYTES, THE_LABEL, now)
  );
  // (the build's own facts are the server's, never the caller's: `sha` has
  //  already been checked against THE_BUILD, so a contribution may not put its
  //  own byte count or label on the one build this base is about)

  let took = 0;
  let refused = 0;
  for (const raw of entries) {
    const kind = clean(raw && raw.kind, 16).toLowerCase();
    const key = clean(raw && raw.key, 120);
    if (!KINDS.has(kind) || !key) {
      refused++;
      continue;
    }
    // ('0x4378df', 'ZTAnimal+0x128', 'ZTAnimal', '10560': the key the base
    // uses for a thing. `addr` is the absolute address where it has one, so
    // asking "what is at 0x4378df" still finds it)
    const a = addr(raw && raw.addr);
    const conf = CONFIDENCE.has(clean(raw.confidence, 16).toLowerCase())
      ? clean(raw.confidence, 16).toLowerCase()
      : 'likely';
    // (an entry that is already the reviewed answer keeps it: a contribution
    // may suggest, never overwrite what was settled. A provisional entry takes
    // the newer telling, and a 'sure' one always wins over a lesser guess)
    stmts.push(
      env.DB.prepare(
        `INSERT INTO entries (build_sha, kind, key, addr, name, status, confidence, summary, extra, source, by, on_date, proposed, first_id)
         VALUES (?, ?, ?, ?, ?, 'provisional', ?, ?, ?, ?, ?, ?, 1, ?)
         ON CONFLICT(build_sha, kind, key) DO UPDATE SET
           proposed = entries.proposed + 1,
           -- (every field a reviewer settled stays settled: a contribution may
           --  suggest, never overwrite what was accepted - including the
           --  address, which is what every other entry is keyed against)
           addr = CASE
             WHEN entries.status = 'accepted' THEN entries.addr
             WHEN excluded.addr <> '' THEN excluded.addr
             ELSE entries.addr END,
           name = CASE
             WHEN entries.status = 'accepted' THEN entries.name
             WHEN entries.name = '' THEN excluded.name
             ELSE entries.name END,
           extra = CASE
             WHEN entries.status = 'accepted' THEN entries.extra
             WHEN entries.extra = '' THEN excluded.extra
             ELSE entries.extra END,
           -- (whose words we keep must be whose name we keep: a later
           -- contributor's byline beside an earlier finding's summary would
           -- misattribute who found it - so one condition decides all three)
           summary = CASE
             WHEN entries.status = 'accepted' THEN entries.summary
             WHEN excluded.confidence = 'sure' OR entries.confidence <> 'sure' THEN excluded.summary
             ELSE entries.summary END,
           source = CASE
             WHEN entries.status = 'accepted' THEN entries.source
             WHEN excluded.confidence = 'sure' OR entries.confidence <> 'sure' THEN excluded.source
             ELSE entries.source END,
           by = CASE
             WHEN entries.status = 'accepted' THEN entries.by
             WHEN excluded.confidence = 'sure' OR entries.confidence <> 'sure' THEN excluded.by
             ELSE entries.by END,
           confidence = CASE
             WHEN entries.status = 'accepted' THEN entries.confidence
             WHEN excluded.confidence = 'sure' THEN 'sure'
             ELSE entries.confidence END`
      ).bind(
        sha,
        kind,
        key,
        a,
        clean(raw.name, 80),
        conf,
        clean(raw.summary, 400),
        clean(raw.extra, 200),
        clean(raw.source, 40),
        clean(raw.by, 80) || by,
        clean(raw.on_date || raw.on, 20),
        id
      )
    );
    took++;
  }

  let situations = 0;
  for (const raw of seen) {
    const a = addr(raw && raw.addr);
    if (!a) {
      refused++;
      continue;
    }
    // (one row per situation, counted: several players' runs of the same build
    // say more than one - so keep the runs, not every copy of the row)
    stmts.push(
      env.DB.prepare(
        `INSERT INTO seen (build_sha, addr, from_addr, when_text, by, on_date, runs, id)
         VALUES (?, ?, ?, ?, ?, ?, 1, ?)
         ON CONFLICT(build_sha, addr, from_addr, when_text) DO UPDATE SET
           runs = seen.runs + 1, id = excluded.id,
           on_date = CASE WHEN excluded.on_date > seen.on_date THEN excluded.on_date ELSE seen.on_date END`
      ).bind(
        sha,
        a,
        addr(raw.from || raw.from_addr) || '',
        clean(raw.when || raw.when_text, 120),
        clean(raw.by, 80) || by,
        clean(raw.on || raw.on_date, 20),
        id
      )
    );
    situations++;
  }

  stmts.push(
    env.DB.prepare('UPDATE builds SET facts = (SELECT COUNT(*) FROM entries WHERE build_sha = ?) WHERE sha256 = ?').bind(sha, sha)
  );
  stmts.push(
    env.DB.prepare(
      `INSERT INTO contributions (id, build_sha, by, client, sent_at, proposed, refused)
       VALUES (?, ?, ?, ?, ?, ?, ?)`
    ).bind(id, sha, by, '', now, took + situations, refused)
  );

  await run(env, stmts);
  return json({
    ok: true,
    id,
    took,
    situations,
    refused,
    note: 'Kept as provisional until someone reviews it (POST /review).',
    seen_note: 'Observations are published at once, unverified.',
  });
}

async function canReview(request, env, body) {
  const want = env.ZT1_REVIEW_TOKEN;
  if (!want) return 'Reviewing is off: no ZT1_REVIEW_TOKEN is set on this Worker.';
  const given = request.headers.get('x-zt1-token') || (body && body.token) || '';
  return sameSecret(String(given), String(want)) ? '' : 'Not allowed.';
}

async function pending(request, env, url) {
  const body = null;
  const no = await canReview(request, env, body);
  if (no) return fail(no, 401);
  const sha = String(url.searchParams.get('sha') || '').toLowerCase() || THE_BUILD;
  const rows = await env.DB.prepare(
    `SELECT kind, key, addr, name, status, confidence, summary, extra, source, by, on_date, proposed
       FROM entries WHERE build_sha = ? AND status = 'provisional'
      ORDER BY proposed DESC, key LIMIT 1000`
  )
    .bind(sha)
    .all();
  return json({ sha, pending: rows.results || [] });
}

async function review(request, env) {
  const body = await request.json().catch(() => null);
  if (!body || typeof body !== 'object') return fail('the body must be JSON');
  const no = await canReview(request, env, body);
  if (no) return fail(no, 401);

  const sha = String(body.sha || THE_BUILD).toLowerCase();
  const take = Array.isArray(body.take) ? body.take : [];
  const refuse = Array.isArray(body.refuse) ? body.refuse : [];
  // (what a reviewer names a row by. The Worker's own word is `key`: that is
  //  what /pending returns and what the table is keyed by. `addr` is accepted
  //  too, because a reviewer naturally copies the address a function shows -
  //  it matches the row's own key when they are the same thing, or its addr
  //  column when they are not (a field or a slot))
  const names = (raw) => {
    const kind = clean(raw && raw.kind, 16).toLowerCase();
    const key = clean(raw && raw.key, 120);
    const addr = clean(raw && raw.addr, 120);
    return { kind, key, addr };
  };
  const stmts = [];
  let taken = 0;
  let refused = 0;
  let unknown = 0;
  for (const [list, status] of [
    [take, 'accepted'],
    [refuse, 'refused'],
  ]) {
    for (const raw of list) {
      const { kind, key, addr: a } = names(raw);
      if (!KINDS.has(kind)) {
        unknown++;
        continue;
      }
      if (!key && !a) {
        unknown++;
        continue;
      }
      // (a plain UPDATE changes nothing when it matches nothing, so this is
      //  counted by what it actually reached - a reviewer is told when a name
      //  matched no waiting row, rather than being told it worked)
      stmts.push(
        env.DB.prepare(
          `UPDATE entries SET status = ?
            WHERE build_sha = ? AND kind = ? AND status = 'provisional'
              AND (key = ? OR (? <> '' AND addr = ?))`
        ).bind(status, sha, kind, key, a, a)
      );
      if (status === 'accepted') taken++;
      else refused++;
    }
  }
  if (!stmts.length) return fail('nothing to review: give take[] or refuse[]');
  await run(env, stmts);
  return json({ ok: true, accepted: taken, refused, unknown });
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    const p = url.pathname.replace(/\/+$/, '') || '/';
    try {
      if (request.method === 'GET' && p === '/') return await home(env);
      if (request.method === 'GET' && p === '/pull') return await pull(env, url);
      if (request.method === 'GET' && p === '/pending') return await pending(request, env, url);
      if (request.method === 'POST' && p === '/contribute') return await contribute(request, env);
      if (request.method === 'POST' && p === '/review') return await review(request, env);
      return fail('not found: ' + p, 404);
    } catch (e) {
      // (no passThroughOnException: a failure answers as a failure, so the
      // Lab can say what went wrong instead of showing a blank page)
      return fail('server error: ' + (e && e.message ? e.message : String(e)), 500);
    }
  },
};
