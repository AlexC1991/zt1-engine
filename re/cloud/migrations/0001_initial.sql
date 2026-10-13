-- The shared knowledge base: findings about the original's zoo.exe, from
-- every install of the Lab.
--
-- Every fact here is about an ADDRESS in zoo.exe, so every fact is only true
-- of one build: the 2002 Complete Collection (2,449,432 bytes,
-- SHA-256 61fcc6d1...). build_sha is on every row for that reason, and a
-- contribution for any other build is refused - the same rule the Ghidra
-- scripts enforce (re/ghidra_scripts/zt1re.py).
--
-- `key` is how the base itself identifies a thing, because they are not all
-- addresses:
--   function  '0x4378df'            an absolute address
--   global    '0x6377e0'            an absolute address
--   note      '0x42400a'            an absolute address
--   class     'ZTAnimal'            a name (its slots are separate rows)
--   slot      'ZTAnimal+0x100'      a class's slot: name holds the function
--   struct    'ZTAnimal'            a name
--   field     'ZTAnimal+0x128'      a struct's field, by offset
--   const     '10560'               a number (a message id, a limit)
-- `addr` carries the absolute address where there is one, so the base can be
-- asked "what is at 0x4378df" across every kind at once.

CREATE TABLE IF NOT EXISTS builds (
  sha256     TEXT PRIMARY KEY,          -- zoo.exe's SHA-256
  bytes      INTEGER NOT NULL,
  label      TEXT NOT NULL DEFAULT '',  -- human name, e.g. "2002 Complete Collection"
  seen_at    TEXT NOT NULL,             -- first time an install reported it
  facts      INTEGER NOT NULL DEFAULT 0 -- how many are on file for it
);

-- What one install sent, so anything strange can be traced to where it came
-- from.
CREATE TABLE IF NOT EXISTS contributions (
  id         TEXT PRIMARY KEY,          -- crypto.randomUUID()
  build_sha  TEXT NOT NULL,
  by         TEXT NOT NULL DEFAULT '',  -- the name in the entry's `by`
  client     TEXT NOT NULL DEFAULT '',  -- install id, so one machine can be seen
  sent_at    TEXT NOT NULL,
  proposed   INTEGER NOT NULL DEFAULT 0,
  refused    INTEGER NOT NULL DEFAULT 0
);

-- One finding.
--
-- status: 'accepted'    - reviewed and holding as the answer
--         'provisional' - contributed, nobody has said yes (what anything
--                         arriving over the wire becomes)
--         'refused'     - looked at and rejected; kept, not deleted
CREATE TABLE IF NOT EXISTS entries (
  build_sha   TEXT NOT NULL,
  kind        TEXT NOT NULL,
  key         TEXT NOT NULL,            -- the identity, as above
  addr        TEXT NOT NULL DEFAULT '', -- the absolute address, when there is one
  name        TEXT NOT NULL DEFAULT '', -- the human name (a function, a field, a class)
  status      TEXT NOT NULL DEFAULT 'provisional',
  confidence  TEXT NOT NULL DEFAULT 'likely',  -- sure|likely|guess
  summary     TEXT NOT NULL DEFAULT '',
  extra       TEXT NOT NULL DEFAULT '', -- the detail each kind needs: a field's type,
                                        -- a constant's kind
  source      TEXT NOT NULL DEFAULT '', -- 'ghidra', 'lab-recorder', 'class-table', ...
  by          TEXT NOT NULL DEFAULT '',
  on_date     TEXT NOT NULL DEFAULT '', -- when the contributor says they found it
  proposed    INTEGER NOT NULL DEFAULT 0, -- how many times this has arrived
  first_id    TEXT,                     -- the contribution it came in on
  PRIMARY KEY (build_sha, kind, key)
);

-- The original's functions SEEN running, and what the player was doing. This
-- is address-bound like the rest, so it shares: several players' runs of the
-- same zoo build say more than one. A recording FILE is never sent - it is one
-- player's zoo and one session.
--
-- PRIMARY KEY (addr, from_addr, when_text) so a repeat of a situation counts
-- up (runs) instead of piling up rows. Which install reported it is NOT part
-- of the key, and is not stored: the base is a combined database, not a record
-- of who saw what. (An install is therefore not told apart from another, so
-- `runs` says how often something was reported - it is a count of sightings,
-- not of people.)
CREATE TABLE IF NOT EXISTS seen (
  build_sha  TEXT NOT NULL,
  addr       TEXT NOT NULL,             -- the function that ran
  from_addr  TEXT NOT NULL DEFAULT '',  -- who called it
  when_text  TEXT NOT NULL DEFAULT '',  -- 'Starting a freeform zoo (Basic Grass Map, Play)'
  by         TEXT NOT NULL DEFAULT '',
  on_date    TEXT NOT NULL DEFAULT '',
  runs       INTEGER NOT NULL DEFAULT 1, -- times this situation has been reported
  id         TEXT NOT NULL,             -- the last contribution it came in on
  PRIMARY KEY (build_sha, addr, from_addr, when_text)
);
-- (an older database has a `client` column here - the deployed one did. It is
--  simply not written or read any more; SQLite keeps the column, and its
--  default '' is what a row from that time carries. Nothing reads it.)

CREATE INDEX IF NOT EXISTS entries_by_addr ON entries (build_sha, addr);
CREATE INDEX IF NOT EXISTS entries_pending ON entries (build_sha, status);
CREATE INDEX IF NOT EXISTS contributions_recent ON contributions (sent_at);
CREATE INDEX IF NOT EXISTS seen_by_addr ON seen (build_sha, addr);
