# The shared knowledge base

Every install of the Lab works out things about the original's `zoo.exe`: what
`0x4378df` does, what a struct's fields are, which functions a recorded session
ran. None of it belongs to one machine — it is all about one build of one game,
so it is the same answer for everybody. This folder is where that gets shared.

```
  the Lab  ─┐
            ├─ re/cloud/sync.py ──►  the Worker (src/index.js) ──►  D1
  the CLI  ─┘                            re/cloud/
  CI       ─┘
```

`re/knowledge.json` stays the canonical copy: it is what ships in the installer,
what the Lab reads, and what you review in git. The Worker is a way of *moving*
findings between checkouts, not a second home for them.

## Why one build, and why that is the point

Every fact in the base is about an **address**, and an address only means
something for one build of the game. `re/ghidra_scripts/zt1re.py` already
enforces this — it checks `zoo.exe`'s SHA-256 on load and warns that "addresses
won't line up" for any other build — and `re/README.md` says to import the
**2002 Complete Collection** build (2,449,432 bytes).

The Worker enforces the same thing, and it has to: a contribution carrying
another build's addresses would land on the wrong functions and quietly poison
the base for everyone. So every row carries `build_sha`, and a contribution for
any other build is refused with a 409 and nothing taken:

```
This base is for the 2002 Complete Collection zoo.exe:
  61fcc6d1...24d5
That one is:
  aaaa...
Its addresses don't line up, so nothing was taken.
```

## What shares, and what does not

| Shares | Why |
|---|---|
| `functions`, `globals`, `notes` | about an address in the one build |
| `classes`, `slots`, `structs`, `fields` | about a class or struct by name, fields by offset |
| `constants` | a message id, a limit: the same number for everyone |
| what was **seen** running, per situation | "0x40100b ran when starting a freeform zoo" |

| Does not share | Why |
|---|---|
| a **recording** (`re/private/recordings/`) | it is one player's zoo and one session |
| `re/private/` | where you keep what is yours: `me.txt`, where `zoo.exe` is, the recordings |
| the Lab's state | window size, page log, claude index — nobody's business |

A recording's *addresses* do share: `contribute` sends the `seen` table, which
says which function ran in which situation, and never the recording itself.

## What is trusted, and what is only claimed

Two things arrive here that are **not** verified by the Worker, and the base is
honest about both rather than pretending otherwise:

**The build is a claim.** The Worker compares the `sha` a contribution sends
against the one build this base is about, and refuses anything else. That stops
an honest client from sending the wrong file. It cannot stop a direct HTTP call
quoting the approved SHA while sending another build's addresses. Provenance is
not something a public endpoint can establish here, so what keeps the base sound
is the rule below: a **finding** is quarantined until a reviewer accepts it, and
the addresses are what the reviewer checks against the real `zoo.exe`. Treat the
SHA as a claim, never as proof.

**An observation is published unverified.** `seen` has no review step: a
situation arrives and every install can pull it at once. That is what makes the
combined picture useful, and it is also the one part of the base a stranger can
add to freely. So a pulled observation is marked `unverified: true` in
`re/knowledge.json`, `runs` says how many installs reported it, and nothing
depends on it on its own — an observation points at a function, it does not
name it.

**No identity is kept.** The base is a combined database, not a record of who
did what. An install is never named, a contribution is never attributed to one,
and the `client` field older builds still send is accepted and then dropped.
That has one consequence worth stating: because installs are not told apart, a
repeat of the same observation cannot be separated from its being seen again —
`runs` counts how often something was *reported*, not how many people saw it.
The Lab avoids inflating it by remembering what it has already sent
(`re/private/cloud-sent.json`), and re-running `contribute` sends only what is
new.

## Trust: nothing is the answer on arrival

A contribution lands **`provisional`**, visible to everybody but marked as
nobody-has-said-yet. It only becomes `accepted` when it is reviewed. Three rules
keep it honest:

- an entry that is already **accepted keeps it** — a contribution may suggest,
  never overwrite what was settled;
- a **`sure`** entry always wins over a `guess`;
- nothing is **deleted**: a refused finding is kept and marked.

Reviewing needs the Worker's `ZT1_REVIEW_TOKEN`, so only you settle the base.

## Setting it up

```bash
cd re/cloud
npx wrangler d1 create zt1-knowledge          # prints the database_id
#   put that id in wrangler.jsonc (database_id)
npx wrangler d1 migrations apply zt1-knowledge --remote
npx wrangler secret put ZT1_REVIEW_TOKEN      # the token you review with
npx wrangler deploy                           # prints the URL
```

The released Lab and this checkout include the public endpoint in
`re/cloud/config.json`, so users can pull and contribute without configuring a
URL. `ZT1_KB_API` or `re/private/cloud.json` can override that default for a
different deployment. The review token remains server-only; it is never
included in the Lab.

To override the endpoint in a checkout:

```bash
echo {"api": "https://zt1-knowledge.<you>.workers.dev"} > re/private/cloud.json
```

or `set ZT1_KB_API=...`. CI can pass `--api` instead.

**To work on it locally**, no account is needed:

```bash
npx wrangler d1 migrations apply zt1-knowledge --local
echo ZT1_REVIEW_TOKEN=local-dev-token > .dev.vars
npx wrangler dev --local --port 8788
```

`.dev.vars` holds the local review token and is **not** committed.

## Using it

```bash
python re/cloud/sync.py pull                  # bring in what others found
python re/cloud/sync.py contribute            # send what this checkout worked out
python re/cloud/sync.py pending               # what is waiting to be reviewed
python re/cloud/sync.py review --take 0x4378df/function/
python re/cloud/sync.py review --refuse 0x4a1b20/function/
```

`pull` and `contribute` take `--dry-run` to say what would happen and change
nothing, and `--all` to send everything rather than only your own work.

A pull adds what the base has and this checkout does not, marked `shared` and
`provisional`; it never overwrites an answer this checkout already has. Then
`ZT1_Apply` in Ghidra shows it like anything else in the base.

## The API

| | | |
|---|---|---|
| `GET /` | | what this is, and the numbers |
| `GET /pull?sha=<64 hex>` | | the shared base for that build |
| `GET /pending?sha=...` | token | what is waiting to be reviewed |
| `POST /contribute` | | `{ sha, bytes?, label?, by?, client?, entries[], seen[] }` |
| `POST /review` | token | `{ take:[{kind,key}], refuse:[{kind,key}] }` |

How a thing is identified, because they are not all addresses:

| kind | key | example |
|---|---|---|
| `function` `global` `note` | an address | `0x4378df` |
| `class` `struct` | a name | `ZTAnimal` |
| `slot` | class + offset (its function is `addr`) | `ZTAnimal+0x100` |
| `field` | struct + offset | `ZTAnimal+0x128` |
| `const` | a number | `10560` |

## Checking it works

The whole thing runs locally with no account: apply the migration, start
`wrangler dev`, and exercise the endpoints. What has to hold:

- a contribution from another build is **refused** (409), and nothing is taken;
- a contribution arrives **provisional**, and is invisible to `pending` without
  the token;
- an **accepted** entry survives a second install's worse `guess` unchanged —
  including its address;
- `pull` reads **every page** of a base bigger than one request, and a repeated
  pull adds nothing;
- `contribute` twice sends the second time **only what is new**;
- `pull` then `contribute` leaves `re/knowledge.json` valid for `kb.py`.

A base this size does not fit one request (1,817 functions and 18,000-odd
situations today), so both directions go in pages: `contribute` sends 400
findings and 1,500 situations at a time, each piece its own contribution, and
`pull` follows the Worker's `next` / `next_seen` cursors until it says there is
no more — so nothing is silently dropped at either end.
