-- The `seen` table without an install's identity.
--
-- 0001 gave seen a `client` column and made it part of the primary key, so two
-- installs reporting the same situation stayed two rows. The base no longer
-- keeps an identity - it is a combined database, not a record of who saw what -
-- so the column goes and the key is what the situation is.
--
-- The Worker's insert uses ON CONFLICT(build_sha, addr, from_addr, when_text),
-- which needs a unique index on exactly those columns. SQLite cannot drop a
-- column that is part of a primary key, so the table is rebuilt.
--
-- A row from before carries client = '' (or an install name). Rows that
-- differed only by client describe the same situation, so they are merged and
-- their `runs` added: that is the same "how often was this reported" the
-- column always was, just no longer split per install.
--
-- Running this on a database that never had the column would lose nothing
-- (there is nothing to merge) and is harmless: the GROUP BY is over the same
-- four columns either way, and SUM(runs) of one row is that row's runs.

CREATE TABLE IF NOT EXISTS seen_new (
  build_sha  TEXT NOT NULL,
  addr       TEXT NOT NULL,
  from_addr  TEXT NOT NULL DEFAULT '',
  when_text  TEXT NOT NULL DEFAULT '',
  by         TEXT NOT NULL DEFAULT '',
  on_date    TEXT NOT NULL DEFAULT '',
  runs       INTEGER NOT NULL DEFAULT 1,
  id         TEXT NOT NULL,
  PRIMARY KEY (build_sha, addr, from_addr, when_text)
);

INSERT INTO seen_new (build_sha, addr, from_addr, when_text, by, on_date, runs, id)
SELECT build_sha,
       addr,
       from_addr,
       when_text,
       -- (whichever byline was there: none of them is an identity, and after
       --  this they are no longer told apart)
       MAX(by),
       MAX(on_date),
       SUM(runs),
       MAX(id)
  FROM seen
 GROUP BY build_sha, addr, from_addr, when_text;

DROP TABLE seen;
ALTER TABLE seen_new RENAME TO seen;
