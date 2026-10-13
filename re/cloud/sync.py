"""The shared knowledge base, from the command line.

Every install of the Lab works out things about the original's zoo.exe: what
0x4378df does, what a struct's fields are, which functions a recorded session
ran. None of that needs to stay on one machine - it is all about one build of
one game, so it is the same answer for everybody.

This talks to the Worker in re/cloud (see its README): what it does is

  pull        bring what other installs have found into re/knowledge.json
  contribute  send what this checkout has worked out that nobody has yet
  pending     what is waiting to be reviewed (needs the token)
  review      accept or refuse it

What travels is findings and observations - never a recording. A recording is
one player's zoo and one session; the addresses it *found* are the part worth
sharing, so that is what `contribute` sends.

  python re/cloud/sync.py pull [--api URL] [--all] [--dry-run]
  python re/cloud/sync.py contribute [--api URL] [--all] [--dry-run]
  python re/cloud/sync.py pending [--api URL]
  python re/cloud/sync.py review --take 0x4378df/function --refuse ZTAnimal+0x128/field

The name of the API is kept in re/private/cloud.json ({"api": "https://..."}),
or in ZT1_KB_API, so a checkout only has to be told once.

The one build all of this is about is checked, not assumed: `contribute`
hashes the zoo.exe it is looking at and refuses to send anything if it is not
that build (see build_to_send).
"""
import argparse
import datetime
import hashlib
import json
import os
import re
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.dirname(os.path.dirname(HERE))
KB = os.path.join(ROOT, "re", "knowledge.json")
PRIVATE = os.path.join(ROOT, "re", "private")
CONF = os.path.join(PRIVATE, "cloud.json")
# (what this checkout has already sent up. It lives with the Lab's other state
#  and never leaves the machine: it is a bookmark against resending, not a way
#  of telling installs apart - the shared base records no identity)
MARKS = os.path.join(PRIVATE, "cloud-sent.json")

# The build everything here is about. It is not in knowledge.json: the base is
# keyed by address and assumes this build, and re/ghidra_scripts/zt1re.py is
# where the check lives. Sending it is what makes the sharing safe.
ZOO_SHA256 = "61fcc6d18bdd9339b8ce1ed00ed5a9f1bffca00781266396826f6aa2d00124d5"
ZOO_BYTES = 2449432
ZOO_LABEL = "2002 Complete Collection"


def find_zoo_exe(explicit=None):
    """The zoo.exe this checkout is working on, or None.

    `explicit` is what the Lab passed with --zoo (it knows, because Play and
    the other views use the same path). Failing that, the places the Lab
    looks: re/private/original.json first, then ZT1_EXE, then the usual
    install directories.
    """
    cands = []
    if explicit:
        cands.append(explicit)
    conf = read_json(os.path.join(PRIVATE, "original.json"), {})
    if isinstance(conf, dict) and conf.get("exe"):
        cands.append(conf["exe"])
    if os.environ.get("ZT1_EXE"):
        cands.append(os.environ["ZT1_EXE"])
    for base in (
        r"C:\Program Files (x86)\Microsoft Games\Zoo Tycoon",
        r"C:\Program Files\Microsoft Games\Zoo Tycoon",
    ):
        cands.append(os.path.join(base, "zoo.exe"))
    for c in cands:
        if c and os.path.isfile(c):
            return c
    return None


def build_to_send(assume=False, zoo=None):
    """What to send as the build a contribution is about.

    Never a claim: this reads the zoo.exe and hashes it. If it is not the
    build the shared base is about, the contribution is refused here rather
    than travelling - the addresses in it would land on the wrong functions
    in everybody else's copy. --assume-build sends the known answer without
    looking, which is only right when the file cannot be read but is known
    to be that build (it is what the Lab itself does when it has none).
    """
    if assume:
        return ZOO_SHA256, ZOO_BYTES, ZOO_LABEL
    path = find_zoo_exe(zoo)
    if not path:
        sys.exit(
            "[X] contribution needs the zoo.exe it is about, and none was found.\n"
            "    Point the Lab at your copy first: re/private/original.json\n"
            '    {"exe": "<your zoo.exe>"}, or set ZT1_EXE.\n'
            "    (--assume-build sends the known build without checking, which is\n"
            "     only safe when you know the file is that exact build.)"
        )
    h = hashlib.sha256()
    size = 0
    with open(path, "rb") as f:
        while True:
            b = f.read(1 << 20)
            if not b:
                break
            h.update(b)
            size += len(b)
    got = h.hexdigest()
    say(f"  zoo.exe: {path}")
    say(f"  {size} bytes, sha256 {got[:16]}...")
    if got != ZOO_SHA256:
        sys.exit(
            f"[X] that is not the {ZOO_LABEL} build this base is about\n"
            f"    ({size} bytes; expected {ZOO_BYTES}).\n"
            "    Its addresses would land on the wrong functions, so nothing\n"
            "    was sent. (Use --assume-build only if you know it is that build.)"
        )
    return got, size, ZOO_LABEL


def write_json(path, obj):
    """Small, atomic-ish write, for the send bookmark."""
    tmp = path + ".tmp"
    with open(tmp, "w", encoding="utf-8", newline="\n") as f:
        json.dump(obj, f, indent=1, sort_keys=True)
        f.write("\n")
    os.replace(tmp, path)


# Which of the base's parts are findings (things someone concluded) as against
# observations (what was seen happen). The Lab's own records - re/private, the
# recordings, what Ghidra has loaded - are never sent.
FACT_KINDS = {
    "functions": "function",
    "globals": "global",
    "classes": "class",
    "structs": "struct",
    "constants": "const",
    "notes": "note",
}
ADDR = re.compile(r"^0x[0-9a-f]{1,8}$")


def say(*a):
    print(*a, flush=True)


def now():
    return datetime.date.today().isoformat()


def read_json(path, fallback):
    try:
        with open(path, encoding="utf-8") as f:
            return json.load(f)
    except Exception:
        return fallback


def api_from(a):
    """Where the API is: as asked, else the environment, else this checkout."""
    return (
        a.api
        or os.environ.get("ZT1_KB_API")
        or read_json(CONF, {}).get("api")
        or ""
    ).rstrip("/")


def call(url, path, body=None, token="", timeout=60):
    data = None if body is None else json.dumps(body).encode("utf-8")
    req = urllib.request.Request(url + path, data=data, method="POST" if data else "GET")
    req.add_header("content-type", "application/json")
    # (Cloudflare refuses Python's default user-agent - "error code: 1010" -
    # so the connector says who it is. Without this every call is blocked)
    req.add_header("user-agent", "zt1-lab-cloud-sync/1.0 (+https://github.com/zt1-engine)")
    if token:
        req.add_header("x-zt1-token", token)
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            return json.loads(r.read().decode("utf-8")), None
    except urllib.error.HTTPError as e:
        try:
            return None, json.loads(e.read().decode("utf-8")).get("error") or str(e)
        except Exception:
            return None, str(e)
    except Exception as e:
        return None, str(e)


def addr_of(v):
    """An address as this base writes them, or '' if it isn't one."""
    s = str(v or "").strip().lower()
    if s and not s.startswith("0x"):
        s = "0x" + s
    if ADDR.match(s):
        return "0x" + s[2:].lstrip("0")
    return ""


def entries_from(kb, who=""):
    """The base's findings, as rows the API takes.

    What a thing IS differs by kind, so the identity does too - the base is
    keyed by address, by name, by a struct's field offset or by a number:

        function  0x4378df            an address
        global    0x6377e0            an address
        note      0x42400a            an address
        class     ZTAnimal            a name
        slot      ZTAnimal+0x100      a class's slot (its function is the addr)
        struct    ZTAnimal            a name
        field     ZTAnimal+0x128      a struct's field, by offset
        const     10560               a number

    Nothing private goes: re/private and the recordings are not findings, and
    the Lab's own state is not part of the base.
    """
    out = []
    for part in ("functions", "globals", "notes"):
        kind = {"functions": "function", "globals": "global", "notes": "note"}[part]
        for key, v in (kb.get(part) or {}).items():
            if not isinstance(v, dict):
                continue
            a = addr_of(key)
            if not a:
                continue
            # (something that came down from the shared base is not this
            #  checkout's finding, and sending it back would only echo it: it
            #  is already there, and a pull then a contribute must not reopen
            #  what was already settled)
            if v.get("shared"):
                continue
            out.append({
                "kind": kind,
                "key": a,
                "addr": a,
                "name": str(v.get("name") or ""),
                "confidence": str(v.get("confidence") or ("sure" if kind == "note" else "likely")),
                "summary": str(v.get("summary") or v.get("text") or "")[:400],
                "extra": str(v.get("kind") or "")[:200],
                "source": str(v.get("source") or "")[:40],
                "by": str(v.get("by") or who)[:80],
                "on": str(v.get("on") or "")[:20],
            })

    # (a number: a message id, a limit. It is the same number for everyone)
    for key, v in (kb.get("constants") or {}).items():
        if not isinstance(v, dict):
            continue
        k = str(key).strip().lower()
        k = k[2:] if k.startswith("0x") else k
        if not k.isdigit():
            continue
        out.append({
            "kind": "const",
            "key": k,
            "addr": "",
            "name": "",
            "confidence": str(v.get("confidence") or "likely"),
            "summary": str(v.get("meaning") or v.get("summary") or "")[:400],
            "extra": str(v.get("kind") or "")[:200],
            "source": str(v.get("source") or "")[:40],
            "by": str(v.get("by") or who)[:80],
            "on": str(v.get("on") or "")[:20],
        })

    # (a class and a struct are named things; their slots and fields are the
    # real findings, and each goes up as its own row)
    for part, kind, holder in (("classes", "class", "slots"), ("structs", "struct", "fields")):
        for name, s in (kb.get(part) or {}).items():
            if not isinstance(s, dict):
                continue
            if s.get("shared"):
                continue   # (came down from the base: not ours to send back)
            out.append({
                "kind": kind,
                "key": str(name)[:120],
                "addr": "",
                "name": str(name)[:80],
                "confidence": str(s.get("confidence") or "likely"),
                "summary": str(s.get("summary") or "")[:400],
                "extra": "",
                "source": str(s.get("source") or "")[:40],
                "by": str(s.get("by") or who)[:80],
                "on": str(s.get("on") or "")[:20],
            })
            for off, f in (s.get(holder) or {}).items():
                off = str(off).strip().lower()
                if not off.startswith("0x"):
                    continue
                if kind == "class":
                    # (a slot's value is the function it points at)
                    target = addr_of(f)
                    out.append({
                        "kind": "slot",
                        "key": f"{name}+{off}"[:120],
                        "addr": target,
                        "name": "",
                        "confidence": "sure",
                        "summary": "",
                        "extra": "",
                        "source": "class-table",
                        "by": str(s.get("by") or who)[:80],
                        "on": str(s.get("on") or "")[:20],
                    })
                elif isinstance(f, dict):
                    out.append({
                        "kind": "field",
                        "key": f"{name}+{off}"[:120],
                        "addr": addr_of(f.get("at")) or "",
                        "name": str(f.get("name") or "")[:80],
                        "confidence": str(f.get("confidence") or "likely"),
                        "summary": str(f.get("summary") or "")[:400],
                        "extra": str(f.get("type") or "")[:200],
                        "source": str(f.get("source") or "")[:40],
                        "by": str(f.get("by") or who)[:80],
                        "on": str(f.get("on") or "")[:20],
                    })

    # (the same thing can be reached two ways: one row per identity)
    seen = set()
    rows = []
    for e in out:
        k = (e["kind"], e["key"])
        if k in seen:
            continue
        seen.add(k)
        rows.append(e)
    return rows


def seen_from(kb, who="", client=""):
    """What was seen running in the original, per situation. The recording
    files stay here; the addresses they found are what shares.

    A situation that came down from the shared base is skipped: it is already
    there, and sending it back would only count as another sighting of it.
    """
    out = []
    for a, rows in (kb.get("seen") or {}).items():
        addr = addr_of(a)
        if not addr:
            continue
        for r in rows or []:
            if not isinstance(r, dict):
                continue
            if r.get("shared"):
                continue
            out.append(
                {
                    "addr": addr,
                    "from": addr_of(r.get("from")) or "",
                    "when": str(r.get("when") or "")[:120],
                    "by": str(r.get("by") or who)[:80],
                    "on": str(r.get("on") or "")[:20],
                    "client": client,
                }
            )
    return out


def merge(kb, pulled):
    """What came down goes in, as provisional.

    A thing the base doesn't have is added; one it has is left alone. A shared
    guess never quietly replaces something this checkout worked out - if there
    is already an answer here, the shared one is only noted.

    What is *counted* is what was actually added, not what arrived: a second
    pull of the same page has to be able to say it added nothing, and the
    caller reads these counts.
    """
    added = {"findings": 0, "slots": 0, "fields": 0, "seen": 0}
    for e in pulled.get("entries") or []:
        kind = e.get("kind")
        key = str(e.get("key") or "")
        if not key:
            continue
        shared = {
            "by": f"{e.get('by') or 'someone'} (shared)",
            "on": e.get("on_date") or now(),
            "shared": True,
            "provisional": True,
        }
        if kind == "function":
            where = kb.setdefault("functions", {})
            if key not in where:
                where[key] = {
                    **shared,
                    "name": e.get("name") or "",
                    "summary": e.get("summary") or "",
                    "confidence": e.get("confidence") or "likely",
                }
                added["findings"] += 1
        elif kind == "global":
            where = kb.setdefault("globals", {})
            if key not in where:
                where[key] = {
                    **shared,
                    "name": e.get("name") or "",
                    "summary": e.get("summary") or "",
                    "kind": e.get("extra") or "other",
                    "confidence": e.get("confidence") or "likely",
                }
                added["findings"] += 1
        elif kind == "note":
            where = kb.setdefault("notes", {})
            if key not in where:
                where[key] = {**shared, "text": e.get("summary") or ""}
                added["findings"] += 1
        elif kind == "const":
            where = kb.setdefault("constants", {})
            if key not in where:
                where[key] = {
                    **shared,
                    "kind": e.get("extra") or "number",
                    "meaning": e.get("summary") or "",
                }
                added["findings"] += 1
        elif kind in ("class", "struct"):
            part = "classes" if kind == "class" else "structs"
            holder = "slots" if kind == "class" else "fields"
            where = kb.setdefault(part, {})
            if key not in where:
                where[key] = {**shared, holder: {}}
                added["findings"] += 1
            where[key].setdefault(holder, {})
        elif kind in ("slot", "field"):
            name, _, off = key.partition("+")
            if not name or not off.startswith("0x"):
                continue
            part = "classes" if kind == "slot" else "structs"
            holder = "slots" if kind == "slot" else "fields"
            holders = kb.setdefault(part, {})
            if name not in holders:
                holders[name] = {**shared, holder: {}}
            s = holders[name]
            s.setdefault(holder, {})
            if kind == "slot":
                if off not in s[holder] and e.get("addr"):
                    s[holder][off] = e["addr"]
                    added["slots"] += 1
            elif off not in s[holder]:
                s[holder][off] = {
                    **shared,
                    "name": e.get("name") or "",
                    "summary": e.get("summary") or "",
                    "type": e.get("extra") or "",
                    "confidence": e.get("confidence") or "likely",
                }
                added["fields"] += 1
    for s in pulled.get("seen") or []:
        a = addr_of(s.get("addr"))
        if not a:
            continue
        rows = kb.setdefault("seen", {}).setdefault(a, [])
        mark = (s.get("from_addr") or "", s.get("when_text") or "")
        if not any((r.get("from") or "", r.get("when") or "") == mark for r in rows):
            rows.append({
                "by": f"{s.get('by') or 'someone'} (shared)",
                "from": mark[0],
                "on": s.get("on_date") or now(),
                "when": mark[1],
                "shared": True,
                # (observations are published the moment they arrive - nobody
                #  reviews them - so a shared one is marked unverified. The
                #  runs count says how many installs reported it)
                "unverified": True,
                "runs": int(s.get("runs") or 1),
            })
            added["seen"] += 1
    return added


def write_kb(kb):
    """knowledge.json as kb.py writes it: sorted keys, a trailing newline."""
    with open(KB, "w", encoding="utf-8", newline="\n") as f:
        json.dump(kb, f, indent=1, sort_keys=True)
        f.write("\n")


def cmd_pull(a):
    api = api_from(a)
    kb = read_json(KB, None)
    if kb is None:
        sys.exit("[X] re/knowledge.json isn't there")
    sha = a.sha or ZOO_SHA256
    # (the base is far bigger than one page - 18,000-odd situations - so this
    #  asks for page after page until the Worker says there is no more. Asking
    #  once, as this used to, left most of the shared base unseen and the same
    #  first page was fetched again every time)
    total_e = total_s = 0
    pages = 0
    after = after_seen = ""
    unsaved = 0
    while True:
        q = (f"/pull?sha={sha}&after={urllib.parse.quote(after)}"
             f"&after_seen={urllib.parse.quote(after_seen)}")
        said, err = call(api, q)
        if err:
            if pages and not a.dry_run and unsaved:
                write_kb(kb)
                say(f"  [!] stopped after {pages} pages: {err}")
                say("      what came down so far was kept")
                return
            sys.exit(f"[X] {err}")
        if pages == 0 and not said.get("known"):
            say(f"  nothing on file for {sha[:12]}... yet")
            return
        pages += 1
        got_e = said.get("entries") or []
        got_s = said.get("seen") or []
        total_e += len(got_e)
        total_s += len(got_s)
        if pages == 1:
            say(f"  {said.get('label')}: reading it a page at a time")
        adding = merge(kb, said)
        n_added = sum(adding.values())
        # (kept as it goes: a long pull that is interrupted must not throw away
        #  the pages it has already read)
        if not a.dry_run and n_added:
            write_kb(kb)
        else:
            unsaved += n_added
        for k, n in adding.items():
            if n:
                say(f"    + {n} {k}")
        if pages <= 3 or not said.get("more"):
            say(f"    page {pages}: {len(got_e)} findings, {len(got_s)} situations")
        # (both cursors move on, whether or not that stream had a full page:
        #  a stream that is done keeps its last cursor, so asking after it
        #  returns nothing rather than starting over)
        nxt, nxt_seen = said.get("next") or "", said.get("next_seen") or ""
        moved = (nxt and nxt != after) or (nxt_seen and nxt_seen != after_seen)
        after, after_seen = nxt or after, nxt_seen or after_seen
        if not said.get("more") or not moved:
            break
    say(f"  read {pages} pages: {total_e} findings, {total_s} situations")
    if a.dry_run:
        say(f"  (dry run: re/knowledge.json not written; {unsaved} things would be added)")
        return
    say("  re/knowledge.json is up to date (what came down is marked shared and provisional)")


def cmd_contribute(a):
    api = api_from(a)
    kb = read_json(KB, None)
    if kb is None:
        sys.exit("[X] re/knowledge.json isn't there")
    who = a.by or ""
    if not who:
        try:
            with open(os.path.join(PRIVATE, "me.txt"), encoding="utf-8") as f:
                who = f.read().strip()
        except Exception:
            who = ""
    # (no identity: what a contribution says about where it came from is not
    #  kept, by either end. `client` travels empty, and the Worker drops it)
    client = ""
    entries = entries_from(kb, who)
    seen = seen_from(kb, who, client)
    if not a.all:
        # (the default: mine, and only what nobody has sent up yet)
        mine = [e for e in entries if who and who.split()[0] in e["by"]]
        entries = mine or entries
    # (what this checkout last sent. A contribution is not an identity: the
    #  column that would name an install travels empty (the Lab is a combined
    #  database, not a record of who did what). But re-running must not resend
    #  the whole base: the Worker counts a repeat of a situation as another
    #  `runs`, so a second contribute would inflate every count. This is the
    #  local bookmark that stops that - it never leaves the machine)
    marks = read_json(MARKS, {}) if isinstance(read_json(MARKS, {}), dict) else {}
    sent_e = set(marks.get("entries") or [])
    sent_s = set(marks.get("seen") or [])
    before_e, before_s = len(entries), len(seen)
    if sent_e or sent_s:
        entries = [e for e in entries if f"{e['kind']}|{e['key']}" not in sent_e]
        seen = [s for s in seen if f"{s['addr']}|{s['from']}|{s['when']}" not in sent_s]
    skipped = (before_e - len(entries)) + (before_s - len(seen))
    say(f"  sending {len(entries)} findings, {len(seen)} situations"
        + (f" ({skipped} already sent from here)" if skipped else ""))
    if a.dry_run:
        # (still check the build: a dry run that would have been refused is
        #  worth knowing about before the real one)
        sha, size, label = build_to_send(a.assume_build, a.zoo)
        say(f"  ({label}: {size} bytes - would be sent as {sha[:16]}...)")
        say("  (dry run: nothing sent)")
        return
    sha, size, label = build_to_send(a.assume_build, a.zoo)

    # (a base this size does not fit one request: the Worker takes 500
    # findings and 2000 situations at a time, so it goes in pieces. Each piece
    # is its own contribution, so nothing is lost and any piece can be traced)
    CHUNK_E, CHUNK_S = 400, 1500
    took = situations = refused = 0
    pieces = max(1, -(-len(entries) // CHUNK_E), -(-len(seen) // CHUNK_S))
    for i in range(pieces):
        body = {
            "sha": sha,
            "bytes": size,
            "label": label,
            "by": who,
            "client": client,
            "entries": entries[i * CHUNK_E : (i + 1) * CHUNK_E],
            "seen": seen[i * CHUNK_S : (i + 1) * CHUNK_S],
        }
        if not body["entries"] and not body["seen"]:
            continue
        said, err = call(api, "/contribute", body)
        if err:
            say(f"  piece {i + 1}/{pieces} failed: {err}")
            if not took:
                sys.exit("[X] nothing was taken")
            break
        took += said.get("took") or 0
        situations += said.get("situations") or 0
        refused += said.get("refused") or 0
        if pieces > 1:
            say(f"  piece {i + 1}/{pieces}: {said.get('took')} findings, {said.get('situations')} situations")
        # (this piece is on its way: remember it, so an interrupted send can be
        #  resumed without resending what already landed)
        for e in body["entries"]:
            sent_e.add(f"{e['kind']}|{e['key']}")
        for s in body["seen"]:
            sent_s.add(f"{s['addr']}|{s['from']}|{s['when']}")
        write_json(MARKS, {"entries": sorted(sent_e), "seen": sorted(sent_s)})
    say(f"  {took} findings and {situations} situations kept, {refused} refused")
    say("  they are provisional until someone reviews them (sync.py pending)")


def cmd_pending(a):
    api = api_from(a)
    token = a.token or os.environ.get("ZT1_KB_TOKEN", "")
    said, err = call(api, "/pending", token=token)
    if err:
        sys.exit(f"[X] {err}")
    rows = said.get("pending") or []
    if not rows:
        say("  nothing waiting")
        return
    say(f"  {len(rows)} waiting to be reviewed:")
    for r in rows:
        # (the row names itself by what the Worker keys it on, falling back to
        #  its address: this is exactly what `review` then takes)
        named = r.get("key") or r.get("addr") or ""
        say(f"    {named:>20}  {r['kind']:<8} {r.get('name') or '':<18} {r.get('confidence'):<7} {r.get('by')}")
        if r.get("summary"):
            say(f"                {r['summary']}")
    say("  accept or refuse with:  sync.py review --take <key>/<kind>  (the first column and the kind)")


def cmd_review(a):
    api = api_from(a)
    token = a.token or os.environ.get("ZT1_KB_TOKEN", "")
    if not token:
        sys.exit("[X] reviewing needs the token: --token, or ZT1_KB_TOKEN")

    def parse(items):
        out = []
        for it in items or []:
            bits = it.split("/")
            if len(bits) < 2:
                sys.exit(
                    f"[X] {it}: write it as <key>/<kind>  (as `pending` lists them, e.g.\n"
                    "    0x4378df/function, or ZTAnimal+0x128/field, or 10560/const)"
                )
            name, kind = bits[0], bits[1]
            # (the Worker keys rows by `key`, and also matches `addr`. Send the
            #  name as both when it is an address, so a copied address resolves
            #  either way round; a third part is taken as the key when given)
            key = bits[2] if len(bits) > 2 else name
            out.append({"key": key, "kind": kind, "addr": name if ADDR.match(name) else ""})
        return out

    take, refuse = parse(a.take), parse(a.refuse)
    if not take and not refuse:
        sys.exit("[X] nothing to review: give --take or --refuse")
    said, err = call(api, "/review", {"take": take, "refuse": refuse}, token=token)
    if err:
        sys.exit(f"[X] {err}")
    say(f"  accepted {said.get('accepted')}, refused {said.get('refused')}")
    if said.get("unknown"):
        say(f"  {said['unknown']} of those named nothing to review (check the key and kind)")


def main():
    # (the options belong to every subcommand, so they read the way the
    # docstring does: sync.py pull --api URL --dry-run)
    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--api", default="", help="the Worker's URL (else ZT1_KB_API, else re/private/cloud.json)")
    common.add_argument("--sha", default="", help="the build to pull (default: the 2002 Complete Collection)")
    common.add_argument("--token", default="", help="the review token (else ZT1_KB_TOKEN)")
    common.add_argument("--by", default="", help="your name (else re/private/me.txt)")
    common.add_argument("--all", action="store_true", help="contribute: send everything, not only your own")
    common.add_argument("--dry-run", action="store_true", help="say what would happen, change nothing")

    ap = argparse.ArgumentParser(description="The shared knowledge base (see re/cloud/README.md)", parents=[common])
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("pull", help="bring what others found into re/knowledge.json", parents=[common]).set_defaults(fn=cmd_pull)
    c = sub.add_parser("contribute", help="send what this checkout has worked out", parents=[common])
    # (the build is read from the actual zoo.exe by default. This skips that
    #  read, for when the file is somewhere this cannot see)
    c.add_argument("--assume-build", action="store_true",
                   help="send the known build without reading zoo.exe")
    # (the Lab passes the zoo.exe it already resolved - Play and every other
    #  view use it, so there is no guessing from re/private, which an installed
    #  Lab does not have)
    c.add_argument("--zoo", default="", metavar="EXE",
                   help="the zoo.exe to hash (else it is looked for)")
    c.set_defaults(fn=cmd_contribute)
    sub.add_parser("pending", help="what is waiting to be reviewed", parents=[common]).set_defaults(fn=cmd_pending)
    r = sub.add_parser("review", help="accept or refuse what is waiting", parents=[common])
    r.add_argument("--take", action="append", default=[], metavar="KEY/KIND")
    r.add_argument("--refuse", action="append", default=[], metavar="KEY/KIND")
    r.set_defaults(fn=cmd_review)
    a = ap.parse_args()
    fn = getattr(a, "fn", None)
    if fn is None:  # (no subcommand)
        ap.print_help()
        return
    if fn is not cmd_review and not api_from(a):
        sys.exit("[X] no API to talk to: --api, ZT1_KB_API, or re/private/cloud.json")
    say(f"[{a.cmd}] {api_from(a) or '(local)'}")
    fn(a)


if __name__ == "__main__":
    main()
